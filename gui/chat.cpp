
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

void CleanUpPrevious(chatData & chatData) {
    // Clean up the previous sampler.
    if (chatData.sampler) {
        llama_sampler_free(chatData.sampler);
        chatData.sampler = nullptr;
    }

    // Clean up the previous context.
    if (chatData.ctx) {
        llama_free(chatData.ctx);
        chatData.ctx = nullptr;
    }

    // Clean up the previous model.
    if (chatData.model) {
        chatData.model = nullptr;
        chatData.vocab = nullptr;
    }
}

void InitializeModel(chatData & chatData, const char * path, void (*onStatusChanged)(std::string)) {
    // initialize the model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers       = 4096;
    int           n_ctx             = 4096;
    llama_model * model = llama_model_load_from_file(path, model_params);
    if (!model) {
        onStatusChanged("%s: error: unable to load model\n");
        return;
    }

    const llama_vocab * vocab = llama_model_get_vocab(model);

    // initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx                = n_ctx;
    ctx_params.n_batch              = n_ctx;

    llama_context * ctx = llama_init_from_model(model, ctx_params);
    if (!ctx) {
        onStatusChanged("%s: error: failed to create the llama_context\n");
        return;
    }

    // initialize the sampler
    llama_sampler * smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    llama_batch_ext * batch = llama_batch_ext_init(ctx);
    chatData.ctx            = ctx;
    chatData.model          = model;
    chatData.vocab          = vocab;
}

bool chat::LoadModel(chatData& chatData, void (*onStatusChanged)(std::string), const char * path) {
    CleanUpPrevious(chatData);
    llama_backend_init();

    std::string backendInfo = GetBackendInfo();
    InitializeModel(chatData, path, onStatusChanged);
    /*// Load model.
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers       = -1;

    chatData.model = llama_model_load_from_file(path, model_params);

    if (!chatData.model) {
        onStatusChanged("Failed to load model");
        return false;
    }

    // Get vocabulary owned by the model.
    chatData.vocab = llama_model_get_vocab(chatData.model);

    if (!chatData.vocab) {
        llama_model_free(chatData.model);
        chatData.model = nullptr;

        onStatusChanged("Failed to get vocabulary");
        return false;
    }

    // Create context.
    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.n_ctx = 16384;
    ctx_params.n_batch  = 16384;
    ctx_params.n_ubatch = 16384;

    chatData.ctx = llama_init_from_model(chatData.model, ctx_params);*/

    if (!chatData.ctx) {
        llama_model_free(chatData.model);
        chatData.model = nullptr;
        chatData.vocab = nullptr;

        onStatusChanged("Failed to create context");
        return false;
    }

    onStatusChanged("Model loaded:\n" + backendInfo);
    return true;
}

std::string chat::SendChatMessage(chatData & data, const std::string & userText, void (*incompleteResponse)()) {
    if (!data.model || !data.ctx) {
        return "No model loaded.";
    }

    return GenerateResponse(data, userText, incompleteResponse);
}

// Generates one token per call.
// Returns false when generation ends or an error occurs.
bool chat::GenerateNextToken(chatData & data) {
    // Evaluate the current batch.
    data.timeCalled  = data.timeCalled + 1;
    int decodeResult = llama_decode(data.ctx, data.batch);

    if (decodeResult != 0) {
        data.response = "Failed to decode batch.";
        data.isGenerating = false;
        return false;
    }

    // Sample the next token.
    llama_token token = llama_sampler_sample(data.sampler, data.ctx, -1);

    // Stop at the end-of-generation token.
    if (llama_vocab_is_eog(data.vocab, token)) {
        data.isGenerating = false;
        return false;
    }

    // Convert token to text.
    char buffer[256];

    int n = llama_token_to_piece(data.vocab, token, buffer, sizeof(buffer), 0, true);

    if (n < 0) {
        data.response = "Failed to convert token.";
        data.isGenerating = false;
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

std::string chat::GenerateResponse(chatData & data, const std::string & userText, void (*incompleteResponse)()) {
    if (!data.isGenerating) {
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

        int formattedSize = llama_chat_apply_template(tmpl, &message, 1, true, formatted.data(),
                                                      static_cast<int32_t>(formatted.size()));

        if (formattedSize < 0) {
            return "Failed to format prompt.";
        }

        std::string prompt(formatted.data(), formattedSize);

        // Granite 4.2 thinking-mode workaround.
        const std::string thinkPrefix = "";

        //thinkPrefix = "<think>\n";
        /* if (prompt.size() < thinkPrefix.size() ||
            prompt.compare(prompt.size() - thinkPrefix.size(), thinkPrefix.size(), thinkPrefix) != 0) {
            prompt += thinkPrefix;
        }*/

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
        data.isGenerating = true;
    }

    int count = 0;
    std::string result = data.response; 
    while (count < 1 && data.isGenerating) {
        count++;
        GenerateNextToken(data);

        // Save the response before cleanup.
        result = data.response;

        if (!data.isGenerating) {
            llama_sampler_free(data.sampler);
            data.sampler = nullptr;
        } else {
            incompleteResponse();
        }
    }
    return result;
}
