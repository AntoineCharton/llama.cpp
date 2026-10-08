#pragma once

#include "llama.h"

#include <string>

struct chatData {
    llama_model *   model = nullptr;
    llama_context * ctx   = nullptr;
};

class chat {
  public:
    static std::string SendChatMessage(chatData & data, const std::string & userText);

  private:
    static std::string GenerateResponse(chatData & data, const std::string & userText);
};
