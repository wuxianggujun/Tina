//
// Created by wuxianggujun on 2024/7/30.
//

#ifndef TINA_FILESYSTEM_BYTEARRAYOUTPUTSTREAM_HPP
#define TINA_FILESYSTEM_BYTEARRAYOUTPUTSTREAM_HPP

#include "OutputStream.hpp"
#include "ByteBuffer.hpp"
#include "Byte.hpp"
#include "File.hpp"
#include <vector>

namespace Tina
{
    class ByteArrayOutputStream : public OutputStream
    {
    public:
        ByteArrayOutputStream();
        explicit ByteArrayOutputStream(size_t size);

        void reset() const;
        [[nodiscard]] size_t size() const;
        [[nodiscard]] std::vector<uint8_t> toByteArray() const;

        [[nodiscard]] Buffer<Byte>*  getByteBuffer() const;

        void close() override;
        void flush() override;
        void write(Byte byte) override;
        void write(Byte* bytes, size_t size) override;
        void write(Bytes& bytes) override;
        void writeBytes(Bytes* outputBuffer) const;
        ~ByteArrayOutputStream() override;
    protected:
        Bytes* buffer_;
    };
} // Tina

#endif //TINA_FILESYSTEM_BYTEARRAYOUTPUTSTREAM_HPP
