//
// 物理系统 - 应用重力、碰撞检测、位置更新
//

#pragma once

#include "../Components.hpp"
#include "../../game/TileMap.hpp"

namespace Tina::ECS {

class PhysicsSystem {
public:
    void update(entt::registry& registry, const Tina::Game::TileMap& tilemap, float dt);

private:
    // 碰撞检测辅助函数（复用Player.cpp的逻辑）
    bool isSolid(int tx, int ty, const Tina::Game::TileMap& tilemap) const;
    bool checkCollision(float nx, float ny, float width, float height, const Tina::Game::TileMap& tilemap) const;
};

} // namespace Tina::ECS
