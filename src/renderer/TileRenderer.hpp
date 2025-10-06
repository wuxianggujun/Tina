//
// 瓦片/水体/玩家 渲染封装
// - 减少 main.cpp 内重复网格构建与渲染代码
//

#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>
#include "../game/TileMap.hpp"
#include "../core/Container.hpp"
#include "../core/Color.hpp"

namespace Tina::Renderer {

struct ColorVertex { float x, y, z; float r, g, b, a; };

class TileRenderer {
public:
    void initialize() {}

    // 从原 main.cpp 迁移：根据 TileType 获取颜色
    Tina::Core::Color getTileColor(Tina::Game::TileType t) const;

    // 渲染固体瓦片（整格）
    void renderSolid(const Tina::Game::TileMap& map,
                     uint16_t viewId,
                     bgfx::ProgramHandle program,
                     const bgfx::VertexLayout& layout) const;

    // 渲染水体（按水位分数渲染部分高度，启用透明混合）
    void renderWater(const Tina::Game::TileMap& map,
                     uint16_t viewId,
                     bgfx::ProgramHandle program,
                     const bgfx::VertexLayout& layout) const;

private:
    static inline void appendQuad(ColorVertex* vptr, uint16_t* iptr,
                                  uint32_t& vb, uint32_t& ib,
                                  float x0, float y0, float x1, float y1,
                                  float r, float g, float b, float a)
    {
        vptr[vb+0] = { x0,y0,0.0f, r,g,b,a };
        vptr[vb+1] = { x1,y0,0.0f, r,g,b,a };
        vptr[vb+2] = { x1,y1,0.0f, r,g,b,a };
        vptr[vb+3] = { x0,y1,0.0f, r,g,b,a };
        iptr[ib+0] = (uint16_t)(vb+0);
        iptr[ib+1] = (uint16_t)(vb+1);
        iptr[ib+2] = (uint16_t)(vb+2);
        iptr[ib+3] = (uint16_t)(vb+0);
        iptr[ib+4] = (uint16_t)(vb+2);
        iptr[ib+5] = (uint16_t)(vb+3);
        vb += 4; ib += 6;
    }
};

} // namespace Tina::Renderer
