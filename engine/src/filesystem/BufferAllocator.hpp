//
// Created by wuxianggujun on 2024/7/31.
//

#ifndef TINA_FILESYSTEM_BUFFERALLOCATOR_HPP
#define TINA_FILESYSTEM_BUFFERALLOCATOR_HPP

#include <iostream>
#include <cstddef>

namespace Tina
{
    // 如果没有指定的BufferAllocator，则使用默认的BufferAllocator
    template <typename ch>
    class BufferAllocator
    {
    public:
        typedef ch char_type;

        static char_type* allocate(const std::streamsize size)
        {
            return new char_type[static_cast<std::size_t>(size)];
        }

        static void deallocate(const char_type* ptr, const std::streamsize size) noexcept
        {
            delete[] ptr;
        }
    };
}

#endif //TINA_FILESYSTEM_BUFFERALLOCATOR_HPP 
