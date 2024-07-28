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
        explicit Byte(uint8_t data = 0): data_(data), is_null_(false)
        {
        }

        [[nodiscard]] uint8_t getData() const
        {
            return data_;
        }

        [[nodiscard]] bool isNull() const
        {
            return is_null_;
        }
    
    private:
        uint8_t data_;
        bool is_null_;
    };
}

#endif //TINA_FILESYSTEM_BYTE_HPP
