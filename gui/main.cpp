#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "chat.h"
#include "llama.h"

#include <windows.h>

#include <commdlg.h>

#include <string>
#include <vector>

static HWND g_window      = nullptr;
static HWND g_status      = nullptr;
static HWND g_inputText   = nullptr;
static HWND g_textInModel = nullptr;

static chatData g_chatData;

void SendChat() {
    if (!g_chatData.ctx) {
        MessageBoxW(g_window, L"Please load a model first.", L"Llama.cpp GUI", MB_OK | MB_ICONWARNING);
        return;
    }

    wchar_t text[4096] = {};

    GetWindowTextW(g_inputText, text, _countof(text));

    if (text[0] == L'\0') {
        return;
    }

    std::string input = chat::WideToUtf8(text);

    chat::SetStatus(L"Generating...", g_status);

    std::string response = chat::SendChatMessage(g_chatData, input);

    int wideSize = MultiByteToWideChar(CP_UTF8, 0, response.data(), static_cast<int>(response.size()), nullptr, 0);

    if (wideSize <= 0) {
        chat::SetStatus(L"Generation failed", g_status);
        return;
    }

    std::wstring wideResponse(wideSize, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, response.data(), static_cast<int>(response.size()), &wideResponse[0], wideSize);

    SetWindowTextW(g_textInModel, wideResponse.c_str());

    chat::SetStatus(L"Ready", g_status);
}

void BrowseForModel() {
    wchar_t filename[MAX_PATH] = {};

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = g_window;
    dialog.lpstrFilter =
        L"GGUF Models (*.gguf)\0*.gguf\0"
        L"All Files (*.*)\0*.*\0";
    dialog.lpstrFile  = filename;
    dialog.nMaxFile   = MAX_PATH;
    dialog.Flags      = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = L"Select GGUF Model";

    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    char path[MAX_PATH] = {};

    WideCharToMultiByte(CP_UTF8, 0, filename, -1, path, MAX_PATH, nullptr, nullptr);

    chat::LoadModel(g_chatData, g_window, g_status, path);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            {
                g_window = hwnd;

                CreateWindowW(L"BUTTON", L"Load GGUF Model", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, 20, 180, 35,
                              hwnd, (HMENU) 1001, nullptr, nullptr);

                g_status = CreateWindowW(L"STATIC", L"No model loaded", WS_VISIBLE | WS_CHILD, 20, 70, 500, 30, hwnd,
                                         nullptr, nullptr, nullptr);

                CreateWindowW(L"BUTTON", L"Send", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, 320, 180, 35,
                              hwnd, (HMENU) 1002, nullptr, nullptr);

                g_inputText =
                    CreateWindowW(L"EDIT", L"No model loaded",
                                  WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 20,
                                  120, 500, 100, hwnd, nullptr, nullptr, nullptr);

                g_textInModel = CreateWindowW(
                    L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY, 20,
                    370, 800, 250, hwnd, nullptr, nullptr, nullptr);

                return 0;
            }

        case WM_COMMAND:
            {
                if (LOWORD(wParam) == 1001) {
                    BrowseForModel();
                    return 0;
                }

                 if (LOWORD(wParam) == 1002) {
                    SendChat();
                    return 0;
                }

                break;
            }

        case WM_DESTROY:
            {
                if (g_chatData.ctx) {
                    llama_free(g_chatData.ctx);
                    g_chatData.ctx = nullptr;
                }

                if (g_chatData.model) {
                    llama_model_free(g_chatData.model);
                    g_chatData.model = nullptr;
                }

                llama_backend_free();

                PostQuitMessage(0);

                return 0;
            }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"LlamaCppGUI";

    WNDCLASSW wc{};

    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;

    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));

    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"Failed to register window class.", L"Llama.cpp GUI", MB_OK | MB_ICONERROR);

        return 1;
    }

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Llama.cpp GUI", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900,
                                700, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"Llama.cpp GUI", MB_OK | MB_ICONERROR);

        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};

    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
