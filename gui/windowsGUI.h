#pragma once
#include "chat.h"

#include <Windows.h>

#include <string>

struct GUIHandles {
    HWND window      = nullptr;
    HWND status      = nullptr;
    HWND inputText   = nullptr;
    HWND textInModel = nullptr;
    HWND richEdit    = nullptr;
};

class windowsGUI {
  public:
    static std::string GetInputText(chatData chatdata, HWND window, HWND inputText);
    static void        SetMarkdown(HWND edit, std::string_view markdown);
    static LRESULT CALLBACK
    CALLBACKWindowProc(GUIHandles & g_gui, chatData & g_chatdata, HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
