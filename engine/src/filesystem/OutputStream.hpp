//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_OUTPUTSTREAM_HPP
#define TINA_FILESYSTEM_OUTPUTSTREAM_HPP

#include "Closeable.hpp"
#include "Flushable.hpp"
#include "Byte.hpp"
#include "Buffer.hpp"
#include "ByteBuffer.hpp"

namespace Tina
{
    class ByteBuffer;

    class OutputStream : public Closeable, Flushable
    {
    protected:
        using Bytes = Buffer<Byte>;
        //using Bytes = ByteBuffer;
    public:
        void close() override = 0;
        void flush() override = 0;

        virtual void write(Byte byte) = 0;

        virtual void write(Byte* bytes, size_t size) = 0;

        virtual void write(Bytes& buffer) = 0;
    };
} // Tina

#endif //TINA_FILESYSTEM_OUTPUTSTREAM_HPP
