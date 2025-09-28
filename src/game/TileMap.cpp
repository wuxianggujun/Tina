//
// TileMap（地形与水体生成）实现

#include "TileMap.hpp"
#include <cmath>
#include <algorithm>

namespace Tina::Game {

// Thomas Wang 的无符号整数哈希
static inline uint32_t hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float TileMap::noise1(int x) const
{
    uint32_t n = hash_u32((uint32_t)x ^ (m_seed * 0x9e3779b1u));
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float TileMap::noise2(int x, int y) const
{
    uint32_t n = hash_u32(((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ (m_seed * 83492791u));
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}

float TileMap::noise2f(float x, float y) const
{
    // 将连续坐标映射为双线性插值的值噪声，形成空间相关性
    int xi = (int)std::floor(x);
    int yi = (int)std::floor(y);
    float fx = x - (float)xi;
    float fy = y - (float)yi;

    // 样本四个整点噪声
    float v00 = noise2(xi,     yi    );
    float v10 = noise2(xi + 1, yi    );
    float v01 = noise2(xi,     yi + 1);
    float v11 = noise2(xi + 1, yi + 1);

    // 平滑插值（smoothstep）以减少方格感
    auto smooth = [](float t){ return t * t * (3.0f - 2.0f * t); };
    float sx = smooth(fx);
    float sy = smooth(fy);

    float vx0 = v00 + (v10 - v00) * sx;
    float vx1 = v01 + (v11 - v01) * sx;
    return vx0 + (vx1 - vx0) * sy;
}

void TileMap::generate()
{
    // 地表高度：由多频正弦 + 抖动噪声叠加
    const float H0 = m_h * 0.55f;
    m_seaLevel = (int)(m_h * 0.22f); // 海平面（越小水越少）
    for (int x = 0; x < m_w; ++x) {
        float h = H0;
        const float t = float(x);
        h += 8.0f * std::sinf(t * 2.0f * 3.1415926f / 64.0f);
        h += 4.0f * std::sinf(t * 2.0f * 3.1415926f / 23.0f + 1.7f);
        h += 3.0f * (noise1(x) - 0.5f) * 2.0f;
        int surface = (int)std::round(std::fmin(std::fmax(h, 8.0f), float(m_h - 8)));

        for (int y = 0; y < m_h; ++y) {
            if (y > surface) {
                set(x, y, TileType::Air);
                continue;
            }

            // 垂直分层
            if (y == surface) {
                set(x, y, TileType::Grass);
            } else if (surface - y <= 6) {
                set(x, y, TileType::Dirt);
            } else {
                set(x, y, TileType::Stone);
            }
        }

        // 洞穴挖掘（地表以下）：使用相关噪声以形成团块而非散点
        for (int y = surface - 2; y >= 2; --y) {
            // 频率缩放，系数越小结构越大
            float n = noise2f(x * 0.12f, y * 0.12f);
            float d = float(surface - y);
            // 越深越多
            float p = 0.05f + 0.0025f * d;
            if (n < p) set(x, y, TileType::Air);
        }

        // 海水填充：海平面以下的空腔填充为水
        for (int y = 0; y <= m_seaLevel; ++y) {
            if (get(x, y) == TileType::Air) set(x, y, TileType::Water);
        }
    }

    // 生成湖泊：选取若干椭圆区域，挖槽并在下半部位注水
    const int lakeCount = std::max(2, m_w / 80);
    for (int i = 0; i < lakeCount; ++i) {
        int cx = (int)(noise1(i * 17) * (m_w - 20)) + 10;
        int cy = (int)(m_h * (0.35f + 0.25f * noise1(i * 31)));
        float rx = 6.0f + 10.0f * noise1(i * 47);
        float ry = 4.0f + 8.0f * noise1(i * 61);

        int x0 = std::max(1,  cx - (int)rx - 1);
        int x1 = std::min(m_w - 2, cx + (int)rx + 1);
        int y0 = std::max(1,  cy - (int)ry - 1);
        int y1 = std::min(m_h - 2, cy + (int)ry + 1);

        for (int x = x0; x <= x1; ++x) {
            // 找到该列的地表高度（从上往下第一个非空气/非水的格子）
            int groundY = -1;
            for (int yy = m_h - 1; yy >= 0; --yy) {
                TileType t = get(x, yy);
                if (t != TileType::Air && t != TileType::Water) { groundY = yy; break; }
            }
            if (groundY <= 0) continue;

            for (int y = y0; y <= y1; ++y) {
                float dx = (x - cx) / rx;
                float dy = (y - cy) / ry;
                if (dx*dx + dy*dy > 1.0f) continue;

                // 仅作用于地表以下，避免天空出现“悬浮水”
                if (y > groundY) continue;

                // 将下半椭圆（y <= cy）注水；其上半（cy < y <= groundY）挖空为空气
                if (y <= cy) {
                    set(x, y, TileType::Water);
                } else {
                    set(x, y, TileType::Air);
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
    if (get(x, y) != TileType::Water) return;

    // 随机场方向 + 海水向右偏移的趋势
    float n = noise2f(wx * 0.5f, wy * 0.5f); // 平滑一些
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
    if ((int)m_work.size() != W * H) m_work.resize((size_t)W * H, TileType::Air);

    bool anyChanged = false;
    int minx = W, miny = H, maxx = -1, maxy = -1;

    for (int it = 0; it < iterations; ++it) {
        // 拷贝当前到工作缓冲
        std::copy(m_tiles.begin(), m_tiles.end(), m_work.begin());

        bool changed = false;

        // 自顶向下扫描，避免同一列中的“多水同落”冲突；左右交替打破偏置
        for (int y = H - 1; y >= 0; --y) {
            const bool invert = ((y + (int)m_tick) & 1) != 0;
            if (!invert) {
                for (int x = 0; x < W; ++x) {
                    if (m_tiles[index(x,y)] != TileType::Water) continue;
                    // 目标顺序：下 -> 斜下(偏置) -> 斜下(反向) -> 侧向(偏置) -> 侧向(反向)
                    auto tryMove = [&](int nx, int ny)->bool{
                        if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)H) return false;
                        if (m_tiles[index(nx,ny)] != TileType::Air) return false; // 仅向空气流动
                        if (m_work[index(nx,ny)] != TileType::Air) return false;   // 目的地已被占用
                        m_work[index(nx,ny)] = TileType::Water;
                        m_work[index(x,y)]   = TileType::Air;
                        minx = std::min(minx, std::min(x,nx));
                        maxx = std::max(maxx, std::max(x,nx));
                        miny = std::min(miny, std::min(y,ny));
                        maxy = std::max(maxy, std::max(y,ny));
                        return true;
                    };
                    if (y > 0) {
                        if (tryMove(x, y-1)) { changed = true; continue; }
                        int dir = ((x + y + (int)m_tick) & 1) ? -1 : +1;
                        if (tryMove(x+dir, y-1)) { changed = true; continue; }
                        if (tryMove(x-dir, y-1)) { changed = true; continue; }
                    }
                    // 仅在无法下落时尝试侧向扩散
                    int dir = ((x + y + (int)m_tick) & 1) ? -1 : +1;
                    if (tryMove(x+dir, y)) { changed = true; continue; }
                    if (tryMove(x-dir, y)) { changed = true; continue; }
                }
            } else {
                for (int x = W - 1; x >= 0; --x) {
                    if (m_tiles[index(x,y)] != TileType::Water) continue;
                    auto tryMove = [&](int nx, int ny)->bool{
                        if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)H) return false;
                        if (m_tiles[index(nx,ny)] != TileType::Air) return false;
                        if (m_work[index(nx,ny)] != TileType::Air) return false;
                        m_work[index(nx,ny)] = TileType::Water;
                        m_work[index(x,y)]   = TileType::Air;
                        minx = std::min(minx, std::min(x,nx));
                        maxx = std::max(maxx, std::max(x,nx));
                        miny = std::min(miny, std::min(y,ny));
                        maxy = std::max(maxy, std::max(y,ny));
                        return true;
                    };
                    if (y > 0) {
                        if (tryMove(x, y-1)) { changed = true; continue; }
                        int dir = ((x + y + (int)m_tick) & 1) ? -1 : +1;
                        if (tryMove(x+dir, y-1)) { changed = true; continue; }
                        if (tryMove(x-dir, y-1)) { changed = true; continue; }
                    }
                    int dir = ((x + y + (int)m_tick) & 1) ? -1 : +1;
                    if (tryMove(x+dir, y)) { changed = true; continue; }
                    if (tryMove(x-dir, y)) { changed = true; continue; }
                }
            }
        }

        if (!changed) { break; }
        anyChanged = true;
        // 推进：交换缓冲
        m_tiles.swap(m_work);
        ++m_tick;
    }

    outMinX = minx; outMinY = miny; outMaxX = maxx; outMaxY = maxy;
    return anyChanged;
}

} // namespace Tina::Game
