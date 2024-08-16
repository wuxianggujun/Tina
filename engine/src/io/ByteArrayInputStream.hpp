//
// Created by wuxianggujun on 2024/8/14.
//

#ifndef TINA_IO_BYTEARRAYINPUTSTREAM_HPP
#define TINA_IO_BYTEARRAYINPUTSTREAM_HPP

#include "InputStream.hpp"


namespace Tina
{
    class ByteArrayInputStream : public InputStream
    {
    public:
        explicit ByteArrayInputStream(Bytes bytes);
        ByteArrayInputStream(Bytes bytes, size_t offset, size_t length);

        void setReadPos(size_t pos = 0) const {
            buf_.setReadPos(pos);
        }
        
        void close() override;
        Byte read() override;
        Scope<Buffer<Byte>> read(size_t size) override;
        size_t read(Buffer<Byte>* bytes, std::size_t off, std::size_t len) const;
        size_t transferTo(OutputStream& out) override;
        ~ByteArrayInputStream() override;

    private:
        Bytes buf_; // 数据容量
    };
} // Tina

#endif //TINA_IO_BYTEARRAYINPUTSTREAM_HPP
