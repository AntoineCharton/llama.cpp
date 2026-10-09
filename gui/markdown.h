#pragma once
#include <Windows.h>
#include <string_view>

class markdown {
  public:
    static void SetMarkdown(HWND edit, std::string_view markdown);
};
