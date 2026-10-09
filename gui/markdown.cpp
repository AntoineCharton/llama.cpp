#include "markdown.h"

#include <richedit.h>
#include <string_view>
#include <cstdint>
#include <string>
#include "md4c.h"

struct RtfStream {
    const char * data;
    size_t       size;
    size_t       position = 0;
};

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

static void AppendRtfText(std::string & out, const char * text, size_t len) {
    // MD4C's default input encoding is UTF-8.
    if (len == 0) {
        return;
    }

    int count = MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(len), nullptr, 0);

    if (count <= 0) {
        return;
    }

    std::wstring wide(count, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(len), wide.data(), count);

    for (wchar_t ch : wide) {
        switch (ch) {
            case L'\\':
                out += "\\\\";
                break;
            case L'{':
                out += "\\{";
                break;
            case L'}':
                out += "\\}";
                break;
            case L'\r':
                break;
            case L'\n':
                out += "\\line ";
                break;
            default:
                if (ch >= 0x20 && ch <= 0x7e) {
                    out += static_cast<char>(ch);
                } else {
                    // RTF Unicode escapes use signed 16-bit values.
                    short value = static_cast<short>(ch);
                    out += "\\u" + std::to_string(value) + "?";
                }
        }
    }
}

static int EnterBlock(MD_BLOCKTYPE type, void * detail, void * userdata) {
    auto & out = *static_cast<std::string *>(userdata);

    switch (type) {
        case MD_BLOCK_P:
            break;

        case MD_BLOCK_H:
            {
                auto * h    = static_cast<MD_BLOCK_H_DETAIL *>(detail);
                int    size = (h->level == 1) ? 36 : (h->level == 2) ? 30 : (h->level == 3) ? 26 : 22;

                out += "\\b\\fs" + std::to_string(size) + " ";
                break;
            }

        case MD_BLOCK_CODE:
            out += "\\par\\f1\\fs20 ";
            break;

        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            break;

        case MD_BLOCK_LI:
            out += "\\par\\li360\\fi-240\\bullet\\tab ";
            break;

        case MD_BLOCK_HR:
            out += "\\par\\brdrb\\brdrs\\brdrw10\\par ";
            break;

        default:
            break;
    }

    return 0;
}

static int LeaveBlock(MD_BLOCKTYPE type, void *, void * userdata) {
    auto & out = *static_cast<std::string *>(userdata);

    switch (type) {
        case MD_BLOCK_P:
            out += "\\par ";
            break;

        case MD_BLOCK_H:
            out += "\\b0\\fs22\\par ";
            break;

        case MD_BLOCK_CODE:
            out += "\\f0\\fs22\\par ";
            break;

        case MD_BLOCK_LI:
            out += "\\li0 ";
            break;

        default:
            break;
    }

    return 0;
}

static int EnterSpan(MD_SPANTYPE type, void *, void * userdata) {
    auto & out = *static_cast<std::string *>(userdata);

    switch (type) {
        case MD_SPAN_STRONG:
            out += "\\b ";
            break;
        case MD_SPAN_EM:
            out += "\\i ";
            break;
        case MD_SPAN_CODE:
            out += "\\f1\\highlight1 ";
            break;
        case MD_SPAN_DEL:
            out += "\\strike ";
            break;
        default:
            break;
    }

    return 0;
}

static int LeaveSpan(MD_SPANTYPE type, void *, void * userdata) {
    auto & out = *static_cast<std::string *>(userdata);

    switch (type) {
        case MD_SPAN_STRONG:
            out += "\\b0 ";
            break;
        case MD_SPAN_EM:
            out += "\\i0 ";
            break;
        case MD_SPAN_CODE:
            out += "\\highlight0\\f0 ";
            break;
        case MD_SPAN_DEL:
            out += "\\strike0 ";
            break;
        default:
            break;
    }

    return 0;
}

static int RenderText(MD_TEXTTYPE type, const MD_CHAR * text, MD_SIZE size, void * userdata) {
    auto & out = *static_cast<std::string *>(userdata);

    if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
        out += "\\line ";
    } else {
        AppendRtfText(out, text, size);
    }

    return 0;
}

static std::string MarkdownToRtf(std::string_view markdown) {
    std::string rtf =
        "{\\rtf1\\ansi\\deff0\\uc1"
        "{\\fonttbl{\\f0\\fnil Segoe UI;}"
        "{\\f1\\fmodern Consolas;}}"
        "{\\colortbl;\\red230\\green230\\blue230;}"
        "\\f0\\fs22 ";

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags       = MD_FLAG_STRIKETHROUGH;
    parser.enter_block = EnterBlock;
    parser.leave_block = LeaveBlock;
    parser.enter_span  = EnterSpan;
    parser.leave_span  = LeaveSpan;
    parser.text        = RenderText;

    if (markdown.size() <= UINT_MAX &&
        md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &rtf) == 0) {
        rtf += "}";
    } else {
        // Return a valid empty RTF document on parse failure.
        rtf =
            "{\\rtf1\\ansi\\deff0"
            "{\\fonttbl{\\f0 Segoe UI;}}}";
    }

    return rtf;
}

void markdown::SetMarkdown(HWND edit, std::string_view markdown) {
    std::string rtf = MarkdownToRtf(markdown);

    RtfStream stream{ rtf.data(), rtf.size() };

    EDITSTREAM es{};
    es.dwCookie    = reinterpret_cast<DWORD_PTR>(&stream);
    es.pfnCallback = ReadRtf;

    SendMessageW(edit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
}
