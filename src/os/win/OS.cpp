//
// Created by wuxianggujun on 25-9-26.
//
#include "../OS.hpp"

#include "client/TracyThread.hpp"

#define DEBUG_CHECK(R) if(!(R)) ASSERT(false)
#define FATAL_CHECK(R) do { if(!(R)) abort(); } while(false)

namespace Tina::os
{

    template<int N> static void toWchar(WCHAR (&out)[N], StringView in)
    {
        const char* c = in.begin;
        WCHAR* cout = out;
        while (c != in.end && c -in.begin < N - 1)
        {
            *cout = *c;
            ++cout;
            ++c;
        }

        FATAL_CHECK(c == in.end);
        *cout = 0;
    }

    
    template <int N>
    struct WCharStr
    {
        WCharStr(StringView rhs)
        {
            toWchar(data, rhs);
        }

        operator const WCHAR*() const
        {
            return data;
        }

        WCHAR data[N];
    };

    WindowHandle os::createWindow(const InitWindowArgs& args)
    {
    }
}
