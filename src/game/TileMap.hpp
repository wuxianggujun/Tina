//
// 简易泰拉瑞亚风格地图（颜色方块）
// - 仅生成类型栅格与颜色映射，不含纹理
// - 采用列高地形 + 简单洞穴噪声

#pragma once

#include "../core/Container.hpp"
#include "../core/Noise.hpp"
#include <cstdint>

namespace Tina::Game {

enum class TileType : uint8_t {
    Air = 0,
    
    // 基础地形
    Grass,
    Dirt,
    Stone,
    Sand,
    Snow,
    Ice,
    
    // 液体
    Water,
    Lava,
    
    // 矿物
    Coal,
    Iron,
    Gold,
    Diamond,
    
    // 生物群系特有
    Clay,           // 粘土
    Bedrock,        // 基岩
    Obsidian,       // 黑曜石
    
    // 植被相关  
    Wood,           // 木材
    Leaves,         // 树叶
    
    // 地下结构
    CaveWall,       // 洞穴墙壁
    
    MAX_TILE_TYPE
};

// 生物群系类型
enum class BiomeType : uint8_t {
    Ocean,          // 海洋
    Beach,          // 海滩
    Forest,         // 森林
    Plains,         // 平原
    Desert,         // 沙漠
    Tundra,         // 苔原
    Mountain,       // 山地
    Swamp,          // 沼泽
    MAX_BIOME_TYPE
};

// 矿物类型（用于生成）
enum class OreType : uint8_t {
    Coal = 0,
    Iron,
    Gold, 
    Diamond,
    MAX_ORE_TYPE
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
        , m_water(m_w * m_h, (uint8_t)0)
        , m_biomes(m_w * m_h, BiomeType::Plains)
        , m_noiseGen(cfg.seed) {}

    int width() const { return m_w; }
    int height() const { return m_h; }

    TileType get(int x, int y) const { return m_tiles[index(x,y)]; }
    void set(int x, int y, TileType t) { m_tiles[index(x,y)] = t; }
    
    // 获取生物群系
    BiomeType getBiome(int x, int y) const { 
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return BiomeType::Plains;
        return m_biomes[index(x,y)]; 
    }
    void setBiome(int x, int y, BiomeType b) { 
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return;
        m_biomes[index(x,y)] = b; 
    }
    
    // 安全的瓦片设置：同时处理瓦片类型和水位数据的一致性
    void setSafe(int x, int y, TileType t) {
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return;
        m_tiles[index(x,y)] = t;
        
        // 确保数据一致性：非水瓦片时清除水位数据
        if (t != TileType::Water) {
            m_water[index(x,y)] = 0;
        }
    }

    // 新的地图生成系统：
    // - 使用柏林噪声生成自然地形
    // - 基于温度和湿度的生物群系系统
    // - 改进的洞穴和矿物生成
    void generate();
    
    // 分步生成方法
    void generateBiomes();        // 生成生物群系
    void generateTerrain();       // 生成基础地形
    void generateCaves();         // 生成洞穴系统
    void generateOres();          // 生成矿物
    void generateVegetation();    // 生成植被
    void generateWater();         // 生成水体

    // 查询是否为水体 - 改进版本，增强一致性检查
    bool isWater(int x, int y) const {
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return false;
        // 只有在瓦片为Air且有水位数据，或者瓦片就是Water类型时才返回true
        TileType t = m_tiles[index(x,y)];
        uint8_t waterLevel = m_water[index(x,y)];
        return (t == TileType::Air && waterLevel > 0) || (t == TileType::Water);
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
    
    // 安全的水位设置：确保瓦片类型与水位数据一致
    void setWaterSafe(int x, int y, uint8_t lvl) {
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return;
        m_water[index(x,y)] = lvl;
        
        // 如果设置了水位且当前不是Water瓦片，确保瓦片为Air
        if (lvl > 0 && get(x, y) != TileType::Water && get(x, y) != TileType::Air) {
            set(x, y, TileType::Air);
        }
        // 如果清除了水位且瓦片是Air，保持Air状态（正常）
    }

    // 水体元胞自动机更新：执行若干次水流步进，输出改变的 AABB（若无改变则 outMinX>outMaxX）
    bool stepWater(int iterations, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY);

    // 新增：更智能的水流更新，支持压力传播和流速计算
    bool stepWaterAdvanced(int iterations, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY);

private:
    int index(int x, int y) const { return y * m_w + x; }

    // 生物群系确定
    BiomeType determineBiome(float temperature, float humidity, float height) const;
    
    // 根据生物群系获取地表材料
    TileType getSurfaceMaterial(BiomeType biome) const;
    TileType getSubsurfaceMaterial(BiomeType biome, int depth) const;
    
    // 矿物生成辅助
    bool shouldGenerateOre(OreType ore, int x, int y, float caveNoise) const;
    TileType oreTypeToTileType(OreType ore) const;

private:
    int m_w;
    int m_h;
    uint32_t m_seed;
    int m_seaLevel = 0; // 海平面（世界格坐标，y<=seaLevel 且为空气则填水）
    
    // 主要数据
    Tina::Container::Vector<TileType> m_tiles;
    Tina::Container::Vector<uint8_t>  m_water; // 水位（0..255）
    Tina::Container::Vector<BiomeType> m_biomes; // 生物群系
    Tina::Container::Vector<uint8_t>  m_work;  // 水体更新的工作缓冲
    
    // 噪声生成器
    Tina::Core::NoiseGenerator m_noiseGen;
    
    unsigned int m_tick = 0; // 奇偶交替打破左右偏置
};

} // namespace Tina::Game
