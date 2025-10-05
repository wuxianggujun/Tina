//
// 玩家输入系统 - 处理玩家输入，更新PlayerController组件
//

#pragma once

#include "../Components.hpp"
#include "../World.hpp"

namespace Tina::ECS {

class PlayerInputSystem {
public:
    void update(entt::registry& registry, const InputState& input);
};

} // namespace Tina::ECS
