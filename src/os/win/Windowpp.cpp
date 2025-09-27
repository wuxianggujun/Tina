//
// Created by wuxianggujun on 25-9-27.
//

#include "Windowpp.hpp"

namespace Tina::OS
{
    std::wstring Utf::toWide(const char* str, u32 len)
    {
        if (!str || len == 0) return {};
        return Core::StringUtils::utf8ToWstring(std::string_view(str, len));
    }

    std::wstring Utf::toWide(const std::string& str)
    {
        return Core::StringUtils::utf8ToWstring(str);
    }

    std::string Utf::toUtf8(const wchar_t* str)
    {
        if (!str) return {};
        return Core::StringUtils::wstringToUtf8(str);
    }
}
