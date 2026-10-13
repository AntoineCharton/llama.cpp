#pragma once
#include <string_view>
#include <string>

class markdown {
  public:
    static std::string markdown::MarkdownToRtf(std::string_view markdown);
};
