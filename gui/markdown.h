#pragma once
#include <string>
#include <string_view>

class markdown {
  public:
    static std::string markdown::MarkdownToRtf(std::string_view markdown);
};
