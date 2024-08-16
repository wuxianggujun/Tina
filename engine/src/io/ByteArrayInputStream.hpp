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

        void close() override;
        Byte read() override;
        Bytes read(size_t size) override;
        size_t read(Bytes& bytes, size_t off, size_t len) const;
        size_t transferTo(OutputStream& out) override;
        ~ByteArrayInputStream() override;

    private:
        Bytes buf_; // 数据容量
        mutable size_t pos; // 当前读取位置
        mutable size_t count;; // 数据长度
        size_t mark = 0; // 标记位置
    };
} // Tina

#endif //TINA_IO_BYTEARRAYINPUTSTREAM_HPP
