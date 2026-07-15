//
// 角色移动系统 - 根据PlayerController或AIController更新速度
//

#pragma once

#include "../Components.hpp"

namespace Tina::ECS {

class CharacterMovementSystem {
public:
    void update(entt::registry& registry, float dt);
};

} // namespace Tina::ECS
