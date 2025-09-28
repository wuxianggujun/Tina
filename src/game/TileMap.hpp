//
// 简易泰拉瑞亚风格地图（颜色方块）
// - 仅生成类型栅格与颜色映射，不含纹理
// - 采用列高地形 + 简单洞穴噪声

#pragma once

#include "../core/Container.hpp"
#include <cstdint>

namespace Tina::Game {

enum class TileType : uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
};

struct TileMapConfig {
    int width = 256;   // 栅格宽（列数）
    int height = 128;  // 栅格高（行数）
    uint32_t seed = 1337;
};

class TileMap {
public:
    explicit TileMap(const TileMapConfig& cfg)
        : m_w(cfg.width), m_h(cfg.height), m_seed(cfg.seed), m_tiles(m_w * m_h, TileType::Air) {}

    int width() const { return m_w; }
    int height() const { return m_h; }

    TileType get(int x, int y) const { return m_tiles[index(x,y)]; }
    void set(int x, int y, TileType t) { m_tiles[index(x,y)] = t; }

    // 生成：
    // - 使用多频正弦形成起伏地表
    // - 表层1格为 Grass，其下若干为 Dirt，再深为 Stone
    // - 简单洞穴：对地下区域使用哈希噪声阈值挖空
    void generate();

private:
    int index(int x, int y) const { return y * m_w + x; }

    // 简单整型哈希噪声，返回 0..1
    float noise1(int x) const;
    float noise2(int x, int y) const;

private:
    int m_w;
    int m_h;
    uint32_t m_seed;
    Tina::Container::Vector<TileType> m_tiles;
};

} // namespace Tina::Game

