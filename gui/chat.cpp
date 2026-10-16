#include "chat.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

constexpr uint32_t kContextSize    = 8192;
constexpr uint32_t kBatchSize      = 2048;
constexpr uint32_t kMicroBatchSize = 1024;

constexpr std::size_t kMinimumOutputTokens = 512;
constexpr std::size_t kMaxNewTokens        = 1024;

static chatData g_chatData;

chatData & chat::GetChatData() {
    return g_chatData;
}

void FreeSampler(chatData & data) {
    if (data.sampler) {
        llama_sampler_free(data.sampler);
        data.sampler = nullptr;
    }
}

void ClearContext(chatData & data) {
    if (data.ctx) {
        llama_memory_clear(llama_get_memory(data.ctx), true);
    }
}

std::string FailNewTurn(chatData & data, const std::string & message) {
    fprintf(stderr, "[chat] %s\n", message.c_str());

    FreeSampler(data);

    data.isGenerating     = false;
    data.generationFailed = true;
    data.response         = message;

    if (!data.history.empty() && data.history.back().role == "user") {
        data.history.pop_back();
    }

    data.batch = {};
    data.promptTokens.clear();

    ClearContext(data);

    return data.response;
}

bool FormatHistory(chatData & data, const char * tmpl, std::string & prompt, std::string & error) {
    std::vector<llama_chat_message> messages;
    messages.reserve(data.history.size());

    for (const ChatMessage & message : data.history) {
        messages.push_back({ message.role.c_str(), message.content.c_str() });
    }

    // First pass: determine the required output size.
    const int32_t required = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, nullptr, 0);

    if (required < 0) {
        error = "Failed to apply the chat template.";
        return false;
    }

    std::vector<char> formatted(static_cast<std::size_t>(required) + 1);

    const int32_t actual = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(),
                                                     static_cast<int32_t>(formatted.size()));

    if (actual < 0 || static_cast<std::size_t>(actual) > formatted.size()) {
        error = "Failed to format the conversation.";
        return false;
    }

    prompt.assign(formatted.data(), static_cast<std::size_t>(actual));

    return true;
}

bool InitializeModel(chatData & data, const char * path, void (*onStatusChanged)(std::string)) {
    if (!path || path[0] == '\0') {
        if (onStatusChanged) {
            onStatusChanged("Invalid model path.");
        }

        return false;
    }

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = -1;

    llama_model * model = llama_model_load_from_file(path, modelParams);

    if (!model) {
        fprintf(stderr, "[chat] Failed to load model: %s\n", path);

        if (onStatusChanged) {
            onStatusChanged("Failed to load model.");
        }

        return false;
    }

    const llama_vocab * vocab = llama_model_get_vocab(model);

    if (!vocab) {
        llama_model_free(model);

        if (onStatusChanged) {
            onStatusChanged("Failed to get model vocabulary.");
        }

        return false;
    }

    llama_context_params ctxParams = llama_context_default_params();

    ctxParams.n_ctx    = kContextSize;
    ctxParams.n_batch  = kBatchSize;
    ctxParams.n_ubatch = kMicroBatchSize;

    llama_context * ctx = llama_init_from_model(model, ctxParams);

    if (!ctx) {
        llama_model_free(model);

        fprintf(stderr,
                "[chat] Failed to create context "
                "(context=%u, batch=%u, micro-batch=%u).\n",
                kContextSize, kBatchSize, kMicroBatchSize);

        if (onStatusChanged) {
            onStatusChanged("Failed to create context.");
        }

        return false;
    }

    data.model        = model;
    data.vocab        = vocab;
    data.ctx          = ctx;
    data.isGenerating = false;

    return true;
}

std::string GetBackendInfo() {
    std::string result;

    const size_t count = ggml_backend_dev_count();

    for (size_t i = 0; i < count; ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);

        const char * name = ggml_backend_dev_name(dev);

        const char * description = ggml_backend_dev_description(dev);

        if (name) {
            result += name;
        }

        if (name && description) {
            result += " - ";
        }

        if (description) {
            result += description;
        }

        result += '\n';
    }

    return result;
}

std::vector<llama_token> Tokenize(chatData & data, const std::string & text) {
    if (!data.vocab) {
        fprintf(stderr, "[chat] Cannot tokenize: vocabulary is null.\n");

        return {};
    }

    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
        fprintf(stderr, "[chat] Text exceeds the tokenizer size limit.\n");

        return {};
    }

    const int32_t textLength = static_cast<int32_t>(text.size());

    const int32_t required = llama_tokenize(data.vocab, text.c_str(), textLength, nullptr, 0, true, true);

    if (required == std::numeric_limits<int32_t>::min()) {
        fprintf(stderr, "[chat] Token count overflow.\n");

        return {};
    }

    if (required >= 0) {
        if (required == 0) {
            return {};
        }

        fprintf(stderr, "[chat] Failed to determine token count: %d\n", required);

        return {};
    }

    std::vector<llama_token> tokens(static_cast<std::size_t>(-required));

    const int32_t actual = llama_tokenize(data.vocab, text.c_str(), textLength, tokens.data(),
                                          static_cast<int32_t>(tokens.size()), true, true);

    if (actual < 0) {
        fprintf(stderr, "[chat] Tokenization failed: %d\n", actual);

        return {};
    }

    tokens.resize(static_cast<std::size_t>(actual));

    return tokens;
}

void CleanUpPrevious(chatData & data) {
    data.isGenerating     = false;
    data.generationFailed = false;

    FreeSampler(data);

    // Free the context before freeing its model.
    if (data.ctx) {
        llama_free(data.ctx);
        data.ctx = nullptr;
    }

    if (data.model) {
        llama_model_free(data.model);
        data.model = nullptr;
    }

    // Vocab is owned by the model.
    data.vocab = nullptr;

    data.batch = {};
    data.promptTokens.clear();
    data.history.clear();
    data.response.clear();

    data.generatedTokens = 0;
    data.maxNewTokens    = 0;
}

// Load a model.

bool chat::LoadModel(chatData & data, void (*onStatusChanged)(std::string), const char * path) {
    CleanUpPrevious(data);

    llama_backend_init();

    const std::string backendInfo = GetBackendInfo();
    
    if (!InitializeModel(data, path, onStatusChanged)) {
        return false;
    }

    fprintf(stderr,
            "[chat] Model loaded. Context=%u, batch=%u, "
            "micro-batch=%u\n",
            llama_n_ctx(data.ctx), kBatchSize, kMicroBatchSize);

    if (onStatusChanged) {
        onStatusChanged("Model loaded:\n" + backendInfo);
    }

    return true;
}

// Public entry point.
// Call this once for a new user message, then continue calling it
// while data.isGenerating is true to retrieve subsequent tokens.

std::string chat::SendChatMessage(chatData & data, const std::string & userText, void (*incompleteResponse)()) {
    if (!data.model || !data.ctx) {
        return "No model loaded.";
    }

    return GenerateResponse(data, userText, incompleteResponse);
}

// Decode the current batch and generate one token.

bool chat::GenerateNextToken(chatData & data) {
    if (!data.model || !data.ctx || !data.vocab || !data.sampler) {
        fprintf(stderr, "[chat] Generation failed: a required resource is null.\n");

        data.generationFailed = true;
        data.isGenerating     = false;

        if (data.response.empty()) {
            data.response = "Model resources are unavailable.";
        }

        return false;
    }

    if (data.batch.n_tokens <= 0) {
        fprintf(stderr, "[chat] Generation failed: empty batch.\n");

        data.generationFailed = true;
        data.isGenerating     = false;

        if (data.response.empty()) {
            data.response = "Cannot decode an empty batch.";
        }

        return false;
    }

    const llama_pos maxPosition = llama_memory_seq_pos_max(llama_get_memory(data.ctx), 0);

    const uint64_t usedTokens = maxPosition < 0 ? 0 : static_cast<uint64_t>(maxPosition) + 1;

    const uint64_t contextSize = llama_n_ctx(data.ctx);

    const uint64_t batchTokens = static_cast<uint64_t>(data.batch.n_tokens);

    if (usedTokens + batchTokens > contextSize) {
        fprintf(stderr,
                "[chat] Context limit exceeded: used=%llu, "
                "batch=%llu, context=%llu\n",
                static_cast<unsigned long long>(usedTokens), static_cast<unsigned long long>(batchTokens),
                static_cast<unsigned long long>(contextSize));

        data.generationFailed = true;
        data.isGenerating     = false;

        if (data.response.empty()) {
            data.response = "Context limit exceeded.";
        }

        return false;
    }

    ++data.timeCalled;

    const int decodeResult = llama_decode(data.ctx, data.batch);

    // Do not sample after a failed or warning decode result.
    if (decodeResult != 0) {
        fprintf(stderr,
                "[chat] llama_decode returned %d. "
                "Batch tokens=%d, used=%llu, context=%llu\n",
                decodeResult, data.batch.n_tokens, static_cast<unsigned long long>(usedTokens),
                static_cast<unsigned long long>(contextSize));

        data.generationFailed = true;
        data.isGenerating     = false;

        if (data.response.empty()) {
            data.response = "Failed to decode batch. Return code: " + std::to_string(decodeResult);
        }

        return false;
    }

    const llama_token token = llama_sampler_sample(data.sampler, data.ctx, -1);

    if (token < 0) {
        fprintf(stderr, "[chat] Sampler returned an invalid token: %d\n", static_cast<int>(token));

        data.generationFailed = true;
        data.isGenerating     = false;

        if (data.response.empty()) {
            data.response = "Failed to sample the next token.";
        }

        return false;
    }

    // Normal end of generation.
    if (llama_vocab_is_eog(data.vocab, token)) {
        data.isGenerating = false;
        return false;
    }

    // Convert the token into text.
    std::vector<char> buffer(256);

    int32_t n = llama_token_to_piece(data.vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, true);

    if (n < 0) {
        if (n == std::numeric_limits<int32_t>::min()) {
            data.generationFailed = true;
            data.isGenerating     = false;
            return false;
        }

        const std::size_t requiredSize = static_cast<std::size_t>(-n);

        buffer.resize(requiredSize);

        n = llama_token_to_piece(data.vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, true);
    }

    if (n < 0) {
        fprintf(stderr, "[chat] Token conversion failed: %d\n", n);

        data.generationFailed = true;
        data.isGenerating     = false;

        if (data.response.empty()) {
            data.response = "Failed to convert generated token.";
        }

        return false;
    }

    data.response.append(buffer.data(), static_cast<std::size_t>(n));

    ++data.generatedTokens;

    // Stop before attempting to use more than the allowed context.
    if (data.generatedTokens >= data.maxNewTokens) {
        data.isGenerating = false;
        return false;
    }

    // The next token is stored as a chatData member so that its
    // pointer remains valid across calls.
    data.next_token = token;

    data.batch = llama_batch_get_one(&data.next_token, 1);

    return true;
}

// Build a prompt from the full conversation history, then generate
// one token. Subsequent calls continue the same generation.

std::string chat::GenerateResponse(chatData & data, const std::string & userText, void (*incompleteResponse)()) {
    if (!data.isGenerating) {
        if (!data.model || !data.ctx) {
            return "No model loaded.";
        }

        data.response.clear();
        data.generationFailed = false;
        data.generatedTokens  = 0;
        data.maxNewTokens     = 0;

        FreeSampler(data);

        // Every request rebuilds its prompt from history.
        // Therefore, clear the previous KV cache before prefill.
        ClearContext(data);

        if (!data.vocab) {
            data.vocab = llama_model_get_vocab(data.model);
        }

        if (!data.vocab) {
            return "Failed to get model vocabulary.";
        }

        const char * tmpl = llama_model_chat_template(data.model, nullptr);

        if (!tmpl) {
            return "Model does not provide a chat template.";
        }

        // Add the new user message exactly once.
        data.history.push_back({ "user", userText });

        std::string prompt;
        std::string formatError;

        const uint32_t contextSize = llama_n_ctx(data.ctx);

        // Keep the oldest complete exchanges only as long as the
        // formatted conversation still fits within the context.
        for (;;) {
            if (!FormatHistory(data, tmpl, prompt, formatError)) {
                return FailNewTurn(data, formatError);
            }

            data.promptTokens = Tokenize(data, prompt);

            if (data.promptTokens.empty()) {
                return FailNewTurn(data, "Failed to tokenize the conversation.");
            }

            fprintf(stderr, "[chat] Prompt bytes=%zu, tokens=%zu, context=%u\n", prompt.size(),
                    data.promptTokens.size(), contextSize);

            if (data.promptTokens.size() + kMinimumOutputTokens <= contextSize) {
                break;
            }

            // History normally consists of user/assistant pairs.
            // Remove the oldest pair, leaving the latest request.
            if (data.history.size() >= 3 && data.history[0].role == "user" && data.history[1].role == "assistant") {
                data.history.erase(data.history.begin(), data.history.begin() + 2);

                continue;
            }

            return FailNewTurn(data,
                               "The current message is too long for the context. "
                               "Shorten the message or increase the context size.");
        }

        const std::size_t availableTokens = contextSize - data.promptTokens.size();

        data.maxNewTokens = std::min(kMaxNewTokens, availableTokens);

        // Create a sampler for this response.
        data.sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());

        if (!data.sampler) {
            return FailNewTurn(data, "Failed to create sampler.");
        }

        llama_sampler_chain_add(data.sampler, llama_sampler_init_top_k(40));

        llama_sampler_chain_add(data.sampler, llama_sampler_init_top_p(0.95f, 1));

        llama_sampler_chain_add(data.sampler, llama_sampler_init_temp(1.0f));

        llama_sampler_chain_add(data.sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

        // Prefill large prompts in chunks. promptTokens is a member
        // of chatData, so its storage remains valid across calls.
        std::size_t offset = 0;

        while (data.promptTokens.size() - offset > kBatchSize) {
            llama_batch chunk =
                llama_batch_get_one(data.promptTokens.data() + offset, static_cast<int32_t>(kBatchSize));

            const int rc = llama_decode(data.ctx, chunk);

            if (rc != 0) {
                fprintf(stderr,
                        "[chat] Prompt prefill failed: return=%d, "
                        "offset=%zu, total_tokens=%zu\n",
                        rc, offset, data.promptTokens.size());

                return FailNewTurn(data,
                                   "Failed to decode prompt chunk. "
                                   "Check the application logs.");
            }

            offset += kBatchSize;
        }

        // The final chunk is decoded by GenerateNextToken(),
        // which also samples the first generated token.
        const std::size_t remaining = data.promptTokens.size() - offset;

        data.batch = llama_batch_get_one(data.promptTokens.data() + offset, static_cast<int32_t>(remaining));

        data.isGenerating = true;
    }

    // one generated token per call.
    if (data.isGenerating) {
        GenerateNextToken(data);

        if (!data.isGenerating) {
            FreeSampler(data);

            if (!data.generationFailed) {
                // Commit the assistant response once generation ends.
                data.history.push_back({ "assistant", data.response });
            } else {
                // A failed turn is not added to conversation history.
                if (!data.history.empty() && data.history.back().role == "user") {
                    data.history.pop_back();
                }
            }

            // Remove references to prompt memory before releasing it.
            data.batch = {};
            data.promptTokens.clear();
        } else if (incompleteResponse) {
            incompleteResponse();
        }
    }

    return data.response;
}
