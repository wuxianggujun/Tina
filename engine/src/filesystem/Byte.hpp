//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_BYTE_HPP
#define TINA_FILESYSTEM_BYTE_HPP

#include <cstdint>
#include <ostream>

namespace Tina
{
    class Byte
    {
    public:
        explicit constexpr Byte(uint8_t data = 0) noexcept : data_(data)
        {
        }
        Byte(const Byte& other) = default;

        [[nodiscard]] constexpr uint8_t getData() const noexcept
        {
            return data_;
        }
        

        [[nodiscard]] constexpr char getChar() const noexcept
        {
            return static_cast<char>(data_);
        }

        bool operator!=(const Byte& other) const noexcept
        {
            return data_ != other.data_;
        }

        bool operator==(const Byte& other) const noexcept
        {
            return data_ == other.data_;
        }

        bool operator<(const Byte& other) const noexcept
        {
            return data_ < other.data_;
        }

        bool operator>(const Byte& other) const noexcept
        {
            return data_ > other.data_;
        }

        Byte& operator=(uint8_t data) noexcept
        {
            data_ = data;
            return *this;
        }

        // 赋值操作符重载
        Byte& operator=(const Byte& other)
        {
            if (this != &other)
            {
                data_ = other.data_;
            }
            return *this;
        }


        Byte& operator+=(uint8_t value) noexcept
        {
            data_ += value;
            return *this;
        }

        Byte operator+(uint8_t value) const noexcept
        {
            return Byte(data_ + value);
        }

        explicit operator uint8_t() const noexcept
        {
            return data_;
        }

        explicit operator int() const noexcept
        {
            return static_cast<int>(data_);
        }

        static constexpr Byte zero() noexcept
        {
            return Byte(0);
        }
    

    private:
        uint8_t data_;
    };

    inline std::ostream& operator<<(std::ostream& os, const Byte& byte)
    {
        os << static_cast<int>(byte.getData());
        return os;
    }
}

#endif //TINA_FILESYSTEM_BYTE_HPP
