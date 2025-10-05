//
// 物理系统实现（复用Player.cpp的碰撞检测逻辑）
//

#include "PhysicsSystem.hpp"
#include <cmath>
#include <algorithm>

namespace Tina::ECS {

bool PhysicsSystem::isSolid(int tx, int ty, const Tina::Game::TileMap& tilemap) const {
    if (tx < 0 || ty < 0 || tx >= tilemap.width() || ty >= tilemap.height()) {
        return true; // 边界视为固体
    }
    Tina::Game::TileType t = tilemap.get(tx, ty);
    return tilemap.isSolidTile(t);
}

bool PhysicsSystem::checkCollision(float nx, float ny, float width, float height,
                                   const Tina::Game::TileMap& tilemap) const {
    // 计算玩家覆盖的瓦片范围
    const float EPS = 1e-6f;
    float left   = nx;
    float right  = nx + width  - EPS;
    float bottom = ny;
    float top    = ny + height - EPS;
    int x0 = (int)std::floor(left);
    int y0 = (int)std::floor(bottom);
    int x1 = (int)std::floor(right);
    int y1 = (int)std::floor(top);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (isSolid(x, y, tilemap)) {
                // 检查精确重叠
                float tileL = (float)x;
                float tileR = (float)(x + 1);
                float tileB = (float)y;
                float tileT = (float)(y + 1);

                float playerL = left;
                float playerR = right + EPS;
                float playerB = bottom;
                float playerT = top + EPS;

                // AABB重叠检测
                if (playerR > tileL && playerL < tileR &&
                    playerT > tileB && playerB < tileT) {
                    return true;
                }
            }
        }
    }
    return false;
}

void PhysicsSystem::update(entt::registry& registry, const Tina::Game::TileMap& tilemap, float dt) {
    auto view = registry.view<Transform, Velocity, PhysicsBody>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& velocity = view.get<Velocity>(entity);
        auto& physics = view.get<PhysicsBody>(entity);

        if (dt <= 0.0f) continue;

        // 1. 应用重力
        velocity.vy -= physics.gravity * dt;

        // 限制最大下落速度
        velocity.vy = std::max(velocity.vy, -40.0f);

        // 2. 水平移动 + 碰撞
        float newX = transform.x + velocity.vx * dt;
        if (checkCollision(newX, transform.y, physics.width, physics.height, tilemap)) {
            // 对齐到网格边界
            if (velocity.vx > 0.0f) {
                int ceilRight = (int)std::ceil(transform.x + physics.width - 1e-6f);
                newX = (float)ceilRight - physics.width - 0.001f;
            } else if (velocity.vx < 0.0f) {
                int floorLeft = (int)std::floor(transform.x + 1e-6f);
                newX = (float)floorLeft + 0.001f;
            }
            velocity.vx = 0.0f;
        }
        transform.x = newX;

        // 3. 垂直移动 + 碰撞
        float newY = transform.y + velocity.vy * dt;
        if (checkCollision(transform.x, newY, physics.width, physics.height, tilemap)) {
            if (velocity.vy > 0.0f) {
                // 向上撞到天花板
                int ceilTop = (int)std::ceil(transform.y + physics.height - 1e-6f);
                newY = (float)ceilTop - physics.height - 0.001f;
                velocity.vy = 0.0f;
            } else if (velocity.vy < 0.0f) {
                // 向下落地
                int floorBottom = (int)std::floor(transform.y + 1e-6f);
                newY = (float)floorBottom + 0.001f;
                velocity.vy = 0.0f;
                physics.onGround = true;
            }
        } else {
            physics.onGround = false;
        }
        transform.y = newY;

        // 4. 脚底探测（更稳健）
        if (!physics.onGround) {
            const float EPS = 1e-6f;
            int feetY = (int)std::floor(transform.y - EPS);
            int x0 = (int)std::floor(transform.x + EPS);
            int x1 = (int)std::floor(transform.x + physics.width - EPS);
            for (int x = x0; x <= x1; ++x) {
                if (isSolid(x, feetY, tilemap)) {
                    physics.onGround = true;
                    break;
                }
            }
        }

        // 5. 防止掉出地图
        if (transform.y < 0.0f) {
            transform.y = 0.0f;
            velocity.vy = 0.0f;
            physics.onGround = true;
        }
        if (transform.y > tilemap.height() - physics.height) {
            transform.y = tilemap.height() - physics.height;
            velocity.vy = 0.0f;
        }
        if (transform.x < 0.0f) {
            transform.x = 0.0f;
            velocity.vx = 0.0f;
        }
        if (transform.x > tilemap.width() - physics.width) {
            transform.x = tilemap.width() - physics.width;
            velocity.vx = 0.0f;
        }
    }
}

} // namespace Tina::ECS
