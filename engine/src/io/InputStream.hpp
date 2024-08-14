//
// Created by wuxianggujun on 2024/8/13.
//

#ifndef TINA_IO_INPUTSTREAM_HPP
#define TINA_IO_INPUTSTREAM_HPP

#include "Byte.hpp"
#include "Buffer.hpp"
#include "Closeable.hpp"

namespace Tina
{
    class InputStream : Closeable
    {
    public:
        using Bytes = Buffer<Byte>;
        void close() override = 0; 
        virtual Byte read() = 0;
        virtual Bytes read(size_t size) = 0;
    };
} // Tina

#endif //TINA_IO_INPUTSTREAM_HPP
