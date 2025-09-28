//
// Physics2D 实现

#include "Physics2D.hpp"

namespace Tina::Physics {

Physics2D::Physics2D(float gravityY)
{
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = { 0.0f, gravityY };
    m_world = b2CreateWorld(&def);
}

Physics2D::~Physics2D()
{
    if (b2World_IsValid(m_world)) {
        b2DestroyWorld(m_world);
        m_world = {0};
    }
}

void Physics2D::step(float dt, int velIters, int posIters)
{
    // Box2D v3 采用子步数量，不区分速度/位置迭代。使用 4 个子步以提升稳定性。
    const int subSteps = 4;
    b2World_Step(m_world, dt, subSteps);
}

void Physics2D::createBounds(float minx, float miny, float maxx, float maxy, float thickness)
{
    m_minx = minx; m_miny = miny; m_maxx = maxx; m_maxy = maxy;
    // 四个边界用静态刚体 + 多边形形状实现
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_staticBody;
    b2BodyId body = b2CreateBody(m_world, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 0.0f;
    sd.material.friction = 0.8f;
    sd.material.restitution = 0.0f;

    // 底边
    {
        b2Polygon poly = b2MakeOffsetBox((maxx-minx)*0.5f, thickness*0.5f,
                                         b2Vec2{(minx+maxx)*0.5f, miny - thickness*0.5f}, b2Rot_identity);
        (void)b2CreatePolygonShape(body, &sd, &poly);
    }
    // 顶边
    {
        b2Polygon poly = b2MakeOffsetBox((maxx-minx)*0.5f, thickness*0.5f,
                                         b2Vec2{(minx+maxx)*0.5f, maxy + thickness*0.5f}, b2Rot_identity);
        (void)b2CreatePolygonShape(body, &sd, &poly);
    }
    // 左边
    {
        b2Polygon poly = b2MakeOffsetBox(thickness*0.5f, (maxy-miny)*0.5f,
                                         b2Vec2{minx - thickness*0.5f, (miny+maxy)*0.5f}, b2Rot_identity);
        (void)b2CreatePolygonShape(body, &sd, &poly);
    }
    // 右边
    {
        b2Polygon poly = b2MakeOffsetBox(thickness*0.5f, (maxy-miny)*0.5f,
                                         b2Vec2{maxx + thickness*0.5f, (miny+maxy)*0.5f}, b2Rot_identity);
        (void)b2CreatePolygonShape(body, &sd, &poly);
    }
}

Debris* Physics2D::spawnDebris(float cx, float cy, float size, float r, float g, float b, float a,
                               float vx, float vy, float density, float friction, float restitution,
                               float ttlSeconds)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = b2Vec2{cx, cy};
    b2BodyId body = b2CreateBody(m_world, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = density;
    sd.material.friction = friction;
    sd.material.restitution = restitution;

    b2Polygon box = b2MakeBox(size * 0.5f, size * 0.5f);
    (void)b2CreatePolygonShape(body, &sd, &box);

    b2Body_SetLinearVelocity(body, b2Vec2{vx, vy});
    b2Body_SetAngularVelocity(body, (vx*vy) * 0.05f);

    Debris d; d.body = body; d.r = r; d.g = g; d.b = b; d.a = a; d.size = size;
    d.life = ttlSeconds; d.lifeMax = ttlSeconds; d.a0 = a;
    m_debris.push_back(d);
    return &m_debris.back();
}

Debris* Physics2D::spawnWaterParticle(float cx, float cy, float size, float r, float g, float b, float a,
                                      float vx, float vy, float ttlSeconds)
{
    // 水粒子：低密度、低摩擦、基本无弹性
    Debris* d = spawnDebris(cx, cy, size, r, g, b, a,
                            vx, vy, /*density*/0.35f, /*friction*/0.05f, /*restitution*/0.0f,
                            /*ttl*/ttlSeconds);
    if (d) d->isWater = true;
    return d;
}

void Physics2D::decayDebris(float dt)
{
    for (size_t i = 0; i < m_debris.size(); ) {
        Debris& d = m_debris[i];
        if (!b2Body_IsValid(d.body)) { m_debris.erase(m_debris.begin() + (eastl_size_t)i); continue; }
        if (d.lifeMax > 0.0f) {
            d.life -= dt;
            const float k = (d.life > 0.0f) ? (d.life / d.lifeMax) : 0.0f;
            d.a = d.a0 * k; // 渐隐
        }
        if (d.lifeMax > 0.0f && d.life <= 0.0f) {
            b2DestroyBody(d.body);
            m_debris.erase(m_debris.begin() + (eastl_size_t)i);
        } else {
            ++i;
        }
    }
}

void Physics2D::cleanupDebris()
{
    // 超出范围或无效的碎块销毁；控制上限
    const float marginX = 20.0f;
    const float marginY = 60.0f;
    for (size_t i = 0; i < m_debris.size(); ) {
        Debris& d = m_debris[i];
        bool remove = false;
        if (!b2Body_IsValid(d.body)) {
            remove = true;
        } else {
            b2Vec2 p = b2Body_GetPosition(d.body);
            if (p.x < m_minx - marginX || p.x > m_maxx + marginX ||
                p.y < m_miny - marginY || p.y > m_maxy + marginY) {
                remove = true;
            } else if (!b2Body_IsAwake(d.body)) {
                // 睡眠体也可按需清理：这里不强制，但可扩展
            }
        }
        if (remove) {
            if (b2Body_IsValid(d.body)) b2DestroyBody(d.body);
            m_debris.erase(m_debris.begin() + (eastl_size_t)i);
        } else {
            ++i;
        }
    }
    // 如果仍超过上限，按 FIFO 方式回收
    while (m_debris.size() > m_debrisLimit) {
        Debris& d = m_debris.front();
        if (b2Body_IsValid(d.body)) b2DestroyBody(d.body);
        m_debris.erase(m_debris.begin());
    }
}
} // namespace Tina::Physics
