//
// Created by wuxianggujun on 2024/7/28.
// https://github.com/RamseyK/ByteBufferCpp/blob/main/src/ByteBuffer.hpp
// https://github.com/jameyboor/Robots-on-Wheels/blob/1cde0a4b512f0d5d88f46f70689035ce580d02ce/ByteBuffer.h
//

#ifndef TINA_FILESYSTEM_BYTEBUFFER_HPP
#define TINA_FILESYSTEM_BYTEBUFFER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "tool/Endianness.hpp"

namespace Tina
{
    class ByteBuffer
    {
    public:
        // 4096
        static constexpr size_t BUFFER_DEFAULT_SIZE = 0x1000;

        ByteBuffer() : rPos_(0), wPos_(0)
        {
            buffer_.reserve(BUFFER_DEFAULT_SIZE);
        }

        explicit ByteBuffer(size_t size) : rPos_(0), wPos_(0)
        {
            buffer_.reserve(size);
        }

        ByteBuffer(ByteBuffer&& buffer) noexcept: rPos_(buffer.rPos_), wPos_(buffer.wPos_),
                                                  buffer_(std::move(buffer.buffer_))
        {
        }

        ByteBuffer(ByteBuffer const& right) noexcept = default;

        ByteBuffer& operator=(ByteBuffer const& right) noexcept
        {
            if (this != &right)
            {
                rPos_ = right.rPos_;
                wPos_ = right.wPos_;
                buffer_ = right.buffer_;
            }
            return *this;
        }

        virtual ~ByteBuffer()
        {
            clear();
        }

        void clear()
        {
            buffer_.clear();
            rPos_ = wPos_ = 0;
        }

        size_t capacity() const
        {
            return buffer_.capacity();
        }

        size_t size() const
        {
            return buffer_.size();
        }

        void reserve(size_t size)
        {
            if (size > buffer_.size())
            {
                buffer_.reserve(size);
            }
        }

        void resize(size_t size)
        {
            buffer_.resize(size);
            rPos_ = 0;
            wPos_ = 0;
        }


        void setReadPos(size_t pos) const
        {
            rPos_ = pos;
        }

        size_t getReadPos() const
        {
            return rPos_;
        }

        void setWritePos(size_t pos)
        {
            wPos_ = pos;
        }

        size_t getWritePos() const
        {
            return wPos_;
        }
        

        std::string readString() const
        {
            std::string str{};
            char byte = '\0';

            // 确保当前读位置有效
            while (rPos_ < buffer_.size())
            {
                // 从当前读位置读取char
                byte = read<char>(rPos_);
                str += byte; // 将读取的字符添加到字符串

                rPos_++; // 更新读位置

                // 如果读取到字符串结束符，退出循环
                if (byte == '\0')
                {
                    break;
                }
            }

            return str;
        }


        // 比较两个ByteBuffer对象是否相等
        bool equal(const ByteBuffer& other) const
        {
            if (size() != other.size())
            {
                return false;
            }
            return std::memcmp(buffer_.data(), other.buffer_.data(), size()) == 0;
        }

        // 获取缓冲区剩余字节数
        size_t bytesRemaining() const
        {
            return size() - rPos_;
        }

        void copyTo(ByteBuffer& other) const
        {
            other.resize(size());
            std::memcpy(other.buffer_.data(), buffer_.data(), size());
            other.rPos_ = rPos_;
            other.wPos_ = wPos_;
        }

        template <typename T>
        void put(size_t pos, T value)
        {
            static_assert(std::is_fundamental_v<T>, "put(compound)");
            if (pos + sizeof(T) <= size())
            {
                std::memcpy((&buffer_[pos], reinterpret_cast<const uint8_t*>(&value), sizeof(T)));
            }
        }


        template <typename T>
        T read() const
        {
            T data = read<T>(rPos_);
            rPos_ += sizeof(T);
            return data;
        }


        template <typename T>
        T read(size_t pos) const
        {
            T value{};
            if (pos + sizeof(T) <= buffer_.size())
            {
                value = *reinterpret_cast<const T*>(&buffer_[pos]);
                value = Tool::EndianConvert(value); // 执行大小端转
                return value;
            }
            return value;
        }


        /*template <typename T>
        T read(T& value) const
        {
            return readAtPosition(rPos_, value);
        }*/


        void read(uint8_t* dest, size_t len) const
        {
            if ((rPos_ + len) < buffer_.size())
            {
                std::memcpy(dest, &buffer_[rPos_], len);
                rPos_ += len;
            }
        }

        void append(const uint8_t* src, size_t cnt)
        {
            if (src && cnt)
            {
                if ((buffer_.size() < wPos_ + cnt))
                {
                    buffer_.resize(wPos_ + cnt);
                }

                std::memcpy(&buffer_[wPos_], src, cnt);
                wPos_ += cnt;
            }
        }

        template <typename T>
        void append(const T* src, size_t cnt)
        {
            return append(reinterpret_cast<const uint8_t*>(src), cnt * sizeof(T));
        }

        template <typename T>
        void append(T& value)
        {
            static_assert(std::is_fundamental_v<T>, "append(compound)");
            value = EndianConvert(value);
            append(reinterpret_cast<uint8_t*>(&value), sizeof(value));
        }

    protected:
        mutable size_t rPos_;
        size_t wPos_;
        std::vector<uint8_t> buffer_;
    };
} // Tina

#endif //TINA_FILESYSTEM_BYTEBUFFER_HPP
