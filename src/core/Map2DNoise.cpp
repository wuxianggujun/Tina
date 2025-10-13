//
// 2D地图专用噪声生成器实现
//

#include "Map2DNoise.hpp"
#include <cmath>
#include <algorithm>

namespace Tina::Core {

Map2DNoise::Map2DNoise(uint32_t seed)
    : m_perlin(seed)
    , m_seed(seed)
    , m_config()
{
}

Map2DNoise::Map2DNoise(uint32_t seed, const Map2DNoiseConfig& config)
    : m_perlin(seed)
    , m_seed(seed)
    , m_config(config)
{
}

// === 地形生成 ===

float Map2DNoise::getTerrainHeight(float x, float y) const
{
    const auto& cfg = m_config;
    
    // 大陆形状层 - 控制大尺度地形起伏
    float continental = m_perlin.fbm2D(
        x * cfg.terrainContinental.scale,
        y * cfg.terrainContinental.scale,
        cfg.terrainContinental.octaves,
        cfg.terrainContinental.persistence
    ) * cfg.terrainContinental.amplitude;
    
    // 主要地形层 - 控制山脉、丘陵等中等尺度特征
    float main = m_perlin.fbm2D(
        x * cfg.terrainMain.scale,
        y * cfg.terrainMain.scale,
        cfg.terrainMain.octaves,
        cfg.terrainMain.persistence
    ) * cfg.terrainMain.amplitude;
    
    // 细节层 - 添加小尺度细节
    float detail = m_perlin.fbm2D(
        x * cfg.terrainDetail.scale,
        y * cfg.terrainDetail.scale,
        cfg.terrainDetail.octaves,
        cfg.terrainDetail.persistence
    ) * cfg.terrainDetail.amplitude;
    
    // 组合不同层次的噪声（权重可配置）
    float height = continental * 0.6f + main * 0.3f + detail * 0.1f;
    
    // 归一化到 [0, 1]
    return normalize(height);
}

float Map2DNoise::getTerrainHeightDetailed(float x, float y,
                                          float& outContinental,
                                          float& outMain,
                                          float& outDetail) const
{
    const auto& cfg = m_config;
    
    outContinental = m_perlin.fbm2D(
        x * cfg.terrainContinental.scale,
        y * cfg.terrainContinental.scale,
        cfg.terrainContinental.octaves,
        cfg.terrainContinental.persistence
    ) * cfg.terrainContinental.amplitude;
    
    outMain = m_perlin.fbm2D(
        x * cfg.terrainMain.scale,
        y * cfg.terrainMain.scale,
        cfg.terrainMain.octaves,
        cfg.terrainMain.persistence
    ) * cfg.terrainMain.amplitude;
    
    outDetail = m_perlin.fbm2D(
        x * cfg.terrainDetail.scale,
        y * cfg.terrainDetail.scale,
        cfg.terrainDetail.octaves,
        cfg.terrainDetail.persistence
    ) * cfg.terrainDetail.amplitude;
    
    float height = outContinental * 0.6f + outMain * 0.3f + outDetail * 0.1f;
    return normalize(height);
}

// === 生物群系 ===

float Map2DNoise::getTemperature(float x, float y) const
{
    const auto& cfg = m_config.temperature;
    
    // 温度主要受纬度影响（y坐标）
    // 假设 y=0 是赤道，y值越大越接近极地
    float latitudeFactor = 1.0f - std::abs(y * 0.002f);
    
    // 添加噪声变化，模拟局部气候差异
    float tempNoise = m_perlin.fbm2D(
        x * cfg.scale,
        y * cfg.scale,
        cfg.octaves,
        cfg.persistence
    ) * cfg.amplitude;
    
    // 组合纬度因素和噪声（70%纬度 + 30%噪声）
    float temp = latitudeFactor * 0.7f + tempNoise * 0.3f;
    
    // 归一化并限制到 [0, 1]
    return std::max(0.0f, std::min(1.0f, normalize(temp)));
}

float Map2DNoise::getHumidity(float x, float y) const
{
    const auto& cfg = m_config.humidity;
    
    // 湿度噪声，与温度相对独立
    // 添加偏移以避免与温度噪声相关
    float humidity = m_perlin.fbm2D(
        x * cfg.scale + 1000.0f,  // 偏移避免与温度相关
        y * cfg.scale + 1000.0f,
        cfg.octaves,
        cfg.persistence
    ) * cfg.amplitude;
    
    return normalize(humidity);
}

// === 洞穴生成 ===

float Map2DNoise::getCaveDensity(float x, float y) const
{
    const auto& cfg = m_config;
    
    // 大尺度洞穴系统
    float largeCaves = m_perlin.turbulence2D(
        x * cfg.caveLarge.scale,
        y * cfg.caveLarge.scale,
        cfg.caveLarge.octaves,
        cfg.caveLarge.persistence
    ) * cfg.caveLarge.amplitude;
    
    // 中等尺度洞穴细节
    float mediumCaves = m_perlin.turbulence2D(
        x * cfg.caveMedium.scale + 500.0f,
        y * cfg.caveMedium.scale + 500.0f,
        cfg.caveMedium.octaves,
        cfg.caveMedium.persistence
    ) * cfg.caveMedium.amplitude;
    
    // 洞穴区域控制（更大的连续区域）
    float caveRegion = m_perlin.fbm2D(
        x * cfg.caveRegion.scale + 1000.0f,
        y * cfg.caveRegion.scale + 1000.0f,
        cfg.caveRegion.octaves,
        cfg.caveRegion.persistence
    ) * cfg.caveRegion.amplitude;
    
    // 组合不同尺度的洞穴噪声
    float density = largeCaves * 0.5f + mediumCaves * 0.3f + normalize(caveRegion) * 0.2f;
    
    return density;
}

bool Map2DNoise::shouldGenerateCave(float x, float y, float threshold) const
{
    return getCaveDensity(x, y) > threshold;
}

// === 矿物生成 ===

uint32_t Map2DNoise::hash(uint32_t x) const
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float Map2DNoise::getOreDensity(float x, float y, int oreType) const
{
    const auto& cfg = m_config.ore;
    
    // 为不同矿物类型生成不同的噪声模式
    uint32_t offset = hash(static_cast<uint32_t>(oreType) * 0x9e3779b1u);
    float offsetX = static_cast<float>(offset & 0xFFFF) * 0.1f;
    float offsetY = static_cast<float>((offset >> 16) & 0xFFFF) * 0.1f;
    
    float ore = m_perlin.fbm2D(
        x * cfg.scale + offsetX,
        y * cfg.scale + offsetY,
        cfg.octaves,
        cfg.persistence
    ) * cfg.amplitude;
    
    return normalize(ore);
}

bool Map2DNoise::shouldGenerateOre(float x, float y, int oreType, float threshold) const
{
    return getOreDensity(x, y, oreType) > threshold;
}

// === 地表装饰 ===

float Map2DNoise::getDecorationDensity(float x, float y) const
{
    const auto& cfg = m_config.decoration;
    
    float decoration = m_perlin.fbm2D(
        x * cfg.scale,
        y * cfg.scale,
        cfg.octaves,
        cfg.persistence
    ) * cfg.amplitude;
    
    return normalize(decoration);
}

float Map2DNoise::getVegetationDensity(float x, float y) const
{
    const auto& cfg = m_config.vegetation;
    
    float vegetation = m_perlin.fbm2D(
        x * cfg.scale,
        y * cfg.scale,
        cfg.octaves,
        cfg.persistence
    ) * cfg.amplitude;
    
    return normalize(vegetation);
}

// === 通用噪声访问 ===

float Map2DNoise::getRawNoise(float x, float y) const
{
    return m_perlin.noise2D(x, y);
}

float Map2DNoise::getFBM(float x, float y, const NoiseLayerConfig& config) const
{
    return m_perlin.fbm2D(
        x * config.scale,
        y * config.scale,
        config.octaves,
        config.persistence
    ) * config.amplitude;
}

} // namespace Tina::Core
