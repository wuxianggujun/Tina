//
// UIBatch - UI批处理数据结构
// 职责：定义批处理相关的数据结构
//

#pragma once

#include <bgfx/bgfx.h>
#include "../core/Container.hpp"

namespace Tina::UI {

// 顶点结构
struct ColorVtx {
    float x, y, z;
    float r, g, b, a;
};

struct SpriteVtx {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

// 批处理结构
struct ColorBatch {
    Container::Vector<ColorVtx> vertices;
    Container::Vector<uint16_t> indices;
    uint32_t currentDepth = 0;

    void clear() {
        vertices.clear();
        indices.clear();
        currentDepth = 0;
    }

    bool canMerge(uint32_t depth) const {
        return true;  // 将在实现中根据策略判断
    }
};

struct SpriteBatch {
    Container::Vector<SpriteVtx> vertices;
    Container::Vector<uint16_t> indices;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    uint32_t currentDepth = 0;
    uint64_t stateFlags = 0;

    void clear() {
        vertices.clear();
        indices.clear();
        texture = BGFX_INVALID_HANDLE;
        currentDepth = 0;
        stateFlags = 0;
    }

    bool canMerge(bgfx::TextureHandle tex, uint32_t depth) const {
        return texture.idx == tex.idx;
    }
};

} // namespace Tina::UI