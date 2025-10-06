//
// 颜色工具（基于 Math::Vec4 的轻量实现）
// - 提供构造/变换/导出等常用能力
// - 使用 GLM Vec4，与整个项目统一
// - 提供常用颜色预定义

#pragma once

#include <cstdint>
#include <algorithm>
#include "Math.hpp"
#include "Container.hpp"

namespace Tina::Core {

class Color {
public:
    // 默认构造：黑色，完全不透明
    Color() : m_rgba(0.0f, 0.0f, 0.0f, 1.0f) {}

    // RGBA构造
    constexpr Color(float r, float g, float b, float a = 1.0f)
        : m_rgba(r, g, b, a) {}

    // 从 Math::Vec4 构造
    explicit constexpr Color(const Math::Vec4& v) : m_rgba(v) {}

    // 隐式转换为 Math::Vec4（方便与UI系统集成）
    constexpr operator Math::Vec4() const { return m_rgba; }

    // 静态构造器（语义化）
    static constexpr Color rgba(float r, float g, float b, float a = 1.0f) {
        return Color(r, g, b, a);
    }

    static constexpr Color rgb(float r, float g, float b) {
        return Color(r, g, b, 1.0f);
    }

    // 从 0xRRGGBB 或 0xAARRGGBB 构造
    static Color fromHex(uint32_t hex, bool hasAlpha = false)
    {
        if (hasAlpha) {
            // 0xAARRGGBB 格式
            float a = (float)((hex >> 24) & 0xFF) / 255.0f;
            float r = (float)((hex >> 16) & 0xFF) / 255.0f;
            float g = (float)((hex >> 8)  & 0xFF) / 255.0f;
            float b = (float)((hex >> 0)  & 0xFF) / 255.0f;
            return Color(r, g, b, a);
        } else {
            // 0xRRGGBB 格式，默认 alpha = 1
            float r = (float)((hex >> 16) & 0xFF) / 255.0f;
            float g = (float)((hex >> 8)  & 0xFF) / 255.0f;
            float b = (float)((hex >> 0)  & 0xFF) / 255.0f;
            return Color(r, g, b, 1.0f);
        }
    }

    // Alpha通道操作
    constexpr Color withAlpha(float a) const {
        return Color(m_rgba.x, m_rgba.y, m_rgba.z, a);
    }

    // 范围限制到 [0, 1]
    Color clamp01() const
    {
        return Color(
            Math::clamp(m_rgba.x, 0.0f, 1.0f),
            Math::clamp(m_rgba.y, 0.0f, 1.0f),
            Math::clamp(m_rgba.z, 0.0f, 1.0f),
            Math::clamp(m_rgba.w, 0.0f, 1.0f)
        );
    }

    // 预乘 Alpha（用于高级渲染）
    Math::Vec4 premultiplied() const
    {
        return Math::Vec4(
            m_rgba.x * m_rgba.w,
            m_rgba.y * m_rgba.w,
            m_rgba.z * m_rgba.w,
            m_rgba.w
        );
    }

    // 线性插值（颜色混合）
    static Color lerp(const Color& a, const Color& b, float t)
    {
        float tt = Math::clamp(t, 0.0f, 1.0f);
        return Color(
            a.m_rgba.x + (b.m_rgba.x - a.m_rgba.x) * tt,
            a.m_rgba.y + (b.m_rgba.y - a.m_rgba.y) * tt,
            a.m_rgba.z + (b.m_rgba.z - a.m_rgba.z) * tt,
            a.m_rgba.w + (b.m_rgba.w - a.m_rgba.w) * tt
        );
    }

    // 乘法调制（灯光/着色叠加）
    Color modulate(const Color& rhs) const
    {
        return Color(
            m_rgba.x * rhs.m_rgba.x,
            m_rgba.y * rhs.m_rgba.y,
            m_rgba.z * rhs.m_rgba.z,
            m_rgba.w * rhs.m_rgba.w
        );
    }

    // 亮度调节
    Color lighten(float amount) const {
        return Color(
            m_rgba.x + amount,
            m_rgba.y + amount,
            m_rgba.z + amount,
            m_rgba.w
        ).clamp01();
    }

    Color darken(float amount) const {
        return lighten(-amount);
    }

    // 饱和度调节（简单实现）
    Color saturate(float amount) const {
        float gray = (m_rgba.x + m_rgba.y + m_rgba.z) / 3.0f;
        return Color(
            Math::mix(gray, m_rgba.x, amount),
            Math::mix(gray, m_rgba.y, amount),
            Math::mix(gray, m_rgba.z, amount),
            m_rgba.w
        ).clamp01();
    }

    // 导出为 Math::Vec4
    constexpr Math::Vec4 toVec4() const { return m_rgba; }

    // 导出为数组（兼容老代码）
    Tina::Container::Array<float, 4> toArray() const
    {
        return {{ m_rgba.x, m_rgba.y, m_rgba.z, m_rgba.w }};
    }

    // 分量访问
    constexpr float r() const { return m_rgba.x; }
    constexpr float g() const { return m_rgba.y; }
    constexpr float b() const { return m_rgba.z; }
    constexpr float a() const { return m_rgba.w; }

    // 设置分量（可选，方便修改）
    void setR(float r) { m_rgba.x = r; }
    void setG(float g) { m_rgba.y = g; }
    void setB(float b) { m_rgba.z = b; }
    void setA(float a) { m_rgba.w = a; }

    // ========== 预定义颜色（常用） ==========

    // 基础颜色
    static constexpr Color White()       { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
    static constexpr Color Black()       { return Color(0.0f, 0.0f, 0.0f, 1.0f); }
    static constexpr Color Red()         { return Color(1.0f, 0.0f, 0.0f, 1.0f); }
    static constexpr Color Green()       { return Color(0.0f, 1.0f, 0.0f, 1.0f); }
    static constexpr Color Blue()        { return Color(0.0f, 0.0f, 1.0f, 1.0f); }
    static constexpr Color Yellow()      { return Color(1.0f, 1.0f, 0.0f, 1.0f); }
    static constexpr Color Cyan()        { return Color(0.0f, 1.0f, 1.0f, 1.0f); }
    static constexpr Color Magenta()     { return Color(1.0f, 0.0f, 1.0f, 1.0f); }

    // 扩展颜色
    static constexpr Color Orange()      { return Color(1.0f, 0.65f, 0.0f, 1.0f); }
    static constexpr Color Purple()      { return Color(0.5f, 0.0f, 0.5f, 1.0f); }
    static constexpr Color Pink()        { return Color(1.0f, 0.75f, 0.8f, 1.0f); }
    static constexpr Color Brown()       { return Color(0.6f, 0.4f, 0.2f, 1.0f); }

    // 灰度系列
    static constexpr Color Gray()        { return Color(0.5f, 0.5f, 0.5f, 1.0f); }
    static constexpr Color LightGray()   { return Color(0.75f, 0.75f, 0.75f, 1.0f); }
    static constexpr Color DarkGray()    { return Color(0.25f, 0.25f, 0.25f, 1.0f); }

    // 特殊颜色
    static constexpr Color Transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }
    static constexpr Color Gold()        { return Color(1.0f, 0.84f, 0.0f, 1.0f); }
    static constexpr Color Silver()      { return Color(0.75f, 0.75f, 0.75f, 1.0f); }

    // UI常用颜色
    static constexpr Color UIBackground()   { return Color(0.15f, 0.15f, 0.18f, 0.95f); }
    static constexpr Color UIText()         { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
    static constexpr Color UIButtonNormal() { return Color(0.3f, 0.3f, 0.35f, 0.9f); }
    static constexpr Color UIButtonHover()  { return Color(0.4f, 0.4f, 0.5f, 0.9f); }
    static constexpr Color UIButtonPressed(){ return Color(0.2f, 0.2f, 0.25f, 0.9f); }

    // ========== 游戏专用工具函数 ==========

    // 健康值颜色（根据百分比）
    static Color HealthColor(float percent) {
        if (percent > 0.6f) return Color(0.2f, 0.8f, 0.2f, 1.0f);  // 绿色（健康）
        if (percent > 0.3f) return Color(0.9f, 0.7f, 0.2f, 1.0f);  // 黄色（受伤）
        return Color(0.9f, 0.2f, 0.2f, 1.0f);  // 红色（危险）
    }

    // 能量值颜色（蓝色系）
    static Color EnergyColor(float percent) {
        if (percent > 0.6f) return Color(0.2f, 0.5f, 1.0f, 1.0f);  // 亮蓝
        if (percent > 0.3f) return Color(0.4f, 0.4f, 0.8f, 1.0f);  // 中蓝
        return Color(0.3f, 0.3f, 0.5f, 1.0f);  // 暗蓝
    }

    // 温度颜色（从冷到热）
    static Color TemperatureColor(float temperature) {
        // temperature: 0 = 冷（蓝），0.5 = 中性（白），1 = 热（红）
        if (temperature < 0.5f) {
            return lerp(Color::Blue(), Color::White(), temperature * 2.0f);
        } else {
            return lerp(Color::White(), Color::Red(), (temperature - 0.5f) * 2.0f);
        }
    }

private:
    Math::Vec4 m_rgba;  // 使用 GLM Vec4 存储 RGBA
};

} // namespace Tina::Core

