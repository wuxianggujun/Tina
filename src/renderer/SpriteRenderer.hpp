//
// SpriteRenderer - 简易 2D 精灵渲染器（世界或工具用）
// - 使用 sprite_vs/sprite_fs 与 Position+Texcoord0+Color 布局

#pragma once

#include <bgfx/bgfx.h>

namespace Tina::Renderer { class ShaderManager; }

namespace Tina::Renderer {

class SpriteRenderer {
public:
    SpriteRenderer() = default;
    ~SpriteRenderer();

    bool initialize(ShaderManager& shaders);
    void shutdown();

    // 渲染一张精灵（左上坐标 + 尺寸，颜色乘法）
    void draw(uint16_t viewId,
              bgfx::TextureHandle tex,
              float x, float y, float w, float h,
              float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) const;

private:
    bgfx::ProgramHandle m_prog = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_layout{};   // Position, Texcoord0, Color
    bgfx::UniformHandle m_sTex = BGFX_INVALID_HANDLE;
};

} // namespace Tina::Renderer

