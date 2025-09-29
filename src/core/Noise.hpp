//
// 噪声生成工具类 - 支持柏林噪声、分数布朗运动等
//

#pragma once

#include <cstdint>
#include <cmath>

namespace Tina::Core {

class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed = 0);
    
    // 2D 柏林噪声，返回 [-1, 1]
    float noise2D(float x, float y) const;
    
    // 分数布朗运动 (fBm) - 多层噪声叠加
    float fbm2D(float x, float y, int octaves = 4, float persistence = 0.5f, float scale = 1.0f) const;
    
    // 湍流噪声 - 绝对值叠加
    float turbulence2D(float x, float y, int octaves = 4, float persistence = 0.5f, float scale = 1.0f) const;
    
    // 脊状噪声 - 适合山脊生成
    float ridgedNoise2D(float x, float y, int octaves = 4, float persistence = 0.5f, float scale = 1.0f) const;

private:
    // 梯度向量
    static constexpr int GRAD_SIZE = 12;
    static constexpr float GRAD_3D[GRAD_SIZE][3] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
        {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
    };
    
    // 排列表
    static constexpr int PERM_SIZE = 256;
    uint8_t m_perm[PERM_SIZE * 2]; // 重复一次避免取模
    
    // 淡化函数
    static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    
    // 线性插值
    static float lerp(float t, float a, float b) { return a + t * (b - a); }
    
    // 梯度点积
    static float grad(int hash, float x, float y);
    
    void initPermutation(uint32_t seed);
};

// 简化噪声接口类
class NoiseGenerator {
public:
    explicit NoiseGenerator(uint32_t seed = 0) : m_perlin(seed), m_seed(seed) {}
    
    // 地形高度噪声 [0, 1]
    float heightNoise(float x, float y) const;
    
    // 温度噪声 [0, 1] - 用于生物群系
    float temperatureNoise(float x, float y) const;
    
    // 湿度噪声 [0, 1] - 用于生物群系 
    float humidityNoise(float x, float y) const;
    
    // 洞穴噪声 [0, 1] - 用于洞穴生成
    float caveNoise(float x, float y) const;
    
    // 矿物密度噪声 [0, 1]
    float oreNoise(float x, float y, int oreType) const;

private:
    PerlinNoise m_perlin;
    uint32_t m_seed;
    
    // Thomas Wang 哈希
    uint32_t hash(uint32_t x) const;
};

} // namespace Tina::Core