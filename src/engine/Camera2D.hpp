//
// Camera2D - 简易 2D 相机（引擎层）
// - 世界单位：以 tile=1.0 计量
// - 视口像素决定宽高比，视区高度（世界单位）可配置
// - 提供生成 bgfx 视图/投影矩阵（正交，原点左下，y 向上）

#pragma once

#include <cstdint>

namespace Tina::Engine {

class Camera2D {
public:
    void setViewportPixels(int w, int h) { m_vpW = (w>0?w:1); m_vpH = (h>0?h:1); }
    void setViewHeightWorld(float h) { m_viewH = (h > 0.001f ? h : 0.001f); }

    void setPosition(float x, float y) { m_x = x; m_y = y; }
    void moveBy(float dx, float dy) { m_x += dx; m_y += dy; }

    float x() const { return m_x; }
    float y() const { return m_y; }
    int vpW() const { return m_vpW; }
    int vpH() const { return m_vpH; }
    float viewH() const { return m_viewH; }
    float viewW() const { return m_viewH * (float)m_vpW / (float)m_vpH; }

    // 生成视图/投影矩阵（供 bgfx::setViewTransform 使用）
    void buildViewProj(float* outView16, float* outProj16) const;

private:
    float m_x = 0.0f, m_y = 0.0f; // 相机左下角在世界坐标中的位置
    int m_vpW = 1, m_vpH = 1;     // 视口像素尺寸
    float m_viewH = 30.0f;        // 视区高度（世界单位）
};

} // namespace Tina::Engine

