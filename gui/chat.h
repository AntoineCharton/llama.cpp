#pragma once
#include "llama.h"
#include <string>
#include <vector>

struct chatData {
    llama_model *       model      = nullptr;
    llama_context *     ctx        = nullptr;
    llama_sampler *     sampler    = nullptr;
    const llama_vocab * vocab      = nullptr;
    llama_batch         batch      = {};
    std::vector<llama_token> tokens;
    llama_token         next_token = 0;
    std::string         response;
};

class chat {
  public:
    static std::string SendChatMessage(chatData & data, const std::string & userText);
    static bool        LoadModel(chatData & data, void (*status)(std::string),const char * path);
    static chatData&   GetChatData();

  private:
    static std::string GenerateResponse(chatData & data, const std::string & userText);
    static bool        GenerateNextToken(chatData & data);
};
