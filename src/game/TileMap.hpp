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
    Water,
};

struct TileMapConfig {
    int width = 256;   // 栅格宽（列数）
    int height = 128;  // 栅格高（行数）
    uint32_t seed = 1337;
};

class TileMap {
public:
    explicit TileMap(const TileMapConfig& cfg)
        : m_w(cfg.width), m_h(cfg.height), m_seed(cfg.seed)
        , m_tiles(m_w * m_h, TileType::Air)
        , m_water(m_w * m_h, (uint8_t)0) {}

    int width() const { return m_w; }
    int height() const { return m_h; }

    TileType get(int x, int y) const { return m_tiles[index(x,y)]; }
    void set(int x, int y, TileType t) { m_tiles[index(x,y)] = t; }

    // 生成：
    // - 使用多频正弦形成起伏地表
    // - 表层1格为 Grass，其下若干为 Dirt，再深为 Stone
    // - 简单洞穴：对地下区域使用哈希噪声阈值挖空
    void generate();

    // 查询是否为水体
    bool isWater(int x, int y) const {
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return false;
        return m_water[index(x,y)] > 0;
    }

    // 为水体提供一个简易流场（单位：世界单位/秒），用于对碎块施加流动力
    // 说明：采用哈希噪声生成方向场，并叠加一个微小的全局偏移（向右）。
    // 非水区域返回 {0,0}。
    void waterFlow(float wx, float wy, float& outVx, float& outVy) const;
    void waterFlowAdvanced(float wx, float wy, float& outVx, float& outVy) const;
    // 水位读写（0..255）
    uint8_t water(int x, int y) const { return m_water[index(x,y)]; }
    void setWater(int x, int y, uint8_t lvl) { m_water[index(x,y)] = lvl; }
    void addWater(int x, int y, int delta) { int v = (int)m_water[index(x,y)] + delta; if (v < 0) v = 0; if (v > 255) v = 255; m_water[index(x,y)] = (uint8_t)v; }

    // 水体元胞自动机更新：执行若干次水流步进，输出改变的 AABB（若无改变则 outMinX>outMaxX）
    bool stepWater(int iterations, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY);

    // 新增：更智能的水流更新，支持压力传播和流速计算
    bool stepWaterAdvanced(int iterations, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY);

private:
    int index(int x, int y) const { return y * m_w + x; }

    // 简单整型哈希噪声，返回 0..1
    float noise1(int x) const;
    float noise2(int x, int y) const;
    float noise2f(float x, float y) const;

private:
    int m_w;
    int m_h;
    uint32_t m_seed;
    int m_seaLevel = 0; // 海平面（世界格坐标，y<=seaLevel 且为空气则填水）
    Tina::Container::Vector<TileType> m_tiles;
    Tina::Container::Vector<uint8_t>  m_water; // 水位（0..255）
    Tina::Container::Vector<uint8_t>  m_work;  // 水体更新的工作缓冲
    unsigned int m_tick = 0; // 奇偶交替打破左右偏置
};

} // namespace Tina::Game
