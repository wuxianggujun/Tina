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
                               float vx, float vy, float density, float friction, float restitution)
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
    m_debris.push_back(d);
    return &m_debris.back();
}

} // namespace Tina::Physics
