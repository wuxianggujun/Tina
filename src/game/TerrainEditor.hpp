//
// 地形编辑辅助（挖掘/放置液体 等）
// - 提供圆形挖空、放置水源等常用编辑操作
//

#pragma once

#include <algorithm>
#include <cmath>
#include "TileMap.hpp"

namespace Tina::Game {

// 圆形挖掘（将圆内的固体置为 Air），半径单位=格
inline void excavateCircle(TileMap& tilemap, float wx, float wy, float radius)
{
    if (radius <= 0.0f) return;
    const float r2 = radius * radius;
    const int W = tilemap.width();
    const int H = tilemap.height();
    int x0 = std::max(0, (int)std::floor(wx - radius));
    int y0 = std::max(0, (int)std::floor(wy - radius));
    int x1 = std::min(W - 1, (int)std::ceil(wx + radius));
    int y1 = std::min(H - 1, (int)std::ceil(wy + radius));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float cx = x + 0.5f, cy = y + 0.5f;
        float dx = cx - wx, dy = cy - wy;
        if (dx * dx + dy * dy <= r2) tilemap.setSafe(x, y, TileType::Air);
    }
}

// 放置水源（安全写入：瓦片保持 Water/空气一致性）
inline void placeWater(TileMap& tilemap, int tx, int ty, uint8_t level = 255)
{
    if (!tilemap.isInBounds(tx, ty)) return;
    tilemap.setWaterSafe(tx, ty, level);
}

} // namespace Tina::Game

