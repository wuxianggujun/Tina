//
// 轻量 2D 物理（Box2D 封装）
// - 管理碎块（debris）、静态边界
// - 支持碎块的寿命管理（Debris 用于爆破展示）

#pragma once

#include <box2d/box2d.h>
#include <cstddef>
#include "../core/Container.hpp"

namespace Tina::Physics {

struct Debris {
    b2BodyId body = {0};
    float r = 1, g = 1, b = 1, a = 1;
    float size = 1.0f; // 可视方块尺寸（世界单位）
    float life = 0.0f;     // 剩余寿命（秒）
    float lifeMax = 0.0f;  // 初始寿命（秒）
    float a0 = 1.0f;       // 初始 alpha，用于渐隐
};

class Physics2D {
public:
    explicit Physics2D(float gravityY = -20.0f);
    ~Physics2D();

    void step(float dt, int velIters = 8, int posIters = 3);

    // 创建边界（静态刚体），限制碎块范围
    void createBounds(float minx, float miny, float maxx, float maxy, float thickness = 1.0f);

    // 生成碎块（动态刚体），初速度 vx, vy
    Debris* spawnDebris(float cx, float cy, float size, float r, float g, float b, float a,
                        float vx, float vy, float density = 1.0f, float friction = 0.4f, float restitution = 0.1f,
                        float ttlSeconds = 6.0f);

    const Tina::Container::Vector<Debris>& debris() const { return m_debris; }

    b2WorldId world() const { return m_world; }

    // 限制碎块总数（超过后 FIFO 丢弃）
    void setDebrisLimit(std::size_t limit) { m_debrisLimit = limit; }
    // 按寿命衰减碎块（减少 life、渐隐、过期销毁）
    void decayDebris(float dt);
    // 清理越界/失效碎块，并按上限裁剪
    void cleanupDebris();

private:
    b2WorldId m_world {0};
    float m_minx = -1000.0f, m_miny = -1000.0f, m_maxx = 1000.0f, m_maxy = 1000.0f;
    std::size_t m_debrisLimit = 1500;
    Tina::Container::Vector<Debris> m_debris;
};

} // namespace Tina::Physics

