//
// 2D地图专用噪声生成器 - 为游戏地图生成提供高层次接口
// 封装柏林噪声的复杂参数，提供语义化的地图生成方法
//

#pragma once

#include "Noise.hpp"
#include <cstdint>

namespace Tina::Core {

// 噪声层配置 - 用于配置单个噪声层的参数
struct NoiseLayerConfig {
    float scale = 1.0f;        // 噪声缩放（影响频率）
    int octaves = 4;           // 噪声层数
    float persistence = 0.5f;  // 持久度（每层振幅衰减）
    float lacunarity = 2.0f;   // 间隙度（每层频率增长）
    float amplitude = 1.0f;    // 整体振幅
    
    NoiseLayerConfig() = default;
    NoiseLayerConfig(float s, int o, float p, float l = 2.0f, float a = 1.0f)
        : scale(s), octaves(o), persistence(p), lacunarity(l), amplitude(a) {}
};

// 2D地图噪声配置
struct Map2DNoiseConfig {
    // 地形高度配置
    NoiseLayerConfig terrainContinental{0.001f, 3, 0.6f};  // 大陆形状
    NoiseLayerConfig terrainMain{0.01f, 6, 0.5f};          // 主要地形
    NoiseLayerConfig terrainDetail{0.1f, 4, 0.4f};         // 地形细节
    
    // 生物群系配置
    NoiseLayerConfig temperature{0.005f, 3, 0.4f};         // 温度噪声
    NoiseLayerConfig humidity{0.008f, 4, 0.5f};            // 湿度噪声
    
    // 洞穴配置
    NoiseLayerConfig caveLarge{0.015f, 3, 0.6f};           // 大型洞穴
    NoiseLayerConfig caveMedium{0.03f, 4, 0.4f};           // 中型洞穴
    NoiseLayerConfig caveRegion{0.008f, 2, 0.5f};          // 洞穴区域
    
    // 矿物配置
    NoiseLayerConfig ore{0.03f, 4, 0.6f};                  // 矿物分布
    
    // 装饰配置
    NoiseLayerConfig decoration{0.05f, 3, 0.5f};           // 地表装饰
    NoiseLayerConfig vegetation{0.02f, 4, 0.45f};          // 植被分布
};

/**
 * 2D地图专用噪声生成器
 * 
 * 为2D地图生成提供高层次、语义化的接口
 * 隐藏底层柏林噪声的复杂参数，让游戏开发更直观
 * 
 * 使用示例:
 *   Map2DNoise noise(seed);
 *   float height = noise.getTerrainHeight(x, y);  // 返回 [0, 1]
 *   float temp = noise.getTemperature(x, y);      // 返回 [0, 1]
 */
class Map2DNoise {
public:
    explicit Map2DNoise(uint32_t seed = 0);
    explicit Map2DNoise(uint32_t seed, const Map2DNoiseConfig& config);
    
    // === 地形生成 ===
    
    /**
     * 获取地形高度值
     * @param x 世界X坐标
     * @param y 世界Y坐标（可选，用于3D或额外变化）
     * @return 高度值 [0, 1]，0为最低，1为最高
     */
    float getTerrainHeight(float x, float y = 0.0f) const;
    
    /**
     * 获取地形高度的详细分层数据
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @param outContinental 输出大陆层高度 [-1, 1]
     * @param outMain 输出主要地形层高度 [-1, 1]
     * @param outDetail 输出细节层高度 [-1, 1]
     * @return 组合后的最终高度 [0, 1]
     */
    float getTerrainHeightDetailed(float x, float y, 
                                   float& outContinental, 
                                   float& outMain, 
                                   float& outDetail) const;
    
    // === 生物群系 ===
    
    /**
     * 获取温度值
     * @param x 世界X坐标
     * @param y 世界Y坐标（通常代表纬度）
     * @return 温度值 [0, 1]，0为最冷，1为最热
     */
    float getTemperature(float x, float y) const;
    
    /**
     * 获取湿度值
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @return 湿度值 [0, 1]，0为最干燥，1为最湿润
     */
    float getHumidity(float x, float y) const;
    
    // === 洞穴生成 ===
    
    /**
     * 获取洞穴密度值
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @return 洞穴密度 [0, 1]，值越高越可能是洞穴
     */
    float getCaveDensity(float x, float y) const;
    
    /**
     * 判断指定位置是否应该生成洞穴
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @param threshold 阈值 [0, 1]，默认0.5
     * @return true表示应该生成洞穴
     */
    bool shouldGenerateCave(float x, float y, float threshold = 0.5f) const;
    
    // === 矿物生成 ===
    
    /**
     * 获取矿物密度值
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @param oreType 矿物类型（用于生成不同的噪声模式）
     * @return 矿物密度 [0, 1]
     */
    float getOreDensity(float x, float y, int oreType) const;
    
    /**
     * 判断指定位置是否应该生成矿物
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @param oreType 矿物类型
     * @param threshold 阈值 [0, 1]
     * @return true表示应该生成矿物
     */
    bool shouldGenerateOre(float x, float y, int oreType, float threshold) const;
    
    // === 地表装饰 ===
    
    /**
     * 获取装饰密度值（用于生成花朵、石块等）
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @return 装饰密度 [0, 1]
     */
    float getDecorationDensity(float x, float y) const;
    
    /**
     * 获取植被密度值（用于生成树木、草丛等）
     * @param x 世界X坐标
     * @param y 世界Y坐标
     * @return 植被密度 [0, 1]
     */
    float getVegetationDensity(float x, float y) const;
    
    // === 通用噪声访问 ===
    
    /**
     * 获取原始2D柏林噪声值
     * @param x X坐标
     * @param y Y坐标
     * @return 噪声值 [-1, 1]
     */
    float getRawNoise(float x, float y) const;
    
    /**
     * 获取分数布朗运动噪声（自定义参数）
     * @param x X坐标
     * @param y Y坐标
     * @param config 噪声层配置
     * @return 噪声值 [-1, 1]
     */
    float getFBM(float x, float y, const NoiseLayerConfig& config) const;
    
    // === 配置访问 ===
    
    const Map2DNoiseConfig& getConfig() const { return m_config; }
    void setConfig(const Map2DNoiseConfig& config) { m_config = config; }
    
    uint32_t getSeed() const { return m_seed; }

private:
    PerlinNoise m_perlin;
    uint32_t m_seed;
    Map2DNoiseConfig m_config;
    
    // 辅助函数：将 [-1, 1] 归一化到 [0, 1]
    static float normalize(float value) { return (value + 1.0f) * 0.5f; }
    
    // 辅助函数：Thomas Wang 哈希（用于矿物偏移）
    uint32_t hash(uint32_t x) const;
};

} // namespace Tina::Core
