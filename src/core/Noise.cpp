//
// 噪声生成工具类实现
//

#include "Noise.hpp"
#include <random>
#include <algorithm>

namespace Tina::Core {

// 静态梯度向量定义
constexpr float PerlinNoise::GRAD_3D[GRAD_SIZE][3];

PerlinNoise::PerlinNoise(uint32_t seed) {
    initPermutation(seed);
}

void PerlinNoise::initPermutation(uint32_t seed) {
    // 初始化排列表
    for (int i = 0; i < PERM_SIZE; ++i) {
        m_perm[i] = static_cast<uint8_t>(i);
    }
    
    // 使用种子打乱排列
    std::mt19937 rng(seed);
    std::shuffle(m_perm, m_perm + PERM_SIZE, rng);
    
    // 复制一次避免取模
    for (int i = 0; i < PERM_SIZE; ++i) {
        m_perm[PERM_SIZE + i] = m_perm[i];
    }
}

float PerlinNoise::grad(int hash, float x, float y) {
    int h = hash & 15;  // 取低4位
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : h == 12 || h == 14 ? x : 0;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float PerlinNoise::noise2D(float x, float y) const {
    // 计算格点坐标
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    
    // 计算相对坐标
    x -= std::floor(x);
    y -= std::floor(y);
    
    // 计算淡化曲线
    float u = fade(x);
    float v = fade(y);
    
    // 哈希坐标
    int A = m_perm[X] + Y;
    int AA = m_perm[A];
    int AB = m_perm[A + 1];
    int B = m_perm[X + 1] + Y;
    int BA = m_perm[B];
    int BB = m_perm[B + 1];
    
    // 插值计算
    return lerp(v, 
        lerp(u, grad(m_perm[AA], x, y), grad(m_perm[BA], x - 1, y)),
        lerp(u, grad(m_perm[AB], x, y - 1), grad(m_perm[BB], x - 1, y - 1))
    );
}

float PerlinNoise::fbm2D(float x, float y, int octaves, float persistence, float scale) const {
    float result = 0.0f;
    float amplitude = 1.0f;
    float frequency = scale;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        result += noise2D(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }
    
    return result / maxValue;
}

float PerlinNoise::turbulence2D(float x, float y, int octaves, float persistence, float scale) const {
    float result = 0.0f;
    float amplitude = 1.0f;
    float frequency = scale;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        result += std::abs(noise2D(x * frequency, y * frequency)) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }
    
    return result / maxValue;
}

float PerlinNoise::ridgedNoise2D(float x, float y, int octaves, float persistence, float scale) const {
    float result = 0.0f;
    float amplitude = 1.0f;
    float frequency = scale;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        float n = 1.0f - std::abs(noise2D(x * frequency, y * frequency));
        n = n * n; // 平方以增强脊状效果
        result += n * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }
    
    return result / maxValue;
}

// NoiseGenerator 实现

uint32_t NoiseGenerator::hash(uint32_t x) const {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float NoiseGenerator::heightNoise(float x, float y) const {
    // 多层噪声叠加生成自然地形
    float continental = m_perlin.fbm2D(x * 0.001f, y * 0.001f, 3, 0.6f);  // 大陆形状
    float terrain = m_perlin.fbm2D(x * 0.01f, y * 0.01f, 6, 0.5f);        // 主要地形
    float detail = m_perlin.fbm2D(x * 0.1f, y * 0.1f, 4, 0.4f);           // 细节
    
    // 组合并标准化到 [0, 1]
    float height = continental * 0.6f + terrain * 0.3f + detail * 0.1f;
    return (height + 1.0f) * 0.5f;
}

float NoiseGenerator::temperatureNoise(float x, float y) const {
    // 温度主要受纬度影响，加上一些噪声变化
    float latitudeFactor = 1.0f - std::abs(y * 0.002f); // 假设y=0是赤道
    float tempNoise = m_perlin.fbm2D(x * 0.005f, y * 0.005f, 3, 0.4f);
    float temp = latitudeFactor * 0.7f + tempNoise * 0.3f;
    return std::max(0.0f, std::min(1.0f, (temp + 1.0f) * 0.5f));
}

float NoiseGenerator::humidityNoise(float x, float y) const {
    // 湿度噪声，与温度相对独立
    float humidity = m_perlin.fbm2D(x * 0.008f + 1000.0f, y * 0.008f + 1000.0f, 4, 0.5f);
    return (humidity + 1.0f) * 0.5f;
}

float NoiseGenerator::caveNoise(float x, float y) const {
    // 使用更大尺度的湍流噪声生成更连续的洞穴
    float caves1 = m_perlin.turbulence2D(x * 0.015f, y * 0.015f, 3, 0.6f);      // 大尺度洞穴
    float caves2 = m_perlin.turbulence2D(x * 0.03f + 500.0f, y * 0.03f + 500.0f, 4, 0.4f); // 中等尺度细节
    float caves3 = m_perlin.fbm2D(x * 0.008f + 1000.0f, y * 0.008f + 1000.0f, 2, 0.5f);     // 更大的连续区域
    
    // 组合不同尺度的噪声
    return caves1 * 0.5f + caves2 * 0.3f + caves3 * 0.2f;
}

float NoiseGenerator::oreNoise(float x, float y, int oreType) const {
    // 为不同矿物类型生成不同的噪声模式
    uint32_t offset = hash(static_cast<uint32_t>(oreType) * 0x9e3779b1u);
    float offsetX = static_cast<float>(offset & 0xFFFF) * 0.1f;
    float offsetY = static_cast<float>((offset >> 16) & 0xFFFF) * 0.1f;
    
    float ore = m_perlin.fbm2D(x * 0.03f + offsetX, y * 0.03f + offsetY, 4, 0.6f);
    return (ore + 1.0f) * 0.5f;
}

} // namespace Tina::Core