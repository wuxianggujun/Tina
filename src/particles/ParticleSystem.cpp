#include "ParticleSystem.hpp"
#include <bx/math.h>
#include <random>

namespace Tina::Particles {

namespace {
struct Vtx {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a; // 归一化颜色
};

static inline uint8_t toU8(float v) {
    v = bx::clamp(v, 0.0f, 1.0f);
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}
}

bool ParticleSystem2D::initialize(Tina::renderer::ShaderManager& sm, size_t capacity)
{
    m_particles.clear();
    m_particles.resize(capacity);

    // 顶点布局：与 particle_vs/fs.sc 对齐
    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
    .end();

    m_prog = sm.loadProgram("particle", "particle");
    return bgfx::isValid(m_prog);
}

void ParticleSystem2D::shutdown()
{
    // Program 由 ShaderManager 统一清理，这里无需销毁
}

void ParticleSystem2D::update(float dt)
{
    if (dt <= 0) return;
    for (auto& p : m_particles) {
        if (!p.alive) continue;
        p.life -= dt;
        if (p.life <= 0.0f) { p.alive = false; continue; }

        // 简单阻尼与加速度（视觉用）
        p.vx += m_ax * dt;
        p.vy += m_ay * dt;
        p.vx *= (1.0f - m_drag * dt);
        p.vy *= (1.0f - m_drag * dt);

        p.x += p.vx * dt;
        p.y += p.vy * dt;
    }
}

void ParticleSystem2D::render(uint16_t viewId)
{
    if (!bgfx::isValid(m_prog)) return;

    // 统计活跃数
    uint32_t alive = 0;
    for (auto& p : m_particles) if (p.alive) ++alive;
    if (alive == 0) return;

    const uint32_t maxV = alive * 4;
    const uint32_t maxI = alive * 6;

    if (bgfx::getAvailTransientVertexBuffer(maxV, m_layout) < maxV ||
        bgfx::getAvailTransientIndexBuffer(maxI) < maxI) {
        // 资源不足，简单降采样绘制一部分
        // 此处为简化，直接返回；也可分批次提交
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, maxV, m_layout);
    bgfx::allocTransientIndexBuffer(&tib, maxI);

    Vtx* vptr = reinterpret_cast<Vtx*>(tvb.data);
    uint16_t* iptr = reinterpret_cast<uint16_t*>(tib.data);

    uint32_t vb = 0, ib = 0;
    for (const auto& p : m_particles) {
        if (!p.alive) continue;
        const float t = (p.lifeMax > 0.0f) ? (p.life / p.lifeMax) : 0.0f;
        const float alpha = p.color.a() * bx::pow(bx::max(0.0f, t), 1.2f); // 随寿命衰减
        const float s = p.size;

        // 轴对齐四边形（世界坐标）
        const float x0 = p.x - s * 0.5f;
        const float y0 = p.y - s * 0.5f;
        const float x1 = p.x + s * 0.5f;
        const float y1 = p.y + s * 0.5f;

        const uint8_t cr = toU8(p.color.r());
        const uint8_t cg = toU8(p.color.g());
        const uint8_t cb = toU8(p.color.b());
        const uint8_t ca = toU8(alpha);

        vptr[vb + 0] = { x0, y0, 0.0f, 0.0f, 0.0f, cr,cg,cb,ca };
        vptr[vb + 1] = { x1, y0, 0.0f, 1.0f, 0.0f, cr,cg,cb,ca };
        vptr[vb + 2] = { x1, y1, 0.0f, 1.0f, 1.0f, cr,cg,cb,ca };
        vptr[vb + 3] = { x0, y1, 0.0f, 0.0f, 1.0f, cr,cg,cb,ca };

        iptr[ib + 0] = static_cast<uint16_t>(vb + 0);
        iptr[ib + 1] = static_cast<uint16_t>(vb + 1);
        iptr[ib + 2] = static_cast<uint16_t>(vb + 2);
        iptr[ib + 3] = static_cast<uint16_t>(vb + 0);
        iptr[ib + 4] = static_cast<uint16_t>(vb + 2);
        iptr[ib + 5] = static_cast<uint16_t>(vb + 3);

        vb += 4; ib += 6;
    }

    tvb.size = vb * sizeof(Vtx);
    tib.size = ib * sizeof(uint16_t);

    bgfx::Encoder* enc = bgfx::begin();
    if (!enc) return;
    enc->setVertexBuffer(0, &tvb);
    enc->setIndexBuffer(&tib);
    // 加色混合 + 始终通过深度，适合爆炸/火花
    // 使用 Alpha 混合获得烟尘/碎屑的柔和视觉，不走过曝的加色
    enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                  BGFX_STATE_BLEND_ALPHA | BGFX_STATE_DEPTH_TEST_ALWAYS);
    enc->submit(viewId, m_prog);
    bgfx::end(enc);
}

int ParticleSystem2D::spawnOne(float x, float y, float vx, float vy, float size, float life,
                               const Tina::Core::Color& color)
{
    for (size_t i = 0; i < m_particles.size(); ++i) {
        if (!m_particles[i].alive) {
            Particle& p = m_particles[i];
            p.x = x; p.y = y; p.vx = vx; p.vy = vy;
            p.size = size; p.life = life; p.lifeMax = life;
            p.color = color; p.alive = true;
            return static_cast<int>(i);
        }
    }
    return -1; // 容量已满
}

void ParticleSystem2D::explode(float wx, float wy, int count,
                               float speedMin, float speedMax,
                               float sizeMin, float sizeMax,
                               float lifeMin, float lifeMax,
                               const Tina::Core::Color& baseColor)
{
    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        const float ang = u01(rng) * bx::kPi2;
        const float spd = speedMin + (speedMax - speedMin) * u01(rng);
        const float vx = bx::cos(ang) * spd;
        const float vy = bx::sin(ang) * spd;
        const float size = sizeMin + (sizeMax - sizeMin) * u01(rng);
        const float life = lifeMin + (lifeMax - lifeMin) * u01(rng);
        const float jitter = 0.25f + 0.75f * u01(rng);

        // 基于基础颜色添加抖动
        const float r = bx::clamp(baseColor.r() * jitter, 0.0f, 1.0f);
        const float g = bx::clamp(baseColor.g() * jitter, 0.0f, 1.0f);
        const float b = bx::clamp(baseColor.b() * jitter, 0.0f, 1.0f);
        const float a = baseColor.a();

        spawnOne(wx, wy, vx, vy, size, life, Tina::Core::Color(r, g, b, a));
    }
}

} // namespace Tina::Particles
