//
// TileRenderer 实现

#include "TileRenderer.hpp"
#include <algorithm>
#include <cstring>

namespace Tina::Renderer {

using Tina::Game::TileType;

Tina::Container::Array<float,4> TileRenderer::getTileColor(TileType t) const
{
    switch (t) {
        case TileType::Grass:    return {{0.18f, 0.72f, 0.28f, 1.0f}};
        case TileType::Dirt:     return {{0.55f, 0.38f, 0.22f, 1.0f}};
        case TileType::Stone:    return {{0.55f, 0.55f, 0.58f, 1.0f}};
        case TileType::Sand:     return {{0.94f, 0.86f, 0.51f, 1.0f}};
        case TileType::Snow:     return {{0.95f, 0.95f, 0.98f, 1.0f}};
        case TileType::Ice:      return {{0.68f, 0.85f, 0.90f, 1.0f}};
        case TileType::Water:    return {{0.15f, 0.35f, 0.90f, 0.95f}};
        case TileType::Lava:     return {{0.90f, 0.25f, 0.10f, 1.0f}};
        case TileType::Coal:     return {{0.20f, 0.20f, 0.20f, 1.0f}};
        case TileType::Iron:     return {{0.60f, 0.55f, 0.50f, 1.0f}};
        case TileType::Gold:     return {{0.90f, 0.75f, 0.20f, 1.0f}};
        case TileType::Diamond:  return {{0.85f, 0.95f, 0.95f, 1.0f}};
        case TileType::Clay:     return {{0.72f, 0.45f, 0.30f, 1.0f}};
        case TileType::Bedrock:  return {{0.15f, 0.15f, 0.15f, 1.0f}};
        case TileType::Obsidian: return {{0.25f, 0.15f, 0.25f, 1.0f}};
        case TileType::Wood:     return {{0.45f, 0.35f, 0.25f, 1.0f}};
        case TileType::Leaves:   return {{0.25f, 0.60f, 0.30f, 1.0f}};
        case TileType::Flower:   return {{0.85f, 0.40f, 0.70f, 1.0f}};
        case TileType::Grass_Decoration: return {{0.25f, 0.85f, 0.25f, 1.0f}};
        case TileType::Mushroom: return {{0.80f, 0.35f, 0.30f, 1.0f}};
        case TileType::Crystal:  return {{0.85f, 0.95f, 1.0f, 1.0f}};
        case TileType::Rock:     return {{0.45f, 0.45f, 0.48f, 1.0f}};
        default:                 return {{0.0f,  0.0f,  0.0f,  0.0f}};
    }
}

void TileRenderer::renderSolid(const Tina::Game::TileMap& map,
                               uint16_t viewId,
                               bgfx::ProgramHandle program,
                               const bgfx::VertexLayout& layout) const
{
    const int W = map.width(), H = map.height();
    const int maxTiles = W * H;
    const uint32_t maxV = (uint32_t)maxTiles * 4;
    const uint32_t maxI = (uint32_t)maxTiles * 6;

    bgfx::TransientVertexBuffer tvb; bgfx::TransientIndexBuffer tib;
    if (bgfx::getAvailTransientVertexBuffer(maxV, layout) < maxV ||
        bgfx::getAvailTransientIndexBuffer(maxI) < maxI) return;

    bgfx::allocTransientVertexBuffer(&tvb, maxV, layout);
    bgfx::allocTransientIndexBuffer(&tib, maxI);
    ColorVertex* vptr = (ColorVertex*)tvb.data; uint16_t* iptr = (uint16_t*)tib.data;
    uint32_t vb = 0, ib = 0;

    for (int y = H - 1; y >= 0; --y) for (int x = 0; x < W; ++x) {
        TileType t = map.get(x, y);
        if (t == TileType::Air || t == TileType::Water || t == TileType::Lava) continue;
        auto c4 = getTileColor(t);
        float x0 = (float)x, y0 = (float)y, x1 = x0 + 1.0f, y1 = y0 + 1.0f;
        appendQuad(vptr, iptr, vb, ib, x0, y0, x1, y1, c4[0], c4[1], c4[2], c4[3]);
    }

    if (ib > 0) {
        tvb.size = vb * sizeof(ColorVertex); tib.size = ib * sizeof(uint16_t);
        bgfx::Encoder* enc = bgfx::begin();
        enc->setVertexBuffer(0, &tvb);
        enc->setIndexBuffer(&tib);
        enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        enc->submit(viewId, program);
        bgfx::end(enc);
    }
}

void TileRenderer::renderWater(const Tina::Game::TileMap& map,
                               uint16_t viewId,
                               bgfx::ProgramHandle program,
                               const bgfx::VertexLayout& layout) const
{
    const int W = map.width(), H = map.height();
    const int maxTiles = W * H;
    const uint32_t maxV = (uint32_t)maxTiles * 4;
    const uint32_t maxI = (uint32_t)maxTiles * 6;

    bgfx::TransientVertexBuffer tvb; bgfx::TransientIndexBuffer tib;
    if (bgfx::getAvailTransientVertexBuffer(maxV, layout) < maxV ||
        bgfx::getAvailTransientIndexBuffer(maxI) < maxI) return;

    bgfx::allocTransientVertexBuffer(&tvb, maxV, layout);
    bgfx::allocTransientIndexBuffer(&tib, maxI);
    ColorVertex* vptr = (ColorVertex*)tvb.data; uint16_t* iptr = (uint16_t*)tib.data;
    uint32_t vb = 0, ib = 0;

    auto cw = getTileColor(TileType::Water);
    for (int y = H - 1; y >= 0; --y) for (int x = 0; x < W; ++x) {
        int wv = (int)map.water(x, y); if (wv <= 0) continue;
        float hfrac = (float)map.water(x, y) / 255.0f; if (hfrac <= 0.01f) continue;
        float x0 = (float)x, y0 = (float)y, x1 = x0 + 1.0f; float yh = y0 + std::min(1.0f, hfrac);
        float alphaW = std::min(1.0f, std::max(0.25f, cw[3] * (0.6f + 0.4f * hfrac)));
        appendQuad(vptr, iptr, vb, ib, x0, y0, x1, yh, cw[0], cw[1], cw[2], alphaW);
    }

    if (ib > 0) {
        tvb.size = vb * sizeof(ColorVertex); tib.size = ib * sizeof(uint16_t);
        bgfx::Encoder* enc = bgfx::begin();
        enc->setVertexBuffer(0, &tvb);
        enc->setIndexBuffer(&tib);
        enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        enc->submit(viewId, program);
        bgfx::end(enc);
    }
}

// 玩家渲染已迁移至 ECS 的 CharacterRenderSystem

} // namespace Tina::Renderer
