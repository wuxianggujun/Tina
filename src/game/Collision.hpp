//
// 通用碰撞/地块查询工具
//

#pragma once

#include "TileMap.hpp"
#include <cmath>

namespace Tina::Game::Collision {

// 判定地图坐标是否为“实心”
inline bool IsSolid(int tx, int ty, const TileMap& tilemap)
{
    if (tx < 0 || ty < 0 || tx >= tilemap.width() || ty >= tilemap.height()) {
        return true; // 地图外视为实心
    }
    TileType t = tilemap.get(tx, ty);
    return tilemap.isSolidTile(t);
}

// AABB 与地块的碰撞检测
inline bool CheckAABB(float nx, float ny, float width, float height, const TileMap& tilemap)
{
    const float EPS = 1e-6f;
    float left   = nx;
    float right  = nx + width  - EPS;
    float bottom = ny;
    float top    = ny + height - EPS;
    int x0 = (int)std::floor(left);
    int y0 = (int)std::floor(bottom);
    int x1 = (int)std::floor(right);
    int y1 = (int)std::floor(top);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (IsSolid(x, y, tilemap)) {
                float tileL = (float)x;
                float tileR = (float)(x + 1);
                float tileB = (float)y;
                float tileT = (float)(y + 1);

                float aabbL = left;
                float aabbR = right + EPS;
                float aabbB = bottom;
                float aabbT = top + EPS;

                if (aabbR > tileL && aabbL < tileR && aabbT > tileB && aabbB < tileT) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace Tina::Game::Collision
