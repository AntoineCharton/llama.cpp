#pragma once
#include "llama.h"

#include <string>
#include <vector>

struct ChatMessage {
    std::string role;
    std::string content;
};

struct chatData {
    llama_model *            model   = nullptr;
    llama_context *          ctx     = nullptr;
    llama_sampler *          sampler = nullptr;
    const llama_vocab *      vocab   = nullptr;
    llama_batch              batch   = {};
    std::vector<llama_token> tokens;
    llama_token              next_token = 0;
    std::string              response;
    bool                     isGenerating;
    int                      timeCalled = 0;
    std::vector<ChatMessage> history;
    std::vector<llama_token> promptTokens;
    std::size_t generatedTokens  = 0;
    std::size_t maxNewTokens     = 0;
    bool        generationFailed = false;
};

class chat {
  public:
    static std::string SendChatMessage(chatData & data, const std::string & userText, void (*incompleteResponse)());
    static bool        LoadModel(chatData & data, void (*status)(std::string), const char * path);
    static chatData &  GetChatData();

  private:
    static std::string GenerateResponse(chatData & data, const std::string & userText, void (*incompleteResponse)()); 
    static bool        GenerateNextToken(chatData & data);
};
