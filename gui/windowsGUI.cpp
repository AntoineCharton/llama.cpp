#pragma once
#include "windowsGUI.h"
#include <Windows.h>
#include "chat.h"
#include <string>

const char * windowsGUI::BrowseForModel(HWND window) {
    wchar_t filename[MAX_PATH] = {};

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = window;
    dialog.lpstrFilter =
        L"GGUF Models (*.gguf)\0*.gguf\0"
        L"All Files (*.*)\0*.*\0";
    dialog.lpstrFile  = filename;
    dialog.nMaxFile   = MAX_PATH;
    dialog.Flags      = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = L"Select GGUF Model";

    static char path[MAX_PATH] = {};

     if (!GetOpenFileNameW(&dialog)) {
        return nullptr;
    }

    WideCharToMultiByte(CP_UTF8, 0, filename, -1, path, MAX_PATH, nullptr, nullptr);

    return path;
    //chat::LoadModel(g_chatData, g_window, g_status, path);
}

std::string WideToUtf8(const wchar_t * text) {
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


std::string windowsGUI::GetInputText(chatData chatdata, HWND window, HWND inputText) {
    if (!chatdata.ctx) {
        MessageBoxW(window, L"Please load a model first.", L"Llama.cpp GUI", MB_OK | MB_ICONWARNING);
        return "";
    }

    wchar_t text[4096] = {};
    GetWindowTextW(inputText, text, _countof(text));

    if (text[0] == L'\0') {
        return "";
    }
    return WideToUtf8(text);
}
