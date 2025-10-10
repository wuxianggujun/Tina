//
// Primitive2D 实现

#include "Primitive2D.hpp"
#include "ShaderManager.hpp"
#include "ShaderCatalog.hpp"
#include <bx/math.h>

namespace Tina::Renderer {

Primitive2D::Primitive2D() = default;
Primitive2D::~Primitive2D() = default;

bool Primitive2D::initialize(ShaderManager& shaders)
{
    // 使用逻辑目录加载 UI 基础程序（当前映射到内置 color，可随时替换）
    m_progColor = ShaderCatalog::Load(shaders, ShaderCatalog::Tag::UiSolid);

    // 顶点布局：位置 + 颜色
    m_posColorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
        .end();

    return bgfx::isValid(m_progColor);
}

void Primitive2D::setOrtho(uint16_t viewId, float width, float height)
{
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho,
                 0.0f, width,
                 height, 0.0f,
                 -1.0f, 1.0f,
                 0.0f,
                 caps ? caps->homogeneousDepth : false);
    bgfx::setViewTransform(viewId, nullptr, ortho);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
}

void Primitive2D::drawSolidRect(uint16_t viewId, float x, float y, float w, float h, uint32_t abgr, bool alphaBlend)
{
    struct V { float x,y,z; uint32_t abgr; };
    V vertices[6] = {
        {x,     y,     0.0f, abgr},
        {x + w, y,     0.0f, abgr},
        {x + w, y + h, 0.0f, abgr},
        {x,     y,     0.0f, abgr},
        {x + w, y + h, 0.0f, abgr},
        {x,     y + h, 0.0f, abgr},
    };

    if (bgfx::getAvailTransientVertexBuffer(6, m_posColorLayout) == 6) {
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 6, m_posColorLayout);
        bx::memCopy(tvb.data, vertices, sizeof(vertices));

        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        if (alphaBlend) state |= BGFX_STATE_BLEND_ALPHA;
        bgfx::setState(state);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::submit(viewId, m_progColor);
    }
}

void Primitive2D::drawFullscreen(uint16_t viewId, float width, float height, uint32_t abgr, bool alphaBlend)
{
    drawSolidRect(viewId, 0.0f, 0.0f, width, height, abgr, alphaBlend);
}

void Primitive2D::drawVerticalGradient(uint16_t viewId, float x, float y, float w, float h, uint32_t abgrTop, uint32_t abgrBottom)
{
    struct V { float x,y,z; uint32_t abgr; };
    V vertices[6] = {
        {x,     y,     0.0f, abgrTop},
        {x + w, y,     0.0f, abgrTop},
        {x + w, y + h, 0.0f, abgrBottom},
        {x,     y,     0.0f, abgrTop},
        {x + w, y + h, 0.0f, abgrBottom},
        {x,     y + h, 0.0f, abgrBottom},
    };

    if (bgfx::getAvailTransientVertexBuffer(6, m_posColorLayout) == 6) {
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 6, m_posColorLayout);
        bx::memCopy(tvb.data, vertices, sizeof(vertices));

        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        bgfx::setState(state);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::submit(viewId, m_progColor);
    }
}

} // namespace Tina::Renderer
