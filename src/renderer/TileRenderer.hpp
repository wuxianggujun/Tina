//
// 瓦片/水体/玩家 渲染封装
// - 减少 main.cpp 内重复网格构建与渲染代码
//

#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>
#include "../game/TileMap.hpp"
#include "../game/Player.hpp"
#include "../core/Container.hpp"

namespace Tina::Renderer {

struct ColorVertex { float x, y, z; float r, g, b, a; };

class TileRenderer {
public:
    void initialize() {}

    // 从原 main.cpp 迁移：根据 TileType 获取颜色（r,g,b,a）
    Tina::Container::Array<float,4> getTileColor(Tina::Game::TileType t) const;

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

    // 渲染玩家 AABB（默认蓝色半透明，可传自定义颜色）
    void renderPlayer(const Tina::Game::Player& player,
                      uint16_t viewId,
                      bgfx::ProgramHandle program,
                      const bgfx::VertexLayout& layout,
                      float r = 0.3f, float g = 0.5f,
                      float b = 0.9f, float a = 0.95f) const;
};

} // namespace Tina::Renderer

