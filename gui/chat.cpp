#include "chat.h"

#include <vector>

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

    //
    // Apply chat template
    //
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

    //
    // Tokenize
    //
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

    //
    // Create sampler
    //
    auto samplerParams = llama_sampler_chain_default_params();

    llama_sampler * sampler = llama_sampler_chain_init(samplerParams);

    if (!sampler) {
        return "Failed to create sampler.";
    }

    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));

    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.95f, 1));

    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));

    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    //
    // Send prompt to model
    //
    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

    std::string response;

    while (true) {
        //
        // Evaluate batch
        //
        int decodeResult = llama_decode(data.ctx, batch);

        if (decodeResult != 0) {
            response = "llama_decode() failed.";
            break;
        }

        //
        // Sample next token
        //
        llama_token token = llama_sampler_sample(sampler, data.ctx, -1);

        //
        // End of generation
        //
        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        //
        // Convert token to text
        //
        char buffer[256];

        int n = llama_token_to_piece(vocab, token, buffer, sizeof(buffer), 0, true);

        if (n < 0) {
            response = "Failed to convert token.";
            break;
        }

        response.append(buffer, n);

        //
        // Next token
        //
        batch = llama_batch_get_one(&token, 1);
    }

    llama_sampler_free(sampler);

    return response;
}
