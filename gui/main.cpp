#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "llama.h"

#include <windows.h>

#include <commdlg.h>

#include <string>
#include <vector>

static HWND g_window      = nullptr;
static HWND g_status      = nullptr;
static HWND g_inputText   = nullptr;
static HWND g_textInModel = nullptr;

static llama_model *   g_model = nullptr;
static llama_context * g_ctx   = nullptr;

void SetStatus(const wchar_t * text) {
    if (g_status) {
        SetWindowTextW(g_status, text);
    }
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

std::vector<llama_token> Tokenize(const std::string & text) {
    const llama_vocab * vocab = llama_model_get_vocab(g_model);

    int n_tokens = -llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()), nullptr, 0, true, true);

    if (n_tokens <= 0) {
        return {};
    }

    std::vector<llama_token> tokens(n_tokens);

    int result =
        llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()), tokens.data(), n_tokens, true, true);

    if (result < 0) {
        return {};
    }

    tokens.resize(result);

    return tokens;
}

std::string GenerateResponse(const std::string & userText) {
    if (!g_model || !g_ctx) {
        return "No model loaded.";
    }

    const llama_vocab * vocab = llama_model_get_vocab(g_model);

    //
    // Build chat message
    //
    llama_chat_message message{ "user", userText.c_str() };

    //
    // Get the chat template embedded in the model.
    //
    const char * tmpl = llama_model_chat_template(g_model, nullptr);

    if (!tmpl) {
        return "Model does not provide a chat template.";
    }

    //
    // Format:
    //
    // user message -> model-specific prompt
    //
    int promptSize = llama_chat_apply_template(tmpl, &message, 1, true, nullptr, 0);

    if (promptSize < 0) {
        return "Failed to apply chat template.";
    }

    std::vector<char> formatted(promptSize + 1);

    int formattedSize =
        llama_chat_apply_template(tmpl, &message, 1, true, formatted.data(), static_cast<int32_t>(formatted.size()));

    if (formattedSize < 0) {
        return "Failed to format prompt.";
    }

    std::string prompt(formatted.data(), formattedSize);

    //
    // Tokenize prompt
    //
    std::vector<llama_token> tokens = Tokenize(prompt);

    if (tokens.empty()) {
        return "Failed to tokenize prompt.";
    }

    //
    // Create sampler
    //
    auto samplerParams = llama_sampler_chain_default_params();

    llama_sampler * sampler = llama_sampler_chain_init(samplerParams);

    if (!sampler) {
        return "Failed to create sampler.";
    }

    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));

    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.95f, 1));

    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));

    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    //
    // Send prompt to the model.
    //
    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

    std::string response;

    while (true) {
        //
        // Evaluate the current batch.
        //
        int result = llama_decode(g_ctx, batch);
        if (result != 0) {
            response = "llama_decode() failed.";
            break;
        }

        llama_token token = llama_sampler_sample(sampler, g_ctx, -1);

        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        char buffer[256];

        int n = llama_token_to_piece(vocab, token, buffer, sizeof(buffer), 0, true);

        if (n < 0) {
            response = "Failed to convert token.";
            break;
        }

        response.append(buffer, n);

        batch = llama_batch_get_one(&token, 1);
    }

    llama_sampler_free(sampler);

    return response;
}

bool LoadModel(const char * path) {
    if (g_ctx) {
        llama_free(g_ctx);
        g_ctx = nullptr;
    }

    if (g_model) {
        llama_model_free(g_model);
        g_model = nullptr;
    }

    llama_backend_init();

    llama_model_params model_params = llama_model_default_params();

    g_model = llama_model_load_from_file(path, model_params);

    if (!g_model) {
        SetStatus(L"Failed to load model");
        return false;
    }

    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.n_ctx = 4096;

    g_ctx = llama_init_from_model(g_model, ctx_params);

    if (!g_ctx) {
        llama_model_free(g_model);
        g_model = nullptr;

        SetStatus(L"Failed to create context");
        return false;
    }

    SetStatus(L"Model loaded");

    return true;
}

void SendChat() {
    if (!g_model || !g_ctx) {
        MessageBoxW(g_window, L"Please load a model first.", L"Llama.cpp GUI", MB_OK | MB_ICONWARNING);

        return;
    }

    wchar_t text[4096] = {};

    GetWindowTextW(g_inputText, text, _countof(text));

    if (text[0] == L'\0') {
        return;
    }

    std::string input = WideToUtf8(text);

    SetStatus(L"Generating...");

    std::string response = GenerateResponse(input);

    //
    // Convert UTF-8 response back to UTF-16
    //
    int wideSize = MultiByteToWideChar(CP_UTF8, 0, response.data(), static_cast<int>(response.size()), nullptr, 0);

    if (wideSize <= 0) {
        SetStatus(L"Generation failed");
        return;
    }

    std::wstring wideResponse(wideSize, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, response.data(), static_cast<int>(response.size()), &wideResponse[0], wideSize);

    SetWindowTextW(g_textInModel, wideResponse.c_str());

    SetStatus(L"Ready");
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

    LoadModel(path);
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
                if (g_ctx) {
                    llama_free(g_ctx);
                    g_ctx = nullptr;
                }

                if (g_model) {
                    llama_model_free(g_model);
                    g_model = nullptr;
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
