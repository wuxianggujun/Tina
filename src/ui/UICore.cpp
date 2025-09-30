#include "UICore.hpp"
#include <bx/math.h>
#include <cstring>

namespace Tina::UI {

struct ColorVtx {
    float x, y, z;
    float r, g, b, a;
};

bool UIRenderer::initialize(Tina::renderer::ShaderManager& sm, TextRenderer* text)
{
    m_progColor = sm.loadProgram("color", "color");
    if (!bgfx::isValid(m_progColor)) return false;
    m_colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
    .end();
    m_text = text;
    return true;
}

void UIRenderer::shutdown()
{
    // 程序句柄由 ShaderManager 统一管理与清理，这里无需销毁
}

void UIRenderer::drawRect(uint16_t viewId, float x, float y, float w, float h,
                          float r, float g, float b, float a)
{
    if (!bgfx::isValid(m_progColor) || w <= 0.0f || h <= 0.0f) return;

    ColorVtx verts[4] = {
        { x,     y,     0.0f, r,g,b,a },
        { x+w,   y,     0.0f, r,g,b,a },
        { x+w,   y+h,   0.0f, r,g,b,a },
        { x,     y+h,   0.0f, r,g,b,a },
    };
    const uint16_t idx[6] = { 0,1,2, 0,2,3 };

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer  tib;
    const uint32_t vcount = 4;
    const uint32_t icount = 6;
    if (bgfx::getAvailTransientVertexBuffer(vcount, m_colorLayout) < vcount
        || bgfx::getAvailTransientIndexBuffer(icount) < icount) {
        return;
    }
    bgfx::allocTransientVertexBuffer(&tvb, vcount, m_colorLayout);
    bgfx::allocTransientIndexBuffer(&tib, icount);
    std::memcpy(tvb.data, verts, sizeof(verts));
    std::memcpy(tib.data, idx, sizeof(idx));

    bgfx::Encoder* enc = bgfx::begin();
    if (!enc) return;
    enc->setVertexBuffer(0, &tvb);
    enc->setIndexBuffer(&tib);
    enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    enc->submit(viewId, m_progColor);
    bgfx::end(enc);
}

void UIRenderer::drawText(uint16_t viewId, float x, float y,
                          float r, float g, float b, float a,
                          const std::string& utf8)
{
    if (!m_text) return;
    m_text->drawText(viewId, x, y, r, g, b, a, utf8.c_str());
}

} // namespace Tina::UI
