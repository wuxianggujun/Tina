//
// Created by 33442 on 2024/3/14.
//

#ifndef UTIL_HPP
#define UTIL_HPP
#include "PlatformDetection.hpp"
#include <memory>
#include <sstream>
#include <thread>
#ifdef TINA_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#endif
#include <cstdlib>
#include <vector>
#include <boost/lexical_cast.hpp>

namespace Tina
{
    template <typename To, typename From>
    inline To implicit_cast(From const& f)
    {
        return f;
    }

    namespace Util
    {
        // 这段代码定义了一个 C++11 版本的 make_unique<T>() 函数模板，用于创建并返回一个指向类型 T 的 std::unique_ptr 智能指针。
        template <typename T, typename... Args>
        std::unique_ptr<T> make_unique(Args&&... args)
        {
            return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        }

#ifdef TINA_PLATFORM_WINDOWS
        inline DWORD gettid()
        {
            return GetCurrentThreadId();
        }

        extern thread_local DWORD t_cachedTid;

#else

        inline pid_t gettid()
        {
            return static_cast<pid_t>(::syscall(SYS_gettid));
        }
        extern thread_local pid_t t_cachedTid;

# endif

        inline void cacheTid()
        {
            if (t_cachedTid == 0)
            {
                t_cachedTid = gettid();
            }
        }
#ifdef TINA_PLATFORM_WINDOWS
        inline DWORD tid() {
            if (t_cachedTid == 0)
            {
                cacheTid();
            }
            return t_cachedTid;
        }
#else
		inline pid_t tid() {
			if (__builtin_expect(t_cachedTid == 0, 0))
			{
				cacheTid();
			}
			return t_cachedTid;
	}
#endif // PLATFORM_WINDOWS

        uint64_t getCoid();

        const char* strerror_tl(int savedErrno);

        struct  CaseInsensitivelLess
        {
			bool operator()(const std::string& lhs, const std::string& rhs) const {
#ifdef TINA_PLATFORM_WINDOWS
                return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
#else
                return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
#endif // PLATFORM_WINDOWS

				
			}

        };
    

    }
} // Tina

#endif //UTIL_HPP
