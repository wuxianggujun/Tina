#pragma once

#include <cstdint>

namespace Tina {
    class Color {
    public:
        float r, g, b, a;

        // 构造函数
        Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
        Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
        
        // 预定义颜色
        static const Color White;
        static const Color Black;
        static const Color Red;
        static const Color Green;
        static const Color Blue;
        static const Color Yellow;
        static const Color Magenta;
        static const Color Cyan;
        static const Color Transparent;

        // 转换为32位ABGR颜色值（BGFX使用）
        uint32_t toABGR() const {
            uint8_t r8 = static_cast<uint8_t>(r * 255.0f);
            uint8_t g8 = static_cast<uint8_t>(g * 255.0f);
            uint8_t b8 = static_cast<uint8_t>(b * 255.0f);
            uint8_t a8 = static_cast<uint8_t>(a * 255.0f);
            return (a8 << 24) | (b8 << 16) | (g8 << 8) | r8;
        }

        // 从32位ABGR颜色值创建
        static Color fromABGR(uint32_t abgr) {
            float r = static_cast<float>((abgr & 0x000000FF) >> 0) / 255.0f;
            float g = static_cast<float>((abgr & 0x0000FF00) >> 8) / 255.0f;
            float b = static_cast<float>((abgr & 0x00FF0000) >> 16) / 255.0f;
            float a = static_cast<float>((abgr & 0xFF000000) >> 24) / 255.0f;
            return Color(r, g, b, a);
        }
    };

    // 预定义颜色的实现
    inline const Color Color::White(1.0f, 1.0f, 1.0f, 1.0f);
    inline const Color Color::Black(0.0f, 0.0f, 0.0f, 1.0f);
    inline const Color Color::Red(1.0f, 0.0f, 0.0f, 1.0f);
    inline const Color Color::Green(0.0f, 1.0f, 0.0f, 1.0f);
    inline const Color Color::Blue(0.0f, 0.0f, 1.0f, 1.0f);
    inline const Color Color::Yellow(1.0f, 1.0f, 0.0f, 1.0f);
    inline const Color Color::Magenta(1.0f, 0.0f, 1.0f, 1.0f);
    inline const Color Color::Cyan(0.0f, 1.0f, 1.0f, 1.0f);
    inline const Color Color::Transparent(0.0f, 0.0f, 0.0f, 0.0f);
} 