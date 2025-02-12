//
// Created by wuxianggujun on 2025/2/12.
//
#pragma once

#include "tina/math/Vec.hpp"

namespace Tina
{
    class Color
    {
    public:
        Color() : m_color(1.0f, 1.0f, 1.0f, 1.0f)
        {
        }

        explicit Color(const float r, const float g, const float b, const float a = 1.0f)
            : m_color(r, g, b, a)
        {
        }

        explicit Color(const Math::Vec4f& color)
            : m_color(color)
        {
        }

        explicit Color(const glm::vec4& color)
            : m_color(Math::Vec4f(color))
        {
        }

        explicit operator glm::vec4() const { return static_cast<glm::vec4>(m_color); }
        
        explicit operator Math::Vec4f() const { return m_color; }

        explicit operator uint32_t() const
        {
            uint8_t r = static_cast<uint8_t>(m_color.x * 255.0f);
            uint8_t g = static_cast<uint8_t>(m_color.y * 255.0f);
            uint8_t b = static_cast<uint8_t>(m_color.z * 255.0f);
            uint8_t a = static_cast<uint8_t>(m_color.w * 255.0f);
            return (a << 24) | (b << 16) | (g << 8) | r;  // ABGR format for BGFX
        }

        // 获取颜色分量
        float getR() const { return m_color.x; }
        float getG() const { return m_color.y; }
        float getB() const { return m_color.z; }
        float getA() const { return m_color.w; }

        static const Color White; // 白色
        static const Color Black; // 黑色
        static const Color Red; // 红色
        static const Color Green; // 绿色
        static const Color Blue; // 蓝色
        static const Color Yellow; // 黄色
        static const Color Magenta; // 品红色
        static const Color Cyan; // 青色
        static const Color Gray; // 灰色
        static const Color Orange; // 橙色
        static const Color Purple; // 紫色
        static const Color Brown; // 棕色
        static const Color Pink; // 粉色
        static const Color Transparent; // 透明色

    private:
        Math::Vec4f m_color;
    };

    // 定义预设颜色
    inline const Color Color::White(1.0f, 1.0f, 1.0f, 1.0f);
    inline const Color Color::Black(0.0f, 0.0f, 0.0f, 1.0f);
    inline const Color Color::Red(1.0f, 0.0f, 0.0f, 1.0f);
    inline const Color Color::Green(0.0f, 1.0f, 0.0f, 1.0f);
    inline const Color Color::Blue(0.0f, 0.0f, 1.0f, 1.0f);
    inline const Color Color::Yellow(1.0f, 1.0f, 0.0f, 1.0f);
    inline const Color Color::Magenta(1.0f, 0.0f, 1.0f, 1.0f);
    inline const Color Color::Cyan(0.0f, 1.0f, 1.0f, 1.0f);
    inline const Color Color::Gray(0.5f, 0.5f, 0.5f, 1.0f);
    inline const Color Color::Orange(1.0f, 0.5f, 0.0f, 1.0f);
    inline const Color Color::Purple(0.5f, 0.0f, 0.5f, 1.0f);
    inline const Color Color::Brown(0.6f, 0.4f, 0.2f, 1.0f);
    inline const Color Color::Pink(1.0f, 0.7f, 0.7f, 1.0f);
    inline const Color Color::Transparent(0.0f, 0.0f, 0.0f, 0.0f);
} // Tina
