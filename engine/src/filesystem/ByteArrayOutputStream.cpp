//
// Created by wuxianggujun on 2024/7/30.
//

#include "ByteArrayOutputStream.hpp"

namespace Tina
{
    ByteArrayOutputStream::ByteArrayOutputStream()
    {
        buffer_ = new ByteBuffer();
    }

    ByteArrayOutputStream::ByteArrayOutputStream(size_t size)
    {
        buffer_ = new ByteBuffer(size);
    }

    void ByteArrayOutputStream::reset() const
    {
        buffer_->clear();
    }

    size_t ByteArrayOutputStream::size() const
    {
        return buffer_->size();
    }

    std::vector<uint8_t> ByteArrayOutputStream::toByteArray() const
    {
        std::vector<uint8_t> byteArray(buffer_->size());
        std::memcpy(byteArray.data(), buffer_->peek(), buffer_->size());
        return byteArray;
    }

    ByteBuffer* ByteArrayOutputStream::getByteBuffer() const
    {
        return buffer_;
    }

    void ByteArrayOutputStream::close()
    {
        if (buffer_ && !buffer_->isEmpty())
        {
            reset();
            delete buffer_;
            buffer_ = nullptr;
        }
    }

    void ByteArrayOutputStream::flush()
    {
    }

    void ByteArrayOutputStream::write(Byte byte)
    {
        buffer_->append(byte.getData());
    }

    void ByteArrayOutputStream::write(Byte* bytes, size_t size)
    {
        buffer_->append(bytes, size);
    }

    void ByteArrayOutputStream::write(ByteBuffer& buffer)
    {
        buffer_->swap(buffer);
    }

    ByteArrayOutputStream::~ByteArrayOutputStream()
    {
        ByteArrayOutputStream::close();
    }
} // Tina
