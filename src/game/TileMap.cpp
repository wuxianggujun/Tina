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
    // 初始化/清空水位
    m_water.resize((size_t)m_w * (size_t)m_h);
    std::fill(m_water.begin(), m_water.end(), (uint8_t)0);
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
            if (get(x, y) == TileType::Air) m_water[index(x,y)] = 255;
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
                if (t != TileType::Air) { groundY = yy; break; }
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
                    m_water[index(x,y)] = 255;
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
    if (m_water[index(x,y)] == 0) return;

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
    if ((int)m_work.size() != W * H) m_work.resize((size_t)W * H, (uint8_t)0);

    bool anyChanged = false;
    int minx = W, miny = H, maxx = -1, maxy = -1;

    const int MAX_DOWN = 64;  // 每步最大下落体积
    const int MAX_SIDE = 32;  // 每步最大侧流体积

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

        // 2) 侧向均衡
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W - 1; ++x) {
                if (get(x, y) != TileType::Air || get(x+1, y) != TileType::Air) continue;
                int a = (int)m_work[index(x,y)];
                int b = (int)m_work[index(x+1,y)];
                int diff = a - b;
                if (diff > 1) {
                    int move = std::min(diff/2, MAX_SIDE);
                    m_work[index(x,y)]     = (uint8_t)(a - move);
                    m_work[index(x+1, y)]  = (uint8_t)(b + move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x+1);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    changed = true;
                } else if (diff < -1) {
                    int move = std::min((-diff)/2, MAX_SIDE);
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

    const int MAX_DOWN = 64;
    const int MAX_SIDE = 32;

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

        // 2) 相邻对调（侧向均衡）
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W - 1; ++x) {
                if (get(x, y) != TileType::Air || get(x+1, y) != TileType::Air) continue;
                int a = (int)m_work[index(x,y)];
                int b = (int)m_work[index(x+1,y)];
                int diff = a - b;
                if (diff > 1) {
                    int move = std::min(diff/2, MAX_SIDE);
                    m_work[index(x,y)]     = (uint8_t)(a - move);
                    m_work[index(x+1, y)]  = (uint8_t)(b + move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x+1);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    changed = true;
                } else if (diff < -1) {
                    int move = std::min((-diff)/2, MAX_SIDE);
                    m_work[index(x,y)]     = (uint8_t)(a + move);
                    m_work[index(x+1, y)]  = (uint8_t)(b - move);
                    minx = std::min(minx, x); maxx = std::max(maxx, x+1);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    changed = true;
                }
            }
        }

        // 2.5) 水平“池化”均衡：同一行连续空气段体积均分
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

        if (!changed) break;
        anyChanged = true;
        m_water.swap(m_work);
        ++m_tick;
    }

    outMinX = minx; outMinY = miny; outMaxX = maxx; outMaxY = maxy;
    return anyChanged;
}

} // namespace Tina::Game
