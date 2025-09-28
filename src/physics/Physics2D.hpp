//
// 轻量 2D 物理（Box2D 封装）
// - 管理重力世界、固定步更新
// - 支持生成临时碎块（Debris）用于爆炸演示

#pragma once

#include <box2d/box2d.h>
#include <cstddef>
#include "../core/Container.hpp"

namespace Tina::Physics {

struct Debris {
    b2BodyId body = {0};
    float r = 1, g = 1, b = 1, a = 1;
    float size = 1.0f; // 方块边长（世界单位）
};

class Physics2D {
public:
    explicit Physics2D(float gravityY = -20.0f);
    ~Physics2D();

    void step(float dt, int velIters = 8, int posIters = 3);

    // 矩形边界（静态刚体），避免碎块飞出场景
    void createBounds(float minx, float miny, float maxx, float maxy, float thickness = 1.0f);

    // 生成碎块（动态刚体），初速度（vx, vy）
    Debris* spawnDebris(float cx, float cy, float size, float r, float g, float b, float a,
                        float vx, float vy, float density = 1.0f, float friction = 0.4f, float restitution = 0.1f);

    const Tina::Container::Vector<Debris>& debris() const { return m_debris; }

    b2WorldId world() const { return m_world; }

    // 设置碎块数量上限，并进行回收（超过上限则销毁最旧碎块）
    void setDebrisLimit(std::size_t limit) { m_debrisLimit = limit; }
    void cleanupDebris();

private:
    b2WorldId m_world {0};
    float m_minx = -1000.0f, m_miny = -1000.0f, m_maxx = 1000.0f, m_maxy = 1000.0f;
    std::size_t m_debrisLimit = 1500;
    Tina::Container::Vector<Debris> m_debris;
};

} // namespace Tina::Physics
