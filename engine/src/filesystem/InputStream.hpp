//
// Created by wuxianggujun on 2024/8/12.
//

#ifndef TINA_FILESYSTEM_INPUTSTREAM_HPP
#define TINA_FILESYSTEM_INPUTSTREAM_HPP

#include "Closeable.hpp"
#include "Flushable.hpp"
#include "Buffer.hpp"
#include "Byte.hpp"

namespace Tina
{
    class InputStream : public Closeable, Flushable
    {
    protected:
        using Bytes = Buffer<Byte>;

    public:
        void close() override;
        void flush() override;
        virtual size_t read(Byte* bytes, size_t size) = 0;
        virtual size_t read(Bytes& buffer) = 0;
        virtual size_t read(Byte& byte) = 0;
        virtual size_t read() = 0;
    };
}

#endif //TINA_FILESYSTEM_INPUTSTREAM_HPP
