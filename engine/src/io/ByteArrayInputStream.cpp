//
// Created by wuxianggujun on 2024/8/14.
//

#include "ByteArrayInputStream.hpp"

#include <algorithm>
#include <utility>

namespace Tina
{
    ByteArrayInputStream::ByteArrayInputStream(Bytes bytes): buf_(bytes), pos(bytes.size()),
                                                             count(bytes.capacity())
    {
    }

    ByteArrayInputStream::ByteArrayInputStream(Bytes bytes, const size_t offset, size_t length): buf_(std::move(bytes)),
        pos(offset), count(length)
    {
    }

    void ByteArrayInputStream::close()
    {
    }

    Byte ByteArrayInputStream::read()
    {
        return pos < count ? buf_[pos++] & 0xff : Byte(-1);
    }

    Buffer<Byte> ByteArrayInputStream::read(size_t size)
    {
        if (pos >= count)
        {
            return Buffer<Byte>(0);
        }

        size_t available = std::min(size, count - pos);
        Buffer<Byte> buffer(available);
        std::copy_n(buf_.begin() + pos, available, buffer.begin());
        pos += available;
        return buffer;
    }

    size_t ByteArrayInputStream::read(Bytes& bytes, size_t off, size_t len) const
    {
        if (len > bytes.capacity() - off)
        {
            throw std::runtime_error("Read IndexOutOfBoundsException");
        }

        if (pos >= count)
        {
            return -1;
        }

        size_t avail = std::min(len, count - pos);
        if (len > avail)
        {
            len = avail;
        }
        if (len <= 0)
        {
            return 0;
        }

        // 确保 bytes 不是 const，以便可以赋值
        std::copy_n(buf_.begin() + pos, len, bytes.begin() + off);
        pos += len;
        return len;
    }

    size_t ByteArrayInputStream::transferTo(OutputStream& out)
    {
        return 0;
    }

    ByteArrayInputStream::~ByteArrayInputStream()
    = default;
} // Tina
