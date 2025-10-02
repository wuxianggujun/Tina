//
// 纯视觉2D粒子系统（不依赖物理引擎）
// - 粒子：位置(x,y)、速度(vx,vy)、颜色(r,g,b,a)、尺寸(size)、寿命(life, lifeMax)
// - 支持爆炸型发射（右键示例）与逐帧更新/渲染
// - 渲染：CPU 生成四边形顶点，bgfx Transient Buffer 提交，粒子着色器使用径向衰减
//

#pragma once

#include <bgfx/bgfx.h>
#include "../core/Container.hpp"
#include "../renderer/ShaderManager.hpp"

namespace Tina::Particles {

struct Particle {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    float size = 1.0f;
    float life = 0.0f;     // 剩余寿命（秒）
    float lifeMax = 0.0f;  // 初始寿命（秒）
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f; // 颜色（0..1）
    bool alive = false;
};

class ParticleSystem2D {
public:
    ParticleSystem2D() = default;
    ~ParticleSystem2D() = default;

    bool initialize(Tina::renderer::ShaderManager& sm, size_t capacity = 4096);
    void shutdown();

    // 每帧更新（秒）
    void update(float dt);

    // 渲染到指定视图（要求外部已设置正交投影与视口）
    void render(uint16_t viewId);

    // 右键爆炸示例：在 (wx,wy) 处生成若干粒子
    void explode(float wx, float wy,
                 int count = 300,
                 float speedMin = 8.0f, float speedMax = 22.0f,
                 float sizeMin = 0.15f, float sizeMax = 0.45f,
                 float lifeMin = 0.6f, float lifeMax = 1.2f,
                 float baseR = 1.0f, float baseG = 0.7f, float baseB = 0.2f);

    // 参数设置
    void setGlobalAcceleration(float ax, float ay) { m_ax = ax; m_ay = ay; }
    void setDrag(float drag) { m_drag = drag; }

private:
    int spawnOne(float x, float y,
                 float vx, float vy,
                 float size,
                 float life,
                 float r, float g, float b, float a);

private:
    Tina::Container::Vector<Particle> m_particles;
    bgfx::ProgramHandle m_prog = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout{}; // pos(3f) + uv(2f) + color0(u8 norm)

    // 全局加速度与阻尼（简单视觉效果）
    float m_ax = 0.0f;
    float m_ay = -9.8f;
    float m_drag = 0.15f; // 速度阻尼系数
};

} // namespace Tina::Particles

