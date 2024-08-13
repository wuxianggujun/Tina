/*
//
// Created by wuxianggujun on 2024/7/30.
//

#include "ByteArrayOutputStream.hpp"

namespace Tina
{
    ByteArrayOutputStream::ByteArrayOutputStream()
    {
        buffer_ = new Bytes(BUFFER_DEFAULT_SIZE);
        buffer_->resize(0);
    }

    ByteArrayOutputStream::ByteArrayOutputStream(size_t size)
    {
        buffer_ = new Bytes(size);
        buffer_->resize(0);
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
        for (size_t i = 0; i < buffer_->size(); ++i)
        {
            byteArray[i] = static_cast<uint8_t>((*buffer_)[i]);
        }
        return byteArray;
    }

    Buffer<Byte>* ByteArrayOutputStream::getByteBuffer() const
    {
        return buffer_;
    }
    
    void ByteArrayOutputStream::close()
    {
        if (buffer_ && buffer_->sizeBytes() > 0)
        {
            reset();
            delete buffer_;
            buffer_ = nullptr;
        }
    }

    void ByteArrayOutputStream::flush()
    {
    }

    void ByteArrayOutputStream::write(const Byte& byte)
    {
        buffer_->append(byte);
    }

    void ByteArrayOutputStream::write(Byte* bytes, size_t size)
    {
        buffer_->append(bytes, size);
    }

    void ByteArrayOutputStream::write(Bytes& bytes)
    {
    }

    void ByteArrayOutputStream::writeBytes(Bytes* outputBuffer) const
    {
        if (outputBuffer)
        {
            *outputBuffer = *buffer_;
        }
    }


    ByteArrayOutputStream::~ByteArrayOutputStream()
    {
        ByteArrayOutputStream::close();
    }
} // Tina
*/
