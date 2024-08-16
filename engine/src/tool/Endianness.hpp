//
// Created by wuxianggujun on 2024/7/29.
//

#ifndef TINA_TOOL_ENDIANNESS_HPP
#define TINA_TOOL_ENDIANNESS_HPP
#include <cstdint>
#include <memory>
#include <type_traits>

namespace Tina::Tool
{
    /*inline uint32_t EndianConvert(uint32_t value)
    {
        auto np = reinterpret_cast<unsigned char*>(&value);

        return (static_cast<uint32_t>(np[0]) << 24) |
            (static_cast<uint32_t>(np[1]) << 16) |
            (static_cast<uint32_t>(np[2]) << 8) |
            static_cast<uint32_t>(np[3]);
    }*/
    
    template <typename T>
    inline T EndianConvert(const T& value)
    {
        static_assert(std::is_fundamental_v<T>, "EndianConvert supports only fundamental types.");
        T result;
        memcpy(&result, reinterpret_cast<const uint8_t*>(&value), sizeof(value));
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            reinterpret_cast<uint8_t*>(&result)[i] = reinterpret_cast<const uint8_t*>(&value)[sizeof(T) - 1 - i];
        }
        return result;
    }
    
    
}


#endif //TINA_TOOL_ENDIANNESS_HPP
