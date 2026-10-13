#pragma once
#include "windowsGUI.h"
#include <Windows.h>

const char * Display::BrowseForModel(HWND window) {
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
