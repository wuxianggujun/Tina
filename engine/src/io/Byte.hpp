//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_BYTE_HPP
#define TINA_FILESYSTEM_BYTE_HPP

#include <cstdint>
#include <ostream>
#include <type_traits>

namespace Tina {
    class Byte {
    public:
        explicit constexpr Byte(uint8_t data = 0) noexcept : data_(data) {
        }

        Byte(const Byte &other) = default;

        Byte &operator=(const Byte &other) = default;

        [[nodiscard]] constexpr uint8_t data() const noexcept { return data_; } // 更简洁的命名
        [[nodiscard]] constexpr char to_char() const noexcept { return static_cast<char>(data_); }

        // 位运算符
        [[nodiscard]] constexpr Byte operator&(Byte other) const noexcept { return Byte(data_ & other.data_); }
        [[nodiscard]] constexpr Byte operator|(Byte other) const noexcept { return Byte(data_ | other.data_); }
        [[nodiscard]] constexpr Byte operator^(Byte other) const noexcept { return Byte(data_ ^ other.data_); }
        [[nodiscard]] constexpr Byte operator~() const noexcept { return Byte(~data_); }
        [[nodiscard]] constexpr Byte operator<<(int shift) const noexcept { return Byte(data_ << shift); }
        [[nodiscard]] constexpr Byte operator>>(int shift) const noexcept { return Byte(data_ >> shift); }

        // 赋值运算符
        Byte &operator=(uint8_t data) noexcept {
            data_ = data;
            return *this;
        }

        // 复合赋值运算符
        Byte &operator+=(uint8_t value) noexcept {
            data_ += value;
            return *this;
        }

        Byte &operator-=(uint8_t value) noexcept {
            data_ -= value;
            return *this;
        }

        Byte &operator&=(Byte other) noexcept {
            data_ &= other.data_;
            return *this;
        }

        Byte &operator|=(Byte other) noexcept {
            data_ |= other.data_;
            return *this;
        }

        Byte &operator^=(Byte other) noexcept {
            data_ ^= other.data_;
            return *this;
        }

        Byte &operator<<=(int shift) noexcept {
            data_ <<= shift;
            return *this;
        }

        Byte &operator>>=(int shift) noexcept {
            data_ >>= shift;
            return *this;
        }

        // 算术运算符（可选，根据你的需求决定是否保留）
        [[nodiscard]] Byte operator+(uint8_t value) const noexcept { return Byte(data_ + value); }
        [[nodiscard]] Byte operator-(uint8_t value) const noexcept { return Byte(data_ - value); }

        // 比较运算符
        [[nodiscard]] bool operator==(Byte other) const noexcept { return data_ == other.data_; }
        [[nodiscard]] bool operator!=(Byte other) const noexcept { return data_ != other.data_; }
        [[nodiscard]] bool operator<(Byte other) const noexcept { return data_ < other.data_; }
        [[nodiscard]] bool operator<=(Byte other) const noexcept { return data_ <= other.data_; }
        [[nodiscard]] bool operator>(Byte other) const noexcept { return data_ > other.data_; }
        [[nodiscard]] bool operator>=(Byte other) const noexcept { return data_ >= other.data_; }

        // 显式类型转换
        explicit operator uint8_t() const noexcept { return data_; }
        explicit operator int() const noexcept { return data_; }

        // 类似于 std::byte 的 to_integer
        template<typename T>
        [[nodiscard]] constexpr std::enable_if_t<std::is_integral_v<T>, T> to_integer() const noexcept {
            return static_cast<T>(data_);
        }

    private:
        uint8_t data_;
    };

    // operator<< 重载被移到了类定义之外，但在命名空间内
    inline std::ostream &operator<<(std::ostream &os, const Byte &byte) {
        os << static_cast<int>(byte.data()); // 使用 data()
        return os;
    }
} // namespace Tina

#endif // TINA_FILESYSTEM_BYTE_HPP
