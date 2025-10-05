//
// AI系统 - 简单的随机游荡AI
//

#pragma once

#include "../Components.hpp"

namespace Tina::ECS {

class AISystem {
public:
    void update(entt::registry& registry, float dt);
};

} // namespace Tina::ECS
