//
// TileMap（地形与水体生成）实现 - 改进版
// 使用柏林噪声、生物群系系统和改进的地形生成

#include "TileMap.hpp"
#include <cmath>
#include <algorithm>

namespace Tina::Game {

void TileMap::generate()
{
    // 统一的地图生成系统
    // 第一阶段：基础初始化
    initializeBasicData();
    
    // 第二阶段：地形生成
    generateBiomes();      // 1. 生成生物群系
    generateTerrain();     // 2. 生成基础地形
    generateCaves();       // 3. 生成洞穴系统
    generateOres();        // 4. 生成矿物
    
    // 第三阶段：生态生成（避免重叠）
    generateSurfaceFeatures(); // 5. 统一生成地表特征（植被+装饰）
    
    // 第四阶段：液体系统
    generateWater();       // 6. 生成水体和岩浆
    
    // 第五阶段：后处理
    postProcessGeneration(); // 7. 最终处理和优化
}

void TileMap::initializeBasicData()
{
    // 初始化所有数据数组
    m_water.resize((size_t)m_w * (size_t)m_h);
    std::fill(m_water.begin(), m_water.end(), (uint8_t)0);
    m_lava.resize((size_t)m_w * (size_t)m_h);
    std::fill(m_lava.begin(), m_lava.end(), (uint8_t)0);
    m_biomes.resize((size_t)m_w * (size_t)m_h);
    std::fill(m_biomes.begin(), m_biomes.end(), BiomeType::Plains);
    
    m_seaLevel = (int)(m_h * 0.25f);
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
    // 改进的洞穴生成：降低密度，提高质量
    for (int x = 1; x < m_w - 1; ++x) {
        // 找到地表位置
        int surfaceY = -1;
        for (int sy = m_h - 1; sy >= 0; --sy) {
            if (get(x, sy) != TileType::Air) {
                surfaceY = sy;
                break;
            }
        }
        if (surfaceY == -1) continue;
        
        for (int y = 1; y < m_h - 1; ++y) {
            if (get(x, y) == TileType::Air) continue;
            
            int depth = surfaceY - y;
            if (depth < 8) continue; // 增加最小深度，减少浅层洞穴
            
            float caveNoise = m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y));
            
            // 提高阈值，减少洞穴密度
            float baseThreshold = 0.45f;  // 提高基础阈值
            float depthBonus = std::min(depth * 0.003f, 0.15f); // 减少深度奖励
            float caveThreshold = baseThreshold - depthBonus;
            
            // 额外的连通性检查：避免孤立的小洞
            if (caveNoise > caveThreshold) {
                // 检查周围是否有其他空气，确保洞穴连通
                int airCount = 0;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < m_w && ny >= 0 && ny < m_h) {
                            if (get(nx, ny) == TileType::Air) airCount++;
                        }
                    }
                }
                
                // 只有周围有足够空气或噪声值很高时才生成
                if (airCount >= 1 || caveNoise > caveThreshold + 0.1f) {
                    set(x, y, TileType::Air);
                }
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

void TileMap::generateSurfaceFeatures()
{
    // 统一的地表特征生成系统 - 避免重叠冲突
    for (int x = 0; x < m_w; ++x) {
        for (int y = m_h - 1; y >= 0; --y) {
            if (get(x, y) != TileType::Air) {
                // 找到地表
                if (y < m_h - 1 && get(x, y + 1) == TileType::Air) {
                    TileType surface = get(x, y);
                    BiomeType biome = getBiome(x, y);
                    
                    // 获取噪声值
                    float primaryNoise = m_noiseGen.humidityNoise(static_cast<float>(x), static_cast<float>(y));
                    float secondaryNoise = m_noiseGen.temperatureNoise(static_cast<float>(x * 0.7f), static_cast<float>(y * 0.7f));
                    float decorationNoise = m_noiseGen.caveNoise(static_cast<float>(x * 0.3f), static_cast<float>(y * 0.3f));
                    
                    // 优先级系统：树木 > 大型装饰 > 小型装饰
                    bool featureGenerated = false;
                    
                    // 第一优先级：生成树木（降低概率）
                    if (!featureGenerated && shouldGenerateTree(biome, primaryNoise, secondaryNoise)) {
                        generateTreeForBiome(x, y, biome, primaryNoise, secondaryNoise);
                        featureGenerated = true;
                    }
                    
                    // 第二优先级：生成大型装饰
                    if (!featureGenerated && shouldGenerateLargeDecoration(biome, decorationNoise)) {
                        generateLargeDecorationForBiome(x, y, biome, decorationNoise);
                        featureGenerated = true;
                    }
                    
                    // 第三优先级：生成小型装饰
                    if (!featureGenerated && shouldGenerateSmallDecoration(biome, primaryNoise)) {
                        generateSmallDecorationForBiome(x, y, biome, primaryNoise);
                        featureGenerated = true;
                    }
                }
                break;
            }
        }
    }
}

void TileMap::postProcessGeneration()
{
    // 地图生成后处理
    // 1. 清理孤立的单个方块
    cleanupIsolatedBlocks();
    
    // 2. 平滑地形边缘
    smoothTerrainEdges();
    
    // 3. 验证液体系统一致性
    validateLiquidConsistency();
}

void TileMap::cleanupIsolatedBlocks()
{
    // 清理孤立的装饰方块，避免视觉混乱
    for (int x = 1; x < m_w - 1; ++x) {
        for (int y = 1; y < m_h - 1; ++y) {
            TileType current = get(x, y);
            
            // 检查装饰类型的孤立方块
            if (current == TileType::Flower || current == TileType::Grass_Decoration || 
                current == TileType::Mushroom || current == TileType::Crystal) {
                
                // 检查是否有支撑
                bool hasSupport = false;
                if (y > 0 && get(x, y - 1) != TileType::Air) {
                    hasSupport = true;
                }
                
                if (!hasSupport) {
                    set(x, y, TileType::Air);
                }
            }
        }
    }
}

void TileMap::smoothTerrainEdges()
{
    // 平滑地形的锯齿边缘
    for (int x = 1; x < m_w - 1; ++x) {
        for (int y = 1; y < m_h - 1; ++y) {
            TileType current = get(x, y);
            if (current == TileType::Air) continue;
            
            // 检查是否为突出的单个方块
            int airNeighbors = 0;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    if (get(x + dx, y + dy) == TileType::Air) {
                        airNeighbors++;
                    }
                }
            }
            
            // 如果周围空气太多，可能是噪点
            if (airNeighbors >= 6 && current != TileType::Wood && current != TileType::Leaves) {
                float smoothingNoise = m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y));
                if (smoothingNoise < 0.3f) {
                    set(x, y, TileType::Air);
                }
            }
        }
    }
}

void TileMap::validateLiquidConsistency()
{
    // 确保液体数据与瓦片数据一致
    for (int x = 0; x < m_w; ++x) {
        for (int y = 0; y < m_h; ++y) {
            TileType tile = get(x, y);
            uint8_t waterLevel = m_water[index(x, y)];
            uint8_t lavaLevel = m_lava[index(x, y)];
            
            // 清理不一致的液体数据
            if (tile != TileType::Air && tile != TileType::Water && tile != TileType::Lava) {
                if (waterLevel > 0) m_water[index(x, y)] = 0;
                if (lavaLevel > 0) m_lava[index(x, y)] = 0;
            }
        }
    }
}

void TileMap::generateWater()
{
    // 海水填充 - 只填充地表的海洋区域，不填充地下洞穴
    for (int x = 0; x < m_w; ++x) {
        for (int y = 0; y <= m_seaLevel; ++y) {
            if (get(x, y) == TileType::Air) {
                // 检查是否为地下洞穴：向上追踪是否直接连接到地表
                bool isUndergroundCave = false;
                for (int checkY = y + 1; checkY < m_h; ++checkY) {
                    TileType checkTile = get(x, checkY);
                    if (checkTile != TileType::Air) {
                        // 遇到固体，这是地下洞穴
                        isUndergroundCave = true;
                        break;
                    }
                }
                
                // 只在非地下洞穴的区域填充海水
                if (!isUndergroundCave) {
                    m_water[index(x, y)] = 255;
                }
            }
        }
    }
    
    // 地狱层岩浆生成 - 改为液体系统
    int hellStart = static_cast<int>(m_h * 0.9f);
    for (int x = 0; x < m_w; ++x) {
        for (int y = 0; y < hellStart; ++y) {
            float relativeDepth = static_cast<float>(m_h - y) / static_cast<float>(m_h);
            
            // 在地狱层的空气区域填充岩浆（液体）
            if (relativeDepth >= 0.85f && get(x, y) == TileType::Air) {
                m_lava[index(x, y)] = 255;
            }
            
            // 在最底层强制生成岩浆湖
            if (y < m_h * 0.05f) { // 底部5%
                if (get(x, y) == TileType::Air) {
                    // 在基岩中挖出岩浆湖
                    float lavaNoice = m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y));
                    if (lavaNoice > 0.3f) {
                        m_lava[index(x, y)] = 255;
                    }
                } else if (get(x, y) == TileType::Bedrock) {
                    // 替换部分基岩为岩浆
                    float lavaNoice = m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y));
                    if (lavaNoice > 0.4f) {
                        set(x, y, TileType::Air);
                        m_lava[index(x, y)] = 255;
                    }
                }
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
    
    // 岩浆-水接触生成黑曜石
    for (int x = 0; x < m_w; ++x) {
        for (int y = 0; y < m_h; ++y) {
            // 检查有岩浆的位置
            if (m_lava[index(x, y)] > 0 && get(x, y) == TileType::Air) {
                // 检查周围是否有水
                bool hasWater = false;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx >= 0 && nx < m_w && ny >= 0 && ny < m_h) {
                            if (isWater(nx, ny)) {
                                hasWater = true;
                                break;
                            }
                        }
                    }
                    if (hasWater) break;
                }
                
                // 如果岩浆周围有水，生成黑曜石
                if (hasWater) {
                    set(x, y, TileType::Obsidian);
                    m_lava[index(x, y)] = 0;  // 清除岩浆
                }
            }
            
            // 检查有水的位置
            if (m_water[index(x, y)] > 0 && get(x, y) == TileType::Air) {
                // 检查周围是否有岩浆
                bool hasLava = false;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx >= 0 && nx < m_w && ny >= 0 && ny < m_h) {
                            if (isLava(nx, ny)) {
                                hasLava = true;
                                break;
                            }
                        }
                    }
                    if (hasLava) break;
                }
                
                // 如果水周围有岩浆，生成黑曜石（较低概率，因为岩浆更热）
                if (hasLava) {
                    float obsidianChance = m_noiseGen.caveNoise(static_cast<float>(x), static_cast<float>(y));
                    if (obsidianChance > 0.6f) {  // 60%概率生成黑曜石
                        set(x, y, TileType::Obsidian);
                        m_water[index(x, y)] = 0;  // 清除水
                    }
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
    
// ===== 统一的地表特征生成系统实现 =====

bool TileMap::shouldGenerateTree(BiomeType biome, float primaryNoise, float secondaryNoise) const
{
    switch (biome) {
        case BiomeType::Forest:
            return primaryNoise > 0.4f && secondaryNoise > 0.3f; // 60% * 70% = 42%概率
        case BiomeType::Plains:
            return primaryNoise > 0.85f; // 15%概率
        case BiomeType::Swamp:
            return primaryNoise > 0.6f; // 40%概率
        case BiomeType::Desert:
        case BiomeType::Tundra:
        case BiomeType::Mountain:
        case BiomeType::Beach:
        case BiomeType::Ocean:
        default:
            return false; // 这些生物群系不生成树木
    }
}

bool TileMap::shouldGenerateLargeDecoration(BiomeType biome, float decorationNoise) const
{
    switch (biome) {
        case BiomeType::Mountain:
            return decorationNoise > 0.8f; // 20%概率生成巨石
        case BiomeType::Desert:
            return decorationNoise > 0.9f; // 10%概率生成仙人掌
        case BiomeType::Tundra:
            return decorationNoise > 0.85f; // 15%概率生成冰块
        default:
            return false;
    }
}

bool TileMap::shouldGenerateSmallDecoration(BiomeType biome, float primaryNoise) const
{
    switch (biome) {
        case BiomeType::Plains:
            return primaryNoise > 0.88f; // 12%概率生成花朵
        case BiomeType::Forest:
            return primaryNoise > 0.82f; // 18%概率生成蘑菇/浆果
        case BiomeType::Beach:
            return primaryNoise > 0.9f; // 10%概率生成贝壳
        case BiomeType::Swamp:
            return primaryNoise > 0.8f; // 20%概率生成沼泽装饰
        default:
            return false;
    }
}

void TileMap::generateTreeForBiome(int x, int y, BiomeType biome, float primaryNoise, float secondaryNoise)
{
    switch (biome) {
        case BiomeType::Forest:
            if (secondaryNoise > 0.8f) {
                generateBigTree(x, y); // 大树
            } else {
                int height = 3 + static_cast<int>(primaryNoise * 4); // 3-7格高
                generateTree(x, y, height, true);
            }
            break;
        case BiomeType::Plains:
            generateTree(x, y, 2 + static_cast<int>(primaryNoise * 3), true); // 小树
            break;
        case BiomeType::Swamp:
            generateTree(x, y, 2 + static_cast<int>(primaryNoise * 2), false); // 沼泽树
            break;
        default:
            break;
    }
}

void TileMap::generateLargeDecorationForBiome(int x, int y, BiomeType biome, float decorationNoise)
{
    if (y >= m_h - 3) return; // 边界检查
    
    switch (biome) {
        case BiomeType::Mountain:
            // 生成巨石
            set(x, y + 1, TileType::Rock);
            if (decorationNoise > 0.9f && y < m_h - 4) {
                set(x, y + 2, TileType::Rock);
            }
            break;
        case BiomeType::Desert:
            // 生成仙人掌
            set(x, y + 1, TileType::Wood); // 用木材代表仙人掌
            if (decorationNoise > 0.95f && y < m_h - 4) {
                set(x, y + 2, TileType::Wood);
            }
            break;
        case BiomeType::Tundra:
            // 生成冰块装饰
            set(x, y + 1, TileType::Crystal); // 用水晶代表冰晶
            break;
        default:
            break;
    }
}

void TileMap::generateSmallDecorationForBiome(int x, int y, BiomeType biome, float primaryNoise)
{
    if (y >= m_h - 2) return; // 边界检查
    
    switch (biome) {
        case BiomeType::Plains:
            // 生成花朵
            set(x, y + 1, TileType::Flower);
            break;
        case BiomeType::Forest:
            if (primaryNoise > 0.9f) {
                set(x, y + 1, TileType::Mushroom); // 蘑菇
            } else {
                set(x, y + 1, TileType::Grass_Decoration); // 浆果丛
            }
            break;
        case BiomeType::Beach:
            set(x, y + 1, TileType::Crystal); // 贝壳
            break;
        case BiomeType::Swamp:
            if (primaryNoise > 0.9f) {
                set(x, y + 1, TileType::Wood); // 枯木
            } else {
                set(x, y + 1, TileType::Grass_Decoration); // 沼泽草
            }
            break;
        default:
            break;
    }
}

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
    // 泰拉瑞亚风格的分层系统
    if (depth <= 0) return getSurfaceMaterial(biome);
    
    // 计算相对深度（基于地图高度的百分比）
    float relativeDepth = static_cast<float>(depth) / static_cast<float>(m_h);
    
    // 浅层（1-6格深）- 表土层
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
    
    // 中层（7-25%深度）- 石层过渡
    if (relativeDepth <= 0.25f) {
        return TileType::Stone;
    }
    
    // 深层（25%-60%深度）- 主石层
    if (relativeDepth <= 0.6f) {
        return TileType::Stone;
    }
    
    // 地狱入口层（60%-75%深度）- 黑曜石过渡层
    if (relativeDepth <= 0.75f) {
        // 随机生成黑曜石和石头的混合
        float obsidianNoise = m_noiseGen.caveNoise(static_cast<float>(depth), static_cast<float>(depth));
        if (obsidianNoise > 0.4f) {
            return TileType::Obsidian;
        }
        return TileType::Stone;
    }
    
    // 地狱层（75%-90%深度）- 主要是黑曜石
    if (relativeDepth <= 0.9f) {
        return TileType::Obsidian;
    }
    
    // 基岩层（90%-100%深度）- 最底层
    return TileType::Bedrock;
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
    
    // 大幅降低阈值，让矿物更分散
    switch (ore) {
        case OreType::Coal:
            return depth >= 5 && depth <= 30 && oreNoise > 0.4f;  // 降低阈值
        case OreType::Iron:
            return depth >= 10 && depth <= 40 && oreNoise > 0.45f; // 降低阈值
        case OreType::Gold:
            return depth >= 20 && depth <= 50 && oreNoise > 0.5f;  // 降低阈值
        case OreType::Diamond:
            return depth >= 35 && depth <= 60 && oreNoise > 0.55f; // 降低阈值
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

} // namespace Tina::Game
