//
// Created by wuxianggujun on 25-9-27.
//

#pragma once

#include <string>
#include <string_view>
#include <EASTL/vector.h>
#include <EASTL/iterator.h>
#include <stdexcept>
#include <cstdint>
#include <cwchar>
#include <climits>
#include <utf8.h>

namespace Tina::Core::StringUtils
{
    inline bool isValidUTF8(std::string_view str)
    {
        return utf8::is_valid(str.begin(), str.end());
    }

    inline std::string replaceInvalidUTF8(std::string_view str)
    {
        std::string out;
        utf8::replace_invalid(str.begin(), str.end(), std::back_inserter(out));
        return out;
    }

    inline std::size_t utf8CodePointCount(std::string_view s)
    {
        return static_cast<std::size_t>(utf8::distance(s.begin(), s.end()));
    }

    // UTF-8 -> UTF-16（抛异常：非法时抛 utfcpp 异常）
    inline std::u16string utf8ToUtf16(std::string_view s)
    {
        std::u16string out;
        utf8::utf8to16(s.begin(), s.end(), std::back_inserter(out));
        return out;
    }

    // UTF-16 -> UTF-8（抛异常）
    inline std::string utf16ToUtf8(const std::u16string& s)
    {
        std::string out;
        utf8::utf16to8(s.begin(), s.end(), std::back_inserter(out));
        return out;
    }

    inline std::string utf16ToUtf8(const char16_t* p, std::size_t len)
    {
        std::string out;
        utf8::utf16to8(p, p + len, std::back_inserter(out));
        return out;
    }

    // UTF-8 -> UTF-32（抛异常）
    inline std::u32string utf8ToUtf32(std::string_view s)
    {
        std::u32string out;
        utf8::utf8to32(s.begin(), s.end(), std::back_inserter(out));
        return out;
    }

    // UTF-32 -> UTF-8（抛异常）
    inline std::string utf32ToUtf8(const std::u32string& s)
    {
        std::string out;
        utf8::utf32to8(s.begin(), s.end(), std::back_inserter(out));
        return out;
    }

    // UTF-8 -> wstring（抛异常）
    // - Windows（wchar_t==2）：经 UTF-16 中转
    // - Linux/macOS（wchar_t==4）：经 UTF-32 中转
    inline std::wstring utf8ToWstring(std::string_view s)
    {
        static_assert(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4, "不支持的 wchar_t 大小");
        if constexpr (sizeof(wchar_t) == 2)
        {
            std::u16string u16;
            utf8::utf8to16(s.begin(), s.end(), std::back_inserter(u16));
            return std::wstring(u16.begin(), u16.end());
        }
        else
        {
            std::u32string u32;
            utf8::utf8to32(s.begin(), s.end(), std::back_inserter(u32));
            return std::wstring(u32.begin(), u32.end());
        }
    }

    // wstring -> UTF-8（抛异常）
    inline std::string wstringToUtf8(const std::wstring& ws)
    {
        static_assert(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4, "不支持的 wchar_t 大小");
        if constexpr (sizeof(wchar_t) == 2)
        {
            std::u16string u16(ws.begin(), ws.end());
            std::string out;
            utf8::utf16to8(u16.begin(), u16.end(), std::back_inserter(out));
            return out;
        }
        else
        {
            std::u32string u32(ws.begin(), ws.end());
            std::string out;
            utf8::utf32to8(u32.begin(), u32.end(), std::back_inserter(out));
            return out;
        }
    }

    // 安全版本：UTF-8 -> UTF-16（不抛异常，失败返回 false）
    inline bool utf8ToUtf16Safe(std::string_view s, std::u16string& out) noexcept
    {
        try
        {
            out.clear();
            utf8::utf8to16(s.begin(), s.end(), std::back_inserter(out));
            return true;
        }
        catch (...)
        {
            out.clear();
            return false;
        }
    }

    // 安全版本：UTF-16 -> UTF-8（不抛异常）
    inline bool utf16ToUtf8Safe(const std::u16string& s, std::string& out) noexcept
    {
        try
        {
            out.clear();
            utf8::utf16to8(s.begin(), s.end(), std::back_inserter(out));
            return true;
        }
        catch (...)
        {
            out.clear();
            return false;
        }
    }

    // 逐码点读取（迭代器式）：返回下一个码点（抛异常：非法）
    inline char32_t nextCodePoint(const char*& it, const char* end)
    {
        return (char32_t)utf8::next(it, end);
    }

    // 追加单个码点到 UTF-8 字符串（抛异常：非法码点时抛）
    inline void appendCodePoint(char32_t cp, std::string& out)
    {
        utf8::append((uint32_t)cp, std::back_inserter(out));
    }

    // 移除 UTF-8 BOM（若存在）
    inline std::string_view removeUtf8BOM(std::string_view s)
    {
        if (s.size() >= 3 &&
            static_cast<unsigned char>(s[0]) == 0xEF &&
            static_cast<unsigned char>(s[1]) == 0xBB &&
            static_cast<unsigned char>(s[2]) == 0xBF)
        {
            return std::string_view(s.data() + 3, s.size() - 3);
        }
        return s;
    }
}
