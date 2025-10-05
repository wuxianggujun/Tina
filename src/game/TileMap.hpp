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
    
    // 装饰类型
    Flower,         // 花朵
    Grass_Decoration, // 草丛装饰
    Mushroom,       // 蘑菇
    Crystal,        // 水晶
    Rock,           // 小石块
    
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
        , m_lava(m_w * m_h, (uint8_t)0)
        , m_biomes(m_w * m_h, BiomeType::Plains)
        , m_noiseGen(cfg.seed) {}

    int width() const { return m_w; }
    int height() const { return m_h; }

    TileType get(int x, int y) const { return m_tiles[index(x,y)]; }
    void set(int x, int y, TileType t) { m_tiles[index(x,y)] = t; }
    
    // 辅助：边界/材质/地表
    bool isInBounds(int x, int y) const { return (unsigned)x < (unsigned)m_w && (unsigned)y < (unsigned)m_h; }
    bool isSolidTile(TileType t) const { return (t != TileType::Air && t != TileType::Water && t != TileType::Lava); }
    bool isNaturalGround(TileType t) const {
        switch (t) {
            case TileType::Grass:
            case TileType::Dirt:
            case TileType::Stone:
            case TileType::Sand:
            case TileType::Snow:
            case TileType::Ice:
            case TileType::Clay:
                return true;
            default:
                return false;
        }
    }
    int findSurfaceY(int x) const; // 从上向下找列 x 上第一个非 Air 的 y；找不到返回 -1
    
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
    void initializeBasicData();      // 初始化基础数据
    void generateBiomes();           // 生成生物群系
    void generateTerrain();          // 生成基础地形
    void generateCaves();            // 生成洞穴系统
    void generateOres();             // 生成矿物
    void generateSurfaceFeatures();  // 统一生成地表特征（植被+装饰）
    void generateWater();            // 生成水体
    void postProcessGeneration();    // 后处理优化

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
    
    // 岩浆位读写（0..255）
    uint8_t lava(int x, int y) const { 
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return 0;
        return m_lava[index(x,y)]; 
    }
    void setLava(int x, int y, uint8_t lvl) { 
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return;
        m_lava[index(x,y)] = lvl; 
    }
    void addLava(int x, int y, int delta) { 
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return;
        int v = (int)m_lava[index(x,y)] + delta; 
        if (v < 0) v = 0; 
        if (v > 255) v = 255; 
        m_lava[index(x,y)] = (uint8_t)v; 
    }
    
    // 检查是否为岩浆
    bool isLava(int x, int y) const {
        if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return false;
        TileType t = m_tiles[index(x,y)];
        uint8_t lavaLevel = m_lava[index(x,y)];
        return (t == TileType::Air && lavaLevel > 0) || (t == TileType::Lava);
    }
    
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
    // 更严格的“地表”查找：忽略树木/树叶/装饰，仅视自然地表为地面
    int findGroundSurfaceY(int x) const;

    // 新增：更智能的水流更新，支持压力传播和流速计算
    bool stepWaterAdvanced(int iterations, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY);

private:
    int index(int x, int y) const { return y * m_w + x; }

    // 生物群系确定
    BiomeType determineBiome(float temperature, float humidity, float height) const;
    
    // 根据生物群系获取地表材料
    TileType getSurfaceMaterial(BiomeType biome) const;
    TileType getSubsurfaceMaterial(BiomeType biome, int depth) const;
    TileType getSubsurfaceMaterial(BiomeType biome, int depth, int surfaceY) const;
    
    // 矿物生成辅助
    bool shouldGenerateOre(OreType ore, int x, int y, float caveNoise) const;
    TileType oreTypeToTileType(OreType ore) const;
    
    // 统一的地表特征生成辅助
    bool shouldGenerateTree(BiomeType biome, float primaryNoise, float secondaryNoise) const;
    bool shouldGenerateLargeDecoration(BiomeType biome, float decorationNoise) const;
    bool shouldGenerateSmallDecoration(BiomeType biome, float primaryNoise) const;
    
    void generateTreeForBiome(int x, int y, BiomeType biome, float primaryNoise, float secondaryNoise);
    void generateLargeDecorationForBiome(int x, int y, BiomeType biome, float decorationNoise);
    void generateSmallDecorationForBiome(int x, int y, BiomeType biome, float primaryNoise);
    
    // 后处理辅助方法
    void cleanupIsolatedBlocks();
    void smoothTerrainEdges();
    void validateLiquidConsistency();
    
    // 树木生成
    void generateTree(int x, int y, int height, bool hasLeaves = true);
    void generateBigTree(int x, int y);

private:
    int m_w;
    int m_h;
    uint32_t m_seed;
    int m_seaLevel = 0; // 海平面（世界格坐标，y<=seaLevel 且为空气则填水）
    
    // 主要数据
    Tina::Container::Vector<TileType> m_tiles;
    Tina::Container::Vector<uint8_t>  m_water; // 水位（0..255）
    Tina::Container::Vector<uint8_t>  m_lava;  // 岩浆位（0..255）
    Tina::Container::Vector<BiomeType> m_biomes; // 生物群系
    Tina::Container::Vector<uint8_t>  m_work;  // 水体更新的工作缓冲
    Tina::Container::Vector<uint8_t>  m_workLava; // 岩浆更新的工作缓冲
    
    // 噪声生成器
    Tina::Core::NoiseGenerator m_noiseGen;
    
    unsigned int m_tick = 0; // 奇偶交替打破左右偏置
};

} // namespace Tina::Game
