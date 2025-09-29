//
// TileMap（地形与水体生成）实现 - 改进版
// 使用柏林噪声、生物群系系统和改进的地形生成

#include "TileMap.hpp"
#include <cmath>
#include <algorithm>

namespace Tina::Game {

void TileMap::generate()
{
    // 新的分步生成系统
    // 1. 首先初始化基础数据
    m_water.resize((size_t)m_w * (size_t)m_h);
    std::fill(m_water.begin(), m_water.end(), (uint8_t)0);
    m_biomes.resize((size_t)m_w * (size_t)m_h);
    std::fill(m_biomes.begin(), m_biomes.end(), BiomeType::Plains);
    
    m_seaLevel = (int)(m_h * 0.25f);
    
    // 2. 分步生成
    generateBiomes();      // 生成生物群系
    generateTerrain();     // 生成基础地形
    generateCaves();       // 生成洞穴系统
    generateOres();        // 生成矿物
    generateVegetation();  // 生成植被
    generateDecorations(); // 生成地表装饰
    generateWater();       // 生成水体
}

void TileMap::generateBiomes()
{
    // 使用温度和湿度噪声确定生物群系
    for (int x = 0; x < m_w; ++x) {
        for (int y = 0; y < m_h; ++y) {
            float temperature = m_noiseGen.temperatureNoise(static_cast<float>(x), static_cast<float>(y));
            float humidity = m_noiseGen.humidityNoise(static_cast<float>(x), static_cast<float>(y));
            float height = m_noiseGen.heightNoise(static_cast<float>(x), static_cast<float>(y));
            
            BiomeType biome = determineBiome(temperature, humidity, height);
            setBiome(x, y, biome);
        }
    }
}

void TileMap::generateTerrain()
{
    // 使用柏林噪声生成地形高度
    for (int x = 0; x < m_w; ++x) {
        float heightNoise = m_noiseGen.heightNoise(static_cast<float>(x), 0.0f);
        
        // 将噪声转换为地表高度
        float baseHeight = m_h * 0.6f;  // 基础高度
        float variation = m_h * 0.3f;   // 高度变化范围
        int surfaceY = static_cast<int>(baseHeight + (heightNoise - 0.5f) * variation);
        surfaceY = std::max(10, std::min(m_h - 10, surfaceY));
        
        // 生成垂直地层
        for (int y = 0; y < m_h; ++y) {
            if (y > surfaceY) {
                set(x, y, TileType::Air);
                continue;
            }
            
            BiomeType biome = getBiome(x, y);
            int depth = surfaceY - y;
            
            TileType material;
            if (depth == 0) {
                material = getSurfaceMaterial(biome);
            } else {
                material = getSubsurfaceMaterial(biome, depth);
            }
            
            set(x, y, material);
        }
    }
}

void TileMap::generateCaves()
{
    // 使用改进的洞穴噪声生成洞穴系统
    for (int x = 1; x < m_w - 1; ++x) {
        for (int y = 1; y < m_h - 1; ++y) {
            if (get(x, y) == TileType::Air) continue;
            
            float caveNoise = m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y));
            
            // 根据深度调整洞穴密度
            int surfaceY = m_h;
            for (int sy = m_h - 1; sy >= 0; --sy) {
                if (get(x, sy) != TileType::Air) {
                    surfaceY = sy;
                    break;
                }
            }
            
            int depth = surfaceY - y;
            if (depth < 5) continue; // 地表附近不生成洞穴
            
            float caveThreshold = 0.3f + 0.01f * depth; // 越深洞穴越多
            if (caveNoise > caveThreshold) {
                set(x, y, TileType::Air);
            }
        }
    }
}

void TileMap::generateOres()
{
    // 在岩石中生成矿物
    for (int x = 0; x < m_w; ++x) {
        for (int y = 0; y < m_h; ++y) {
            TileType current = get(x, y);
            if (current != TileType::Stone && current != TileType::Dirt) continue;
            
            float caveNoise = m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y));
            
            // 检查每种矿物
            for (int oreInt = 0; oreInt < static_cast<int>(OreType::MAX_ORE_TYPE); ++oreInt) {
                OreType ore = static_cast<OreType>(oreInt);
                if (shouldGenerateOre(ore, x, y, caveNoise)) {
                    set(x, y, oreTypeToTileType(ore));
                    break; // 每个位置只生成一种矿物
                }
            }
        }
    }
}

void TileMap::generateVegetation()
{
    // 增强的植被生成系统
    for (int x = 0; x < m_w; ++x) {
        for (int y = m_h - 1; y >= 0; --y) {
            if (get(x, y) != TileType::Air) {
                // 找到地表
                if (y < m_h - 1 && get(x, y + 1) == TileType::Air) {
                    TileType surface = get(x, y);
                    BiomeType biome = getBiome(x, y);
                    
                    // 获取植被噪声用于决定是否生成植被
                    float vegetationNoise = m_noiseGen.humidityNoise(static_cast<float>(x), static_cast<float>(y));
                    float forestNoise = m_noiseGen.temperatureNoise(static_cast<float>(x * 0.1f), static_cast<float>(y * 0.1f));
                    
                    // 根据生物群系生成不同的植被
                    switch (biome) {
                        case BiomeType::Forest:
                            generateForestVegetation(x, y, vegetationNoise, forestNoise);
                            break;
                        case BiomeType::Plains:
                            generatePlainsVegetation(x, y, vegetationNoise);
                            break;
                        case BiomeType::Desert:
                            generateDesertVegetation(x, y, vegetationNoise);
                            break;
                        case BiomeType::Tundra:
                            generateTundraVegetation(x, y, vegetationNoise);
                            break;
                        case BiomeType::Swamp:
                            generateSwampVegetation(x, y, vegetationNoise);
                            break;
                        default:
                            break;
                    }
                }
                break;
            }
        }
    }
}

void TileMap::generateDecorations()
{
    // 生成各种地表装饰元素
    for (int x = 0; x < m_w; ++x) {
        for (int y = m_h - 1; y >= 0; --y) {
            if (get(x, y) != TileType::Air) {
                // 找到地表
                if (y < m_h - 1 && get(x, y + 1) == TileType::Air) {
                    TileType surface = get(x, y);
                    BiomeType biome = getBiome(x, y);
                    
                    float decorationNoise = m_noiseGen.caveNoise(static_cast<float>(x * 0.7f), static_cast<float>(y * 0.7f));
                    
                    // 根据生物群系生成不同的装饰
                    generateBiomeDecorations(x, y, biome, decorationNoise);
                }
                break;
            }
        }
    }
}

void TileMap::generateWater()
{
    // 海水填充
    for (int x = 0; x < m_w; ++x) {
        for (int y = 0; y <= m_seaLevel; ++y) {
            if (get(x, y) == TileType::Air) {
                m_water[index(x, y)] = 255;
            }
        }
    }
    
    // 生成湖泊（使用新的噪声生成器）
    const int lakeCount = std::max(2, m_w / 80);
    for (int i = 0; i < lakeCount; ++i) {
        // 使用新噪声生成器替代旧的 noise1
        float rand1 = m_noiseGen.oreNoise(static_cast<float>(i * 17), 0.0f, 1);
        float rand2 = m_noiseGen.oreNoise(static_cast<float>(i * 31), 0.0f, 2);
        float rand3 = m_noiseGen.oreNoise(static_cast<float>(i * 47), 0.0f, 3);
        float rand4 = m_noiseGen.oreNoise(static_cast<float>(i * 61), 0.0f, 4);
        
        int cx = (int)(rand1 * (m_w - 20)) + 10;
        int cy = (int)(m_h * (0.35f + 0.25f * rand2));
        float rx = 6.0f + 10.0f * rand3;
        float ry = 4.0f + 8.0f * rand4;

        int x0 = std::max(1, cx - (int)rx - 1);
        int x1 = std::min(m_w - 2, cx + (int)rx + 1);
        int y0 = std::max(1, cy - (int)ry - 1);
        int y1 = std::min(m_h - 2, cy + (int)ry + 1);

        for (int x = x0; x <= x1; ++x) {
            int groundY = -1;
            for (int yy = m_h - 1; yy >= 0; --yy) {
                if (get(x, yy) != TileType::Air) {
                    groundY = yy;
                    break;
                }
            }
            if (groundY <= 0) continue;

            for (int y = y0; y <= y1; ++y) {
                float dx = (x - cx) / rx;
                float dy = (y - cy) / ry;
                if (dx * dx + dy * dy > 1.0f) continue;
                if (y > groundY) continue;

                if (y <= cy) {
                    set(x, y, TileType::Air);
                    m_water[index(x, y)] = 255;
                } else {
                    setSafe(x, y, TileType::Air);
                }
            }
        }
    }
}

void TileMap::waterFlow(float wx, float wy, float& outVx, float& outVy) const
{
    outVx = 0.0f; outVy = 0.0f;
    int x = (int)std::floor(wx);
    int y = (int)std::floor(wy);
    if ((unsigned)x >= (unsigned)m_w || (unsigned)y >= (unsigned)m_h) return;
    if (m_water[index(x,y)] == 0) return;

    // 使用新的噪声生成器
    float n = m_noiseGen.heightNoise(wx * 0.5f, wy * 0.5f); // 平滑一些
    float ang = n * 6.2831853f;
    float vx = std::cos(ang);
    float vy = std::sin(ang);
    // 海水偏移：海平面以下偏向向右流动
    if (y <= m_seaLevel) vx += 0.8f;
    // 归一化并设定速度
    float len = std::sqrt(vx*vx + vy*vy) + 1e-6f;
    vx /= len; vy /= len;
    float speed = 6.0f; // 单位/秒
    outVx = vx * speed;
    outVy = vy * speed * 0.6f; // 稍微抑制竖直分量
}

bool TileMap::stepWater(int iterations, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY)
{
    if (iterations < 1) iterations = 1;
    const int W = m_w, H = m_h;
    if ((int)m_work.size() != W * H) m_work.resize((size_t)W * H, (uint8_t)0);

    bool anyChanged = false;
    int minx = W, miny = H, maxx = -1, maxy = -1;

    // 调低单次可转移体积，避免水体过快扩散
    const int MAX_DOWN = 16;  // 每步最大下落体积（原 64）
    const int MAX_SIDE = 8;   // 每步最大侧流体积（原 32）

    for (int it = 0; it < iterations; ++it) {
        std::copy(m_water.begin(), m_water.end(), m_work.begin());
        bool changed = false;

        // 1) 下落
        for (int y = H - 1; y >= 1; --y) {
            for (int x = 0; x < W; ++x) {
                uint8_t a = m_water[index(x,y)];
                if (a == 0) continue;
                if (get(x, y-1) != TileType::Air) continue; // 下面是实心
                uint8_t b = m_work[index(x, y-1)];
                int cap = 255 - (int)b;
                if (cap <= 0) continue;
                int move = std::min<int>({ (int)a, cap, MAX_DOWN });
                if (move > 0) {
                    m_work[index(x,y)]   = (uint8_t)((int)m_work[index(x,y)] - move);
                    m_work[index(x,y-1)] = (uint8_t)((int)m_work[index(x,y-1)] + move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x);
                    miny = std::min(miny, y-1); maxy = std::max(maxy, y);
                    changed = true;
                }
            }
        }

        // 2) 侧向均衡（减缓扩散强度）
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W - 1; ++x) {
                if (get(x, y) != TileType::Air || get(x+1, y) != TileType::Air) continue;
                int a = (int)m_work[index(x,y)];
                int b = (int)m_work[index(x+1,y)];
                int diff = a - b;
                if (diff > 1) {
                    int move = std::min(std::max(diff/4, 1), MAX_SIDE);
                    m_work[index(x,y)]     = (uint8_t)(a - move);
                    m_work[index(x+1, y)]  = (uint8_t)(b + move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x+1);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    changed = true;
                } else if (diff < -1) {
                    int move = std::min(std::max((-diff)/4, 1), MAX_SIDE);
                    m_work[index(x,y)]     = (uint8_t)(a + move);
                    m_work[index(x+1, y)]  = (uint8_t)(b - move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x+1);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    changed = true;
                }
            }
        }

        if (!changed) break;
        anyChanged = true;
        m_water.swap(m_work);
        ++m_tick;
    }

    outMinX = minx; outMinY = miny; outMaxX = maxx; outMaxY = maxy;
    return anyChanged;
}
} // namespace Tina::Game

// ===== 高级功能实现：基于梯度的流场 + 水平池化找平 =====
namespace Tina::Game {

void TileMap::waterFlowAdvanced(float wx, float wy, float& outVx, float& outVy) const
{
    outVx = 0.0f; outVy = 0.0f;
    const int cx = (int)std::floor(wx);
    const int cy = (int)std::floor(wy);
    if ((unsigned)cx >= (unsigned)m_w || (unsigned)cy >= (unsigned)m_h) return;
    if (m_water[index(cx,cy)] == 0 && get(cx,cy) != TileType::Water) return;

    auto H = [&](int ix, int iy)->float{
        if ((unsigned)ix >= (unsigned)m_w || (unsigned)iy >= (unsigned)m_h) return 0.0f;
        float h = (float)m_water[index(ix,iy)] / 255.0f;
        if (get(ix,iy) == TileType::Water) h = std::max(h, 1.0f);
        return h;
    };
    const float hx1 = H(cx+1, cy), hx0 = H(cx-1, cy);
    const float hy1 = H(cx, cy+1), hy0 = H(cx, cy-1);
    float gx = 0.5f * (hx1 - hx0);
    float gy = 0.5f * (hy1 - hy0);
    float vx = -gx;
    float vy = -gy;
    if (cy <= m_seaLevel) vx += 0.15f; // 轻微海流偏置
    float len = std::sqrt(vx*vx + vy*vy);
    if (len < 1e-5f) return;
    vx /= len; vy /= len;
    const float speed = 6.0f;
    outVx = vx * speed;
    outVy = vy * speed;
}

bool TileMap::stepWaterAdvanced(int iterations, int& outMinX, int& outMinY, int& outMaxX, int& outMaxY)
{
    if (iterations < 1) iterations = 1;
    const int W = m_w, H = m_h;
    if ((int)m_work.size() != W * H) m_work.resize((size_t)W * H, (uint8_t)0);

    bool anyChanged = false;
    int minx = W, miny = H, maxx = -1, maxy = -1;

    // 调低单次可转移体积，避免水体过快扩散
    const int MAX_DOWN = 16;  // 原 64
    const int MAX_SIDE = 8;   // 原 32

    for (int it = 0; it < iterations; ++it) {
        std::copy(m_water.begin(), m_water.end(), m_work.begin());
        bool changed = false;

        // 1) 重力下落
        for (int y = H - 1; y >= 1; --y) {
            for (int x = 0; x < W; ++x) {
                uint8_t a = m_water[index(x,y)];
                if (a == 0) continue;
                if (get(x, y-1) != TileType::Air) continue;
                uint8_t b = m_work[index(x, y-1)];
                int cap = 255 - (int)b;
                if (cap <= 0) continue;
                int move = std::min<int>({ (int)a, cap, MAX_DOWN });
                if (move > 0) {
                    m_work[index(x,y)]   = (uint8_t)((int)m_work[index(x,y)] - move);
                    m_work[index(x,y-1)] = (uint8_t)((int)m_work[index(x,y-1)] + move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x);
                    miny = std::min(miny, y-1); maxy = std::max(maxy, y);
                    changed = true;
                }
            }
        }

        // 2) 相邻对调（侧向均衡，降低强度）
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W - 1; ++x) {
                if (get(x, y) != TileType::Air || get(x+1, y) != TileType::Air) continue;
                int a = (int)m_work[index(x,y)];
                int b = (int)m_work[index(x+1,y)];
                int diff = a - b;
                if (diff > 1) {
                    int move = std::min(std::max(diff/4, 1), MAX_SIDE);
                    m_work[index(x,y)]     = (uint8_t)(a - move);
                    m_work[index(x+1, y)]  = (uint8_t)(b + move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x+1);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    changed = true;
                } else if (diff < -1) {
                    int move = std::min(std::max((-diff)/4, 1), MAX_SIDE);
                    m_work[index(x,y)]     = (uint8_t)(a + move);
                    m_work[index(x+1, y)]  = (uint8_t)(b - move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x+1);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    changed = true;
                }
            }
        }

        // 2.5) 水平“池化”均衡：降低频率（每 4 次迭代执行 1 次），避免瞬间拉平
        if ( (m_tick & 3) == 0 ) {
            for (int y = 0; y < H; ++y) {
                int x = 0;
                while (x < W) {
                    if (get(x,y) != TileType::Air) { ++x; continue; }
                    int r = x;
                    while (r < W && get(r,y) == TileType::Air) ++r; // [x, r)
                    const int lenSeg = r - x;
                    int sum = 0;
                    for (int xi = x; xi < r; ++xi) sum += (int)m_work[index(xi,y)];
                    if (sum > 0 && lenSeg > 0) {
                        const int tgt = sum / lenSeg;
                        int rem = sum - tgt * lenSeg;
                        for (int xi = x; xi < r; ++xi) {
                            int nv = tgt + ((xi - x) < rem ? 1 : 0);
                            int idxc = index(xi,y);
                            if (nv != (int)m_work[idxc]) {
                                m_work[idxc] = (uint8_t)nv;
                                minx = std::min(minx, xi); maxx = std::max(maxx, xi);
                                miny = std::min(miny, y);  maxy = std::max(maxy, y);
                                changed = true;
                            }
                        }
                    }
                    x = r;
                }
            }
        }

        if (!changed) break;
        anyChanged = true;
        m_water.swap(m_work);
        ++m_tick;
    }

    outMinX = minx; outMinY = miny; outMaxX = maxx; outMaxY = maxy;
    return anyChanged;
}

// 生物群系确定逻辑
BiomeType TileMap::determineBiome(float temperature, float humidity, float height) const
{
    // 简化的生物群系逻辑
    // temperature: 0=寒冷, 1=炎热
    // humidity: 0=干燥, 1=湿润
    // height: 0=低海拔, 1=高海拔
    
    if (height < 0.3f) {
        return BiomeType::Ocean;  // 低海拔区域为海洋
    }
    
    if (height < 0.4f && humidity > 0.6f) {
        return BiomeType::Beach;  // 海岸线
    }
    
    if (temperature < 0.3f) {
        if (height > 0.7f) return BiomeType::Mountain;  // 高寒山地
        return BiomeType::Tundra;  // 苔原
    }
    
    if (temperature > 0.7f && humidity < 0.3f) {
        return BiomeType::Desert;  // 沙漠
    }
    
    if (humidity > 0.6f && temperature > 0.4f) {
        if (height > 0.6f) return BiomeType::Mountain;
        return BiomeType::Forest;  // 森林
    }
    
    if (humidity < 0.4f && temperature < 0.6f && height > 0.5f) {
        return BiomeType::Mountain;  // 山地
    }
    
    return BiomeType::Plains;  // 默认平原
}

TileType TileMap::getSurfaceMaterial(BiomeType biome) const
{
    switch (biome) {
        case BiomeType::Forest:
        case BiomeType::Plains:
            return TileType::Grass;
        case BiomeType::Desert:
        case BiomeType::Beach:
            return TileType::Sand;
        case BiomeType::Tundra:
        case BiomeType::Mountain:
            return TileType::Snow;
        case BiomeType::Swamp:
            return TileType::Clay;
        case BiomeType::Ocean:
        default:
            return TileType::Stone;
    }
}

TileType TileMap::getSubsurfaceMaterial(BiomeType biome, int depth) const
{
    // 根据深度和生物群系确定地下材料
    if (depth <= 0) return getSurfaceMaterial(biome);
    
    // 浅层（1-6格深）
    if (depth <= 6) {
        switch (biome) {
            case BiomeType::Desert:
            case BiomeType::Beach:
                return TileType::Sand;
            case BiomeType::Swamp:
                return TileType::Clay;
            default:
                return TileType::Dirt;
        }
    }
    
    // 深层（7-20格深）
    if (depth <= 20) {
        return TileType::Stone;
    }
    
    // 极深层（20格以上）
    if (depth > 40) {
        return TileType::Bedrock;  // 基岩层
    }
    
    return TileType::Stone;
}

bool TileMap::shouldGenerateOre(OreType ore, int x, int y, float caveNoise) const
{
    // 计算深度
    int surfaceY = m_h;
    for (int sy = m_h - 1; sy >= 0; --sy) {
        if (get(x, sy) != TileType::Air) {
            surfaceY = sy;
            break;
        }
    }
    int depth = surfaceY - y;
    
    if (depth < 3) return false;  // 地表附近不生成矿物
    
    float oreNoise = m_noiseGen.oreNoise(static_cast<float>(x), static_cast<float>(y), static_cast<int>(ore));
    
    // 不同矿物有不同的生成条件
    switch (ore) {
        case OreType::Coal:
            return depth >= 5 && depth <= 25 && oreNoise > 0.7f;
        case OreType::Iron:
            return depth >= 8 && depth <= 30 && oreNoise > 0.75f;
        case OreType::Gold:
            return depth >= 15 && depth <= 35 && oreNoise > 0.8f;
        case OreType::Diamond:
            return depth >= 25 && oreNoise > 0.85f;
        default:
            return false;
    }
}

TileType TileMap::oreTypeToTileType(OreType ore) const
{
    switch (ore) {
        case OreType::Coal:    return TileType::Coal;
        case OreType::Iron:    return TileType::Iron;
        case OreType::Gold:    return TileType::Gold;
        case OreType::Diamond: return TileType::Diamond;
        default:               return TileType::Stone;
    }
}

// 植被生成辅助函数实现
void TileMap::generateForestVegetation(int x, int y, float vegetationNoise, float forestNoise)
{
    TileType surface = get(x, y);
    if (surface != TileType::Grass) return;
    
    // 森林有很高的植被密度
    if (vegetationNoise > 0.3f) {  // 70%的概率生成植被
        if (forestNoise > 0.7f) {
            // 30% 概率生成大树
            generateBigTree(x, y);
        } else if (vegetationNoise > 0.5f) {
            // 50% 概率生成普通树
            int treeHeight = 3 + static_cast<int>(vegetationNoise * 4);  // 3-7格高
            generateTree(x, y, treeHeight, true);
        } else {
            // 其余生成小灌木
            if (y < m_h - 2) {
                set(x, y + 1, TileType::Leaves);
            }
        }
    }
}

void TileMap::generatePlainsVegetation(int x, int y, float vegetationNoise)
{
    TileType surface = get(x, y);
    if (surface != TileType::Grass) return;
    
    // 平原有中等植被密度
    if (vegetationNoise > 0.7f) {  // 30%的概率
        if (vegetationNoise > 0.85f) {
            // 15% 概率生成小树
            generateTree(x, y, 2 + static_cast<int>(vegetationNoise * 3), true);
        } else {
            // 其余生成草丛
            if (y < m_h - 2) {
                set(x, y + 1, TileType::Leaves);  // 用树叶代表草丛
            }
        }
    }
}

void TileMap::generateDesertVegetation(int x, int y, float vegetationNoise)
{
    TileType surface = get(x, y);
    if (surface != TileType::Sand) return;
    
    // 沙漠植被稀少
    if (vegetationNoise > 0.9f) {  // 10%的概率
        // 生成仙人掌（用木材代表）
        if (y < m_h - 3) {
            set(x, y + 1, TileType::Wood);
            if (vegetationNoise > 0.95f && y < m_h - 4) {
                set(x, y + 2, TileType::Wood);
            }
        }
    }
}

void TileMap::generateTundraVegetation(int x, int y, float vegetationNoise)
{
    TileType surface = get(x, y);
    if (surface != TileType::Snow) return;
    
    // 苔原植被稀少且矮小
    if (vegetationNoise > 0.8f) {  // 20%的概率
        if (y < m_h - 2) {
            // 生成低矮的冰雪植被
            set(x, y + 1, TileType::Ice);  // 用冰代表冰雪植被
        }
    }
}

void TileMap::generateSwampVegetation(int x, int y, float vegetationNoise)
{
    TileType surface = get(x, y);
    if (surface != TileType::Clay) return;
    
    // 沼泽植被密集但种类单一
    if (vegetationNoise > 0.4f) {  // 60%的概率
        if (vegetationNoise > 0.8f) {
            // 生成沼泽树（较矮）
            generateTree(x, y, 2 + static_cast<int>(vegetationNoise * 2), false);
        } else {
            // 生成沼泽草
            if (y < m_h - 2) {
                set(x, y + 1, TileType::Leaves);
            }
        }
    }
}

void TileMap::generateTree(int x, int y, int height, bool hasLeaves)
{
    if (y >= m_h - height - 1 || x < 0 || x >= m_w) return;  // 边界检查
    
    // 生成树干
    for (int h = 1; h <= height; ++h) {
        if (y + h >= m_h) break;
        set(x, y + h, TileType::Wood);
    }
    
    // 生成树叶
    if (hasLeaves && y + height + 1 < m_h) {
        // 树顶
        set(x, y + height + 1, TileType::Leaves);
        
        // 如果树够高，在树干周围也生成一些树叶
        if (height >= 4) {
            int leafY = y + height;
            // 左右两侧
            if (x > 0 && leafY < m_h && get(x - 1, leafY) == TileType::Air) {
                set(x - 1, leafY, TileType::Leaves);
            }
            if (x < m_w - 1 && leafY < m_h && get(x + 1, leafY) == TileType::Air) {
                set(x + 1, leafY, TileType::Leaves);
            }
        }
    }
}

void TileMap::generateBigTree(int x, int y)
{
    int height = 5 + static_cast<int>(m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y)) * 3);  // 5-8格高
    if (y >= m_h - height - 2 || x < 0 || x >= m_w) return;  // 边界检查
    
    // 生成粗树干（有时是2格宽）
    bool isWide = m_noiseGen.temperatureNoise(static_cast<float>(x), static_cast<float>(y)) > 0.7f;
    
    for (int h = 1; h <= height; ++h) {
        if (y + h >= m_h) break;
        set(x, y + h, TileType::Wood);
        
        // 宽树干
        if (isWide && x < m_w - 1 && h <= height - 1) {
            if (get(x + 1, y + h) == TileType::Air) {
                set(x + 1, y + h, TileType::Wood);
            }
        }
    }
    
    // 生成茂密的树冠
    int leafLevel = y + height;
    int leafLevel2 = y + height + 1;
    
    // 第一层树叶（较大）
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = 0; dy <= 1; ++dy) {
            int lx = x + dx;
            int ly = leafLevel + dy;
            if (lx >= 0 && lx < m_w && ly >= 0 && ly < m_h && get(lx, ly) == TileType::Air) {
                // 边缘树叶有一定概率不生成，形成自然形状
                float edgeProb = m_noiseGen.humidityNoise(static_cast<float>(lx), static_cast<float>(ly));
                if (std::abs(dx) <= 1 || edgeProb > 0.4f) {
                    set(lx, ly, TileType::Leaves);
                }
            }
        }
    }
    
    // 第二层树叶（较小）
    if (leafLevel2 >= 0 && leafLevel2 < m_h) {
        for (int dx = -1; dx <= 1; ++dx) {
            int lx = x + dx;
            if (lx >= 0 && lx < m_w && get(lx, leafLevel2) == TileType::Air) {
                set(lx, leafLevel2, TileType::Leaves);
            }
        }
    }
}

void TileMap::generateBiomeDecorations(int x, int y, BiomeType biome, float decorationNoise)
{
    switch (biome) {
        case BiomeType::Mountain:
            // 山地：生成巨石
            if (decorationNoise > 0.85f && y < m_h - 2) {
                set(x, y + 1, TileType::Stone);
                if (decorationNoise > 0.9f && y < m_h - 3) {
                    set(x, y + 2, TileType::Stone);
                }
            }
            break;
            
        case BiomeType::Desert:
            // 沙漠：生成沙堆和偶尔的宝石
            if (decorationNoise > 0.8f && y < m_h - 2) {
                if (decorationNoise > 0.95f) {
                    set(x, y + 1, TileType::Gold);  // 罕见的金块
                } else {
                    set(x, y + 1, TileType::Sand);  // 沙堆
                }
            }
            break;
            
        case BiomeType::Beach:
            // 海滩：生成贝壳（用钻石代表）和海草
            if (decorationNoise > 0.85f && y < m_h - 2) {
                if (decorationNoise > 0.92f) {
                    set(x, y + 1, TileType::Diamond);  // 贝壳
                } else {
                    set(x, y + 1, TileType::Leaves);   // 海草
                }
            }
            break;
            
        case BiomeType::Tundra:
            // 苔原：生成冰块装饰
            if (decorationNoise > 0.8f && y < m_h - 2) {
                set(x, y + 1, TileType::Ice);
            }
            break;
            
        case BiomeType::Plains:
            // 平原：生成小花（用五彩的矿物代表）
            if (decorationNoise > 0.88f && y < m_h - 2) {
                float flowerType = m_noiseGen.humidityNoise(static_cast<float>(x), static_cast<float>(y));
                if (flowerType > 0.7f) {
                    set(x, y + 1, TileType::Gold);     // 黄花
                } else if (flowerType > 0.4f) {
                    set(x, y + 1, TileType::Iron);     // 白花
                } else {
                    set(x, y + 1, TileType::Diamond);  // 蓝花
                }
            }
            break;
            
        case BiomeType::Forest:
            // 森林：生成蘑菇和浆果丛
            if (decorationNoise > 0.8f && y < m_h - 2) {
                if (decorationNoise > 0.9f) {
                    set(x, y + 1, TileType::Coal);     // 蘑菇（黑色）
                } else {
                    set(x, y + 1, TileType::Leaves);   // 浆果丛
                }
            }
            break;
            
        case BiomeType::Swamp:
            // 沼泽：生成枯木和泥球
            if (decorationNoise > 0.75f && y < m_h - 2) {
                if (decorationNoise > 0.85f) {
                    set(x, y + 1, TileType::Wood);     // 枯木
                } else {
                    set(x, y + 1, TileType::Clay);     // 泥球
                }
            }
            break;
            
        default:
            break;
    }
}

} // namespace Tina::Game
