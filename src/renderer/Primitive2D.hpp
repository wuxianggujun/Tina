//
// Primitive2D - 简易 2D 形状渲染器（集中管理着色器与顶点布局）
// - 提供纯色矩形/全屏遮罩/竖向渐变矩形等常用绘制
// - 由 Application 在初始化后创建并持有，场景直接调用，不再重复加载 shader 或创建顶点布局

#pragma once

#include <bgfx/bgfx.h>

namespace Tina { namespace Renderer { class ShaderManager; } }

namespace Tina::Renderer {

class Primitive2D {
public:
    Primitive2D();
    ~Primitive2D();

    // 初始化：从 ShaderManager 加载所需 Program，并创建顶点布局
    bool initialize(ShaderManager& shaders);

    // 设置 UI 正交投影（y 轴向下），配合 UI 视图使用
    void setOrtho(uint16_t viewId, float width, float height);

    // 纯色矩形
    void drawSolidRect(uint16_t viewId, float x, float y, float w, float h, uint32_t abgr, bool alphaBlend = true);

    // 全屏矩形（0,0 到 w,h）
    void drawFullscreen(uint16_t viewId, float width, float height, uint32_t abgr, bool alphaBlend = true);

    // 竖向渐变矩形（上/下颜色）
    void drawVerticalGradient(uint16_t viewId, float x, float y, float w, float h, uint32_t abgrTop, uint32_t abgrBottom);

private:
    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_posColorLayout;
};

} // namespace Tina::Renderer

