//
// Created by wuxianggujun on 25-9-27.
//

#pragma once

#include "os/OS.hpp"
#include <string>
#include <EASTL/deque.h>
#include <variant>
#include <functional>
#include <cstdint>
#include "core/utils/StringUtils.hpp"

struct HWND__;
typedef HWND__* HWND;

namespace Tina::OS
{

    struct Utf
    {
        static std::wstring toWide(const char* str,u32 len);
        static std::wstring toWide(const std::string& str);
        static std::string toUtf8(const wchar_t* str);
    };
    
    
}
