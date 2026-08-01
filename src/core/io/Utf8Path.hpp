#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::Core::Detail {

[[nodiscard]] inline std::filesystem::path pathFromUtf8Bytes(std::string_view text)
{
    std::u8string utf8Path;
    utf8Path.reserve(text.size());
    for (const char byte : text)
    {
        utf8Path.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path{std::move(utf8Path)};
}

} // namespace Tina::Core::Detail
