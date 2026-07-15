//
// SpriteRenderer 实现

#include "SpriteRenderer.hpp"
#include "ShaderManager.hpp"
#include "ShaderCatalog.hpp"

namespace Tina::Renderer {

SpriteRenderer::~SpriteRenderer()
{
    shutdown();
}

bool SpriteRenderer::initialize(ShaderManager& shaders)
{
    shutdown();
    m_prog = ShaderCatalog::Load(shaders, ShaderCatalog::Tag::Sprite);
    if (!bgfx::isValid(m_prog)) return false;

    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
    .end();

    m_sTex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    return bgfx::isValid(m_sTex);
}

void SpriteRenderer::shutdown()
{
    if (bgfx::isValid(m_sTex)) {
        bgfx::destroy(m_sTex);
        m_sTex = BGFX_INVALID_HANDLE;
    }
    // Program 由 ShaderManager 统一拥有。
    m_prog = BGFX_INVALID_HANDLE;
}

void SpriteRenderer::draw(uint16_t viewId,
                          bgfx::TextureHandle tex,
                          float x, float y, float w, float h,
                          float r, float g, float b, float a) const
{
    if (!bgfx::isValid(m_prog) || !bgfx::isValid(tex) || w <= 0.0f || h <= 0.0f) return;

    struct Vtx { float x,y,z,u,v; float r,g,b,a; };
    Vtx v[4] = {
        { x,     y,     0.0f, 0.0f, 0.0f, r,g,b,a },
        { x + w, y,     0.0f, 1.0f, 0.0f, r,g,b,a },
        { x + w, y + h, 0.0f, 1.0f, 1.0f, r,g,b,a },
        { x,     y + h, 0.0f, 0.0f, 1.0f, r,g,b,a },
    };
    uint16_t idx[6] = { 0,1,2, 0,2,3 };

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer  tib;
    if (!bgfx::allocTransientBuffers(&tvb, m_layout, 4, &tib, 6)) return;
    memcpy(tvb.data, v, sizeof(v));
    memcpy(tib.data, idx, sizeof(idx));

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
    bgfx::setState(state);
    bgfx::setTexture(0, m_sTex, tex);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::submit(viewId, m_prog);
}

} // namespace Tina::Renderer

