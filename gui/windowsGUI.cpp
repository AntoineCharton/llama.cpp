#pragma once
#include "windowsGUI.h"

#include "chat.h"
#include "markdown.h"

#include <richedit.h>
#include <Windows.h>

#include <string>

HMODULE hRichEdit = LoadLibraryW(L"Msftedit.dll");

static GUIHandles g_gui;

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

void SetStatus(const std::string & text, HWND status) {
    if (!status) {
        return;
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

    if (size <= 0) {
        return;
    }

    std::wstring wideText(size, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wideText[0], size);

    SetWindowTextW(status, wideText.c_str());
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

void windowsGUI::SetMarkdown(HWND edit, const std::string& markdown) {
    std::string rtf = markdown::MarkdownToRtf(markdown);

    RtfStream stream{ rtf.data(), rtf.size() };

    EDITSTREAM es{};
    es.dwCookie    = reinterpret_cast<DWORD_PTR>(&stream);
    es.pfnCallback = ReadRtf;

    const bool wasVisible = IsWindowVisible(edit) != FALSE;

    if (wasVisible) {
        SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    }

    SendMessageW(edit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));

    if (wasVisible) {
        SendMessageW(edit, WM_SETREDRAW, TRUE, 0);

        RedrawWindow(edit, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    // Scroll after redraw has been re-enabled.
    SendMessageW(edit, WM_VSCROLL, SB_BOTTOM, 0);

    RedrawWindow(edit, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
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

    wchar_t text[16384] = {};
    GetWindowTextW(inputText, text, _countof(text));

    if (text[0] == L'\0') {
        return "";
    }
    return WideToUtf8(text);
}

void IncompleteResponse() {
    InvalidateRect(g_gui.window, nullptr, TRUE);
}

void SendChat(GUIHandles & gui, chatData & chatData) {
    std::string input = windowsGUI::GetInputText(chatData, gui.window, gui.inputText);

    if (input.empty() && !chatData.isGenerating) {
        return;
    }

    SetStatus("Generating...", gui.status);

    std::string response = chat::SendChatMessage(chatData, input, IncompleteResponse);

    if (response.empty()) {
        SetStatus("Generation failed", gui.status);
        return;
    }

    windowsGUI::SetMarkdown(gui.textInModel, response);

    SetStatus("Ready", gui.status);
}

void UpdateStatus(std::string status) {
    SetStatus(status, g_gui.status);
}

void BrowseForModel(GUIHandles & gui, chatData & chatData) {
    const char * path = BrowseForModelWithExplorer(gui.window);

    if (path != nullptr) {
        if (chat::LoadModel(chatData, UpdateStatus, path)) {
            SetStatus("", g_gui.inputText);
            ShowWindow(g_gui.submitButton, SW_SHOW);
        }
    }
}

WNDPROC OriginalInputProc = nullptr;

LRESULT CALLBACK InputProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendChat(g_gui, chat::GetChatData());
        SetStatus("", g_gui.inputText);
        return 0;
    }

    return CallWindowProcW(OriginalInputProc, hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK windowsGUI::CALLBACKWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            {
                g_gui.window = hwnd;

                g_gui.textInModel = CreateWindowExW(
                    0, MSFTEDIT_CLASS, L"",
                    WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY, 20, 0,
                    800, 400, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

                windowsGUI::SetMarkdown(g_gui.textInModel, "# Load your model to get started");

                g_gui.inputText =
                    CreateWindowW(L"EDIT", L"No model loaded",
                                  WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 20,
                                  400, 800, 150, hwnd, nullptr, nullptr, nullptr);

                g_gui.submitButton =
                    CreateWindowW(L"BUTTON", L"Submit", WS_CHILD | BS_PUSHBUTTON, 20, 550, 180, 20,
                                                   hwnd, (HMENU) 1002, nullptr, nullptr);

                CreateWindowW(L"BUTTON", L"Load GGUF Model", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, 570, 180, 20,
                              hwnd, (HMENU) 1001, nullptr, nullptr);

                g_gui.status = CreateWindowW(L"STATIC", L"No model loaded", WS_VISIBLE | WS_CHILD, 20, 590, 500, 100,
                                             hwnd, nullptr, nullptr, nullptr);

                OriginalInputProc = (WNDPROC) SetWindowLongPtrW(g_gui.inputText, GWLP_WNDPROC, (LONG_PTR) InputProc);

                return 0;
            }

        case WM_COMMAND:
            {
                if (LOWORD(wParam) == 1001) {
                    BrowseForModel(g_gui, chat::GetChatData());
                    return 0;
                }

                if (LOWORD(wParam) == 1002) {
                    SendChat(g_gui, chat::GetChatData());
                    SetStatus("", g_gui.inputText);
                    return 0;
                }

                break;
            }

        case WM_DESTROY:
            {
                if (chat::GetChatData().ctx) {
                    llama_free(chat::GetChatData().ctx);
                    chat::GetChatData().ctx = nullptr;
                }

                if (chat::GetChatData().model) {
                    llama_model_free(chat::GetChatData().model);
                    chat::GetChatData().model = nullptr;
                }

                llama_backend_free();

                PostQuitMessage(0);

                return 0;
            }
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC         hdc = BeginPaint(hwnd, &ps);

                EndPaint(hwnd, &ps);
                if (chat::GetChatData().isGenerating) {
                    SendChat(g_gui, chat::GetChatData());
                }
                return 0;
            }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
