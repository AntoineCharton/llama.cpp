#pragma once
#include "windowsGUI.h"

#include "chat.h"
#include "markdown.h"

#include <richedit.h>
#include <Windows.h>

#include <string>

HMODULE hRichEdit = LoadLibraryW(L"Msftedit.dll");

struct RtfStream {
    const char * data;
    size_t       size;
    size_t       position = 0;
};

const char * BrowseForModelWithExplorer(HWND window) {
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
}

static DWORD CALLBACK ReadRtf(DWORD_PTR cookie, LPBYTE buffer, LONG requested, LONG * received) {
    auto * stream = reinterpret_cast<RtfStream *>(cookie);

    size_t remaining = stream->size - stream->position;
    size_t amount    = (remaining < static_cast<size_t>(requested)) ? remaining : static_cast<size_t>(requested);

    if (amount > 0) {
        memcpy(buffer, stream->data + stream->position, amount);
        stream->position += amount;
    }

    *received = static_cast<LONG>(amount);
    return 0;
}

void windowsGUI::SetMarkdown(HWND edit, std::string_view markdown) {
    std::string rtf = markdown::MarkdownToRtf(markdown);

    RtfStream stream{ rtf.data(), rtf.size() };

    EDITSTREAM es{};
    es.dwCookie    = reinterpret_cast<DWORD_PTR>(&stream);
    es.pfnCallback = ReadRtf;

    SendMessageW(edit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
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

std::string windowsGUI::GetInputText(chatData chatData, HWND window, HWND inputText) {
    if (!chatData.ctx) {
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

void SendChat(GUIHandles & gui, chatData & chatData) {
    std::string input = windowsGUI::GetInputText(chatData, gui.window, gui.inputText);

    if (input.empty()) {
        return;
    }

    chat::SetStatus("Generating...", gui.status);

    std::string response = chat::SendChatMessage(chatData, input);

    if (response.empty()) {
        chat::SetStatus("Generation failed", gui.status);
        return;
    }

    windowsGUI::SetMarkdown(gui.textInModel, response);

    chat::SetStatus("Ready", gui.status);
}

void BrowseForModel(GUIHandles & gui, chatData & chatData) {
    const char * path = BrowseForModelWithExplorer(gui.window);

    if (path != nullptr) {
        chat::LoadModel(chatData, gui.window, gui.status, path);
    }
}

LRESULT CALLBACK windowsGUI::CALLBACKWindowProc(GUIHandles & g_gui,
                                                chatData &   g_chatdata,
                                                HWND         hwnd,
                                                UINT         uMsg,
                                                WPARAM       wParam,
                                                LPARAM       lParam) {
    switch (uMsg) {
        case WM_CREATE:
            {
                g_gui.window = hwnd;

                g_gui.textInModel = CreateWindowExW(
                    0, MSFTEDIT_CLASS, L"",
                    WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY, 20, 0,
                    800, 500, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

                windowsGUI::SetMarkdown(g_gui.textInModel, "# Load your model to get started");

                g_gui.inputText =
                    CreateWindowW(L"EDIT", L"No model loaded",
                                  WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 20,
                                  500, 800, 50, hwnd, nullptr, nullptr, nullptr);

                CreateWindowW(L"BUTTON", L"Submit", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, 550, 180, 20, hwnd,
                              (HMENU) 1002, nullptr, nullptr);

                CreateWindowW(L"BUTTON", L"Load GGUF Model", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, 570, 180, 20,
                              hwnd, (HMENU) 1001, nullptr, nullptr);

                g_gui.status = CreateWindowW(L"STATIC", L"No model loaded", WS_VISIBLE | WS_CHILD, 20, 590, 500, 100,
                                             hwnd, nullptr, nullptr, nullptr);

                return 0;
            }

        case WM_COMMAND:
            {
                if (LOWORD(wParam) == 1001) {
                    BrowseForModel(g_gui, g_chatdata);
                    return 0;
                }

                if (LOWORD(wParam) == 1002) {
                    SendChat(g_gui, g_chatdata);
                    return 0;
                }

                break;
            }

        case WM_DESTROY:
            {
                if (g_chatdata.ctx) {
                    llama_free(g_chatdata.ctx);
                    g_chatdata.ctx = nullptr;
                }

                if (g_chatdata.model) {
                    llama_model_free(g_chatdata.model);
                    g_chatdata.model = nullptr;
                }

                llama_backend_free();

                PostQuitMessage(0);

                return 0;
            }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
