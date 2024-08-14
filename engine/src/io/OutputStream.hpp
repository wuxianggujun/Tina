//
// Created by wuxianggujun on 2024/8/13.
//

#ifndef TINA_IO_OUTPUTSTREAM_HPP
#define TINA_IO_OUTPUTSTREAM_HPP

#include "Byte.hpp"
#include "Buffer.hpp"
#include "Closeable.hpp"
#include "Flushable.hpp"

namespace Tina
{
    class OutputStream : Closeable, Flushable
    {
    public:
        using Bytes = Buffer<Byte>;

        void close() override = 0;
        void flush() override = 0;
        virtual void write(const Byte& byte) = 0;
        virtual void write(const Bytes& buffer) = 0;
        virtual void write(const Bytes& buffer, size_t size) = 0;
    };
} // Tina

#endif //TINA_IO_OUTPUTSTREAM_HPP
