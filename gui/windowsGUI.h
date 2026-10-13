#pragma once
#include <Windows.h>
#include "chat.h"
#include <string>

class windowsGUI {
  public:
    static const char * BrowseForModel(HWND window);
    static std::string  GetInputText(chatData chatdata, HWND window, HWND inputText);
    static void SetMarkdown(HWND edit, std::string_view markdown);
};
