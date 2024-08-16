//
// Created by wuxianggujun on 2024/8/13.
//

#ifndef TINA_IO_INPUTSTREAM_HPP
#define TINA_IO_INPUTSTREAM_HPP

#include "Byte.hpp"
#include "Buffer.hpp"
#include "Closeable.hpp"
#include "core/Core.hpp"

namespace Tina
{
    class OutputStream;
    
    class InputStream : Closeable
    {
    public:
        using Bytes = Buffer<Byte>;
        void close() override = 0;
        virtual Byte read() = 0;
        virtual Scope<Bytes> read(size_t size) = 0;
        virtual size_t transferTo(OutputStream& out) = 0;
    protected:
        const size_t MAX_SKIP_BUFFER_SIZE = 2048;
        const size_t DEFAULT_BUFFER_SIZE = 8192;
    };
} // Tina

#endif //TINA_IO_INPUTSTREAM_HPP
