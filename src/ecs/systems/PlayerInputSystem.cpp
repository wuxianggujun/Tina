//
// 玩家输入系统实现
//

#include "PlayerInputSystem.hpp"

namespace Tina::ECS {

void PlayerInputSystem::update(entt::registry& registry, const InputState& input) {
    // 遍历所有拥有PlayerController的实体
    auto view = registry.view<PlayerController>();
    for (auto entity : view) {
        auto& controller = view.get<PlayerController>(entity);

        // 更新输入状态
        controller.moveLeft = input.moveLeft;
        controller.moveRight = input.moveRight;
        controller.jumpHeld = input.jump;
    }
}

} // namespace Tina::ECS
