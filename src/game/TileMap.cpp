//
// 简易泰拉瑞亚风格地图（颜色方块）实现

#include "TileMap.hpp"
#include <cmath>

namespace Tina::Game {

// Thomas Wang 等整数哈希的变体
static inline uint32_t hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float TileMap::noise1(int x) const
{
    uint32_t n = hash_u32((uint32_t)x ^ (m_seed * 0x9e3779b1u));
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float TileMap::noise2(int x, int y) const
{
    uint32_t n = hash_u32(((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ (m_seed * 83492791u));
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

void TileMap::generate()
{
    // 地表高度：基于多频正弦 + 少量随机抖动
    const float H0 = m_h * 0.55f;
    for (int x = 0; x < m_w; ++x) {
        float h = H0;
        const float t = float(x);
        h += 8.0f * std::sinf(t * 2.0f * 3.1415926f / 64.0f);
        h += 4.0f * std::sinf(t * 2.0f * 3.1415926f / 23.0f + 1.7f);
        h += 3.0f * (noise1(x) - 0.5f) * 2.0f;
        int surface = (int)std::round(std::fmin(std::fmax(h, 8.0f), float(m_h - 8)));

        for (int y = 0; y < m_h; ++y) {
            if (y > surface) {
                set(x, y, TileType::Air);
                continue;
            }

            // 深度分层
            if (y == surface) {
                set(x, y, TileType::Grass);
            } else if (surface - y <= 6) {
                set(x, y, TileType::Dirt);
            } else {
                set(x, y, TileType::Stone);
            }
        }

        // 洞穴：对地表下方进行稀疏挖空
        for (int y = surface - 2; y >= 2; --y) {
            float n = noise2(x, y);
            float d = float(surface - y);
            // 越深概率略增
            float p = 0.05f + 0.0025f * d;
            if (n < p) set(x, y, TileType::Air);
        }
    }
}

} // namespace Tina::Game

