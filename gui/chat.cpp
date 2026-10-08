#include "chat.h"
#include <vector>
#include <windows.h>

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

std::string chat::WideToUtf8(const wchar_t * text) {
    if (!text || !*text) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if (size <= 0) {
        return {};
    }

    std::string result(size - 1, '\0');

    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
    return result;
}

std::vector<llama_token> Tokenize(chatData & data, const std::string & text) {
    const llama_vocab * vocab = llama_model_get_vocab(data.model);

    int n_tokens = -llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()), nullptr, 0, true, true);

    if (n_tokens <= 0) {
        return {};
    }

    std::vector<llama_token> tokens(n_tokens);

    int result =
        llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()), tokens.data(), n_tokens, true, true);

    if (result < 0) {
        return {};
    }

    tokens.resize(result);

    return tokens;
}

bool chat::LoadModel(chatData & data, HWND window, HWND status, const char * path) {
    // Clean up previous model/context
    if (data.ctx) {
        llama_free(data.ctx);
        data.ctx = nullptr;
    }

    if (data.model) {
        llama_model_free(data.model);
        data.model = nullptr;
    }

    llama_backend_init();

    std::string backendInfo = GetBackendInfo();

    // Load model
    llama_model_params model_params = llama_model_default_params();

    data.model = llama_model_load_from_file(path, model_params);

    if (!data.model) {
        chat::SetStatus("Failed to load model", status);
        return false;
    }

    // Create context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 16384;

    data.ctx = llama_init_from_model(data.model, ctx_params);

    if (!data.ctx) {
        llama_model_free(data.model);
        data.model = nullptr;

        chat::SetStatus("Failed to create context", status);
        return false;
    }

    chat::SetStatus("Model loaded:\n" + backendInfo, status);

    return true;
}

void chat::SetStatus(const std::string & text, HWND status) {
    if (status) {
        int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

        if (size > 0) {
            std::wstring wideText(size, L'\0');

            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wideText[0], size);

            SetWindowTextW(status, wideText.c_str());
        }
    }
}


std::string chat::SendChatMessage(chatData & data, const std::string & userText) {
    if (!data.model || !data.ctx) {
        return "No model loaded.";
    }

    return GenerateResponse(data, userText);
}


std::string chat::GenerateResponse(chatData & data, const std::string & userText) {
    const llama_vocab * vocab = llama_model_get_vocab(data.model);

    llama_chat_message message{ "user", userText.c_str() };

    const char * tmpl = llama_model_chat_template(data.model, nullptr);

    if (!tmpl) {
        return "Model does not provide a chat template.";
    }

    // Apply chat template
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

    // Tokenize
    int n_tokens = -llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), nullptr, 0, true, true);

    if (n_tokens <= 0) {
        return "Failed to tokenize prompt.";
    }

    std::vector<llama_token> tokens(n_tokens);

    int result =
        llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(), n_tokens, true, true);

    if (result < 0) {
        return "Failed to tokenize prompt.";
    }

    tokens.resize(result);

    // Create sampler
    auto samplerParams = llama_sampler_chain_default_params();

    llama_sampler * sampler = llama_sampler_chain_init(samplerParams);

    if (!sampler) {
        return "Failed to create sampler.";
    }

    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));

    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.95f, 1));

    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));

    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    // Send prompt to model
    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

    std::string response;

    while (true) {

        // Evaluate batch
        int decodeResult = llama_decode(data.ctx, batch);

        // Sample next token
        llama_token token = llama_sampler_sample(sampler, data.ctx, -1);

        // End of generation
        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        // Convert token to text
        char buffer[256];

        int n = llama_token_to_piece(vocab, token, buffer, sizeof(buffer), 0, true);

        if (n < 0) {
            response = "Failed to convert token.";
            break;
        }

        response.append(buffer, n);

        // Next token
        batch = llama_batch_get_one(&token, 1);
    }

    llama_sampler_free(sampler);

    return response;
}
