
#include "chat.h"

static chatData g_chatData;

chatData& chat::GetChatData()
{
    return g_chatData;
}

std::string GetBackendInfo() {
    std::string result;

    size_t count = ggml_backend_dev_count();

    for (size_t i = 0; i < count; ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);

        const char * name        = ggml_backend_dev_name(dev);
        const char * description = ggml_backend_dev_description(dev);

        if (name) {
            result += name;
        }

        if (description) {
            result += " - ";
            result += description;
        }

        result += "\n";
    }

    return result;
}

std::vector<llama_token> Tokenize(chatData & data, const std::string & text) {
    if (!data.vocab) {
        return {};
    }

    int n_tokens = -llama_tokenize(data.vocab, text.c_str(), static_cast<int32_t>(text.size()), nullptr, 0, true, true);

    if (n_tokens <= 0) {
        return {};
    }

    std::vector<llama_token> tokens(n_tokens);

    int result = llama_tokenize(data.vocab, text.c_str(), static_cast<int32_t>(text.size()), tokens.data(), n_tokens,
                                true, true);

    if (result < 0) {
        return {};
    }

    tokens.resize(result);

    return tokens;
}

bool chat::LoadModel(chatData & data, void (*status)(std::string), const char * path) {
    // Clean up the previous sampler.
    if (data.sampler) {
        llama_sampler_free(data.sampler);
        data.sampler = nullptr;
    }

    // Clean up the previous context.
    if (data.ctx) {
        llama_free(data.ctx);
        data.ctx = nullptr;
    }

    // Clean up the previous model.
    if (data.model) {
        data.model = nullptr;
        data.vocab = nullptr;
    }

    llama_backend_init();

    std::string backendInfo = GetBackendInfo();

    // Load model.
    llama_model_params model_params = llama_model_default_params();

    data.model = llama_model_load_from_file(path, model_params);

    if (!data.model) {
        status("Failed to load model");
        return false;
    }

    // Get vocabulary owned by the model.
    data.vocab = llama_model_get_vocab(data.model);

    if (!data.vocab) {
        llama_model_free(data.model);
        data.model = nullptr;

        status("Failed to get vocabulary");
        return false;
    }

    // Create context.
    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.n_ctx = 16384;

    data.ctx = llama_init_from_model(data.model, ctx_params);

    if (!data.ctx) {
        llama_model_free(data.model);
        data.model = nullptr;
        data.vocab = nullptr;

        status("Failed to create context");
        return false;
    }

    status("Model loaded:\n" + backendInfo);
    return true;
}

std::string chat::SendChatMessage(chatData & data, const std::string & userText) {
    if (!data.model || !data.ctx) {
        return "No model loaded.";
    }

    return GenerateResponse(data, userText);
}

// Generates one token per call.
// Returns false when generation ends or an error occurs.
bool chat::GenerateNextToken(chatData & data) {
    // Evaluate the current batch.
    int decodeResult = llama_decode(data.ctx, data.batch);

    if (decodeResult != 0) {
        data.response = "Failed to decode batch.";
        return false;
    }

    // Sample the next token.
    llama_token token = llama_sampler_sample(data.sampler, data.ctx, -1);

    // Stop at the end-of-generation token.
    if (llama_vocab_is_eog(data.vocab, token)) {
        return false;
    }

    // Convert token to text.
    char buffer[256];

    int n = llama_token_to_piece(data.vocab, token, buffer, sizeof(buffer), 0, true);

    if (n < 0) {
        data.response = "Failed to convert token.";
        return false;
    }

    data.response.append(buffer, n);

    // Prepare the next batch.
    // next_token is a chatData member, so its address
    // remains valid after this function returns.
    data.next_token = token;

    data.batch = llama_batch_get_one(&data.next_token, 1);

    return true;
}

std::string chat::GenerateResponse(chatData & data, const std::string & userText) {
    if (!data.model || !data.ctx) {
        return "No model loaded.";
    }

    // Ensure vocabulary is available.
    data.vocab = llama_model_get_vocab(data.model);

    if (!data.vocab) {
        return "Failed to get model vocabulary.";
    }

    // Clean up a previous sampler, if any.
    if (data.sampler) {
        llama_sampler_free(data.sampler);
        data.sampler = nullptr;
    }

    data.response.clear();

    // Prepare the chat message.
    llama_chat_message message{ "user", userText.c_str() };

    const char * tmpl = llama_model_chat_template(data.model, nullptr);

    if (!tmpl) {
        return "Model does not provide a chat template.";
    }

    // Apply chat template.
    int promptSize = llama_chat_apply_template(tmpl, &message, 1, true, nullptr, 0);

    if (promptSize < 0) {
        return "Failed to apply chat template.";
    }

    std::vector<char> formatted(promptSize + 1);

    int formattedSize =
        llama_chat_apply_template(tmpl, &message, 1, true, formatted.data(), static_cast<int32_t>(formatted.size()));

    if (formattedSize < 0) {
        return "Failed to format prompt.";
    }

    std::string prompt(formatted.data(), formattedSize);

    // Granite 4.2 thinking-mode workaround.
    const std::string thinkPrefix = "<think>\n";

    if (prompt.size() < thinkPrefix.size() ||
        prompt.compare(prompt.size() - thinkPrefix.size(), thinkPrefix.size(), thinkPrefix) != 0) {
        prompt += thinkPrefix;
    }

    // Tokenize prompt.
    std::vector<llama_token> tokens = Tokenize(data, prompt);

    if (tokens.empty()) {
        return "Failed to tokenize prompt.";
    }

    // Create sampler.
    auto samplerParams = llama_sampler_chain_default_params();

    data.sampler = llama_sampler_chain_init(samplerParams);

    if (!data.sampler) {
        return "Failed to create sampler.";
    }

    llama_sampler_chain_add(data.sampler, llama_sampler_init_top_k(40));

    llama_sampler_chain_add(data.sampler, llama_sampler_init_top_p(0.95f, 1));

    llama_sampler_chain_add(data.sampler, llama_sampler_init_temp(1.0f));

    llama_sampler_chain_add(data.sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    // Initialize the first batch.
    // tokens stays alive until generation finishes.
    data.batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));

    // Generate until an end token or an error.
    while (GenerateNextToken(data)) {
    }

    // Save the response before cleanup.
    std::string result = data.response;

    llama_sampler_free(data.sampler);
    data.sampler = nullptr;

    return result;
}
