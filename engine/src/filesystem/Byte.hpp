//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_BYTE_HPP
#define TINA_FILESYSTEM_BYTE_HPP

#include <cstdint>

namespace Tina
{
    class Byte
    {
    public:
        explicit Byte(uint8_t data = 0): data_(data)
        {
        }

        explicit Byte(int data = 0) : data_(static_cast<uint8_t>(data))
        {
        }

        [[nodiscard]] const uint8_t* getData() const
        {
            return &data_;
        }

        [[nodiscard]] char getChar() const
        {
            return static_cast<char>(data_);
        }

    private:
        uint8_t data_;
    };
}

#endif //TINA_FILESYSTEM_BYTE_HPP
