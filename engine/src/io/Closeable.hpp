//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_CLOSEABLE_HPP
#define TINA_FILESYSTEM_CLOSEABLE_HPP

#include "AutoCloseable.hpp"

namespace Tina
{
    class Closeable : public AutoCloseable
    {
    public:
        void close() override = 0;
        ~Closeable() override = default;
    };
}


#endif //TINA_FILESYSTEM_CLOSEABLE_HPP
