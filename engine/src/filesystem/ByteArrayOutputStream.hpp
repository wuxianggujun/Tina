//
// Created by wuxianggujun on 2024/7/30.
//

#ifndef TINA_FILESYSTEM_BYTEARRAYOUTPUTSTREAM_HPP
#define TINA_FILESYSTEM_BYTEARRAYOUTPUTSTREAM_HPP

#include "OutputStream.hpp"
#include "ByteBuffer.hpp"
#include "Byte.hpp"
#include <vector>

namespace Tina
{
    class ByteArrayOutputStream : public OutputStream
    {
    public:
        ByteArrayOutputStream();
        explicit ByteArrayOutputStream(size_t size);

        void reset() const;
        size_t size() const;
        std::vector<uint8_t> toByteArray() const;

        ByteBuffer* getByteBuffer() const;

        void close() override;
        void flush() override;
        void write(Byte byte) override;
        void write(Byte* bytes, size_t size) override;
        void write(Bytes& bytes) override;
        ~ByteArrayOutputStream() override;

    protected:
        ByteBuffer* buffer_;
    };
} // Tina

#endif //TINA_FILESYSTEM_BYTEARRAYOUTPUTSTREAM_HPP
