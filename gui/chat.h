#pragma once

#include "llama.h"
#include <Windows.h>
#include <string>

struct chatData {
    llama_model *   model = nullptr;
    llama_context * ctx   = nullptr;
};

class chat {
  public:
    static std::string SendChatMessage(chatData & data, const std::string & userText);
    static bool LoadModel(chatData & data, HWND window, HWND status, const char * path);
    static void SetStatus(const wchar_t * text, HWND status);
    static std::string WideToUtf8(const wchar_t * text);

  private:
    static std::string GenerateResponse(chatData & data, const std::string & userText);
};
