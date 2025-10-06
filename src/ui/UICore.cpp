#include "UICore.hpp"
#include <bx/math.h>
#include <cstring>
#include "../core/Color.hpp"

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

void UIRenderer::drawRect(uint16_t viewId, float x, float y, float w, float h,
                          const Tina::Core::Color& color)
{
    drawRect(viewId, x, y, w, h, color.r(), color.g(), color.b(), color.a());
}

void UIRenderer::drawText(uint16_t viewId, float x, float y,
                          float r, float g, float b, float a,
                          const std::string& utf8)
{
    if (!m_text) return;
    m_text->drawText(viewId, x, y, r, g, b, a, utf8.c_str());
}

void UIRenderer::drawText(uint16_t viewId, float x, float y,
                          const Tina::Core::Color& color,
                          const std::string& utf8)
{
    drawText(viewId, x, y, color.r(), color.g(), color.b(), color.a(), utf8);
}

void UIRenderer::drawTextEx(uint16_t viewId, float x, float y, float w, float h,
                            float r, float g, float b, float a,
                            const std::string& utf8,
                            AlignH halign, AlignV valign,
                            float padX, float padY)
{
    if (!m_text || w <= 0.0f || h <= 0.0f) return;
    float tw=0.0f, th=0.0f, tTop=0.0f, tBottom=0.0f;
    m_text->measureTextExtents(utf8, tw, th, tTop, tBottom);

    // 水平位置
    float textX = x + padX;
    if (halign == AlignH::Center) textX = x + (w - tw)*0.5f;
    else if (halign == AlignH::Right) textX = x + w - padX - tw;

    // 垂直位置（基线）
    float baselineY = y + padY + tTop; // Top 对齐默认
    if (valign == AlignV::Center) {
        baselineY = y + h*0.5f + (tTop - tBottom)*0.5f;
    } else if (valign == AlignV::Bottom) {
        baselineY = y + h - padY - tBottom;
    } else if (valign == AlignV::Baseline) {
        baselineY = y + h - padY; // 将矩形底边作为基线
    }

    float textY = baselineY - (float)m_text->ascenderPx();
    m_text->drawText(viewId, textX, textY, r, g, b, a, utf8);
}

void UIRenderer::drawTextEx(uint16_t viewId, float x, float y, float w, float h,
                            const Tina::Core::Color& color,
                            const std::string& utf8,
                            AlignH halign, AlignV valign,
                            float padX, float padY)
{
    drawTextEx(viewId, x, y, w, h, color.r(), color.g(), color.b(), color.a(),
               utf8, halign, valign, padX, padY);
}

} // namespace Tina::UI
