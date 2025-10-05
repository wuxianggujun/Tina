//
// 角色移动系统实现
//

#include "CharacterMovementSystem.hpp"
#include <cmath>
#include <algorithm>

namespace Tina::ECS {

void CharacterMovementSystem::update(entt::registry& registry, float dt) {
    // 处理玩家控制的角色
    {
        auto view = registry.view<Transform, Velocity, CharacterController, PhysicsBody, PlayerController>();
        for (auto entity : view) {
            auto& velocity = view.get<Velocity>(entity);
            auto& controller = view.get<CharacterController>(entity);
            auto& physics = view.get<PhysicsBody>(entity);
            auto& playerCtrl = view.get<PlayerController>(entity);

            // 水平移动输入
            float moveInput = 0.0f;
            if (playerCtrl.moveLeft) moveInput -= 1.0f;
            if (playerCtrl.moveRight) moveInput += 1.0f;

            // 泰拉瑞亚式移动：有输入时加速，无输入时立即停止
            if (std::abs(moveInput) > 0.01f) {
                // 根据是否在地面调整加速度
                float accel = physics.onGround
                    ? controller.moveSpeed * 20.0f
                    : controller.moveSpeed * 20.0f * controller.airControl;
                velocity.vx += moveInput * accel * dt;
                // 限制最大水平速度
                velocity.vx = std::clamp(velocity.vx, -controller.moveSpeed, controller.moveSpeed);
            } else {
                // 无输入时立即停止
                velocity.vx = 0.0f;
            }

            // 跳跃（长按自动连跳）
            if (playerCtrl.jumpHeld && physics.onGround) {
                velocity.vy = controller.jumpSpeed;
                physics.onGround = false;
            }
        }
    }

    // 处理AI控制的角色
    {
        auto view = registry.view<Transform, Velocity, CharacterController, PhysicsBody, AIController>();
        for (auto entity : view) {
            auto& velocity = view.get<Velocity>(entity);
            auto& controller = view.get<CharacterController>(entity);
            auto& physics = view.get<PhysicsBody>(entity);
            auto& aiCtrl = view.get<AIController>(entity);

            // AI移动输入
            float moveInput = 0.0f;
            if (aiCtrl.wantMoveLeft) moveInput -= 1.0f;
            if (aiCtrl.wantMoveRight) moveInput += 1.0f;

            // 同样的移动逻辑
            if (std::abs(moveInput) > 0.01f) {
                float accel = physics.onGround
                    ? controller.moveSpeed * 20.0f
                    : controller.moveSpeed * 20.0f * controller.airControl;
                velocity.vx += moveInput * accel * dt;
                velocity.vx = std::clamp(velocity.vx, -controller.moveSpeed, controller.moveSpeed);
            } else {
                velocity.vx = 0.0f;
            }

            // AI跳跃
            if (aiCtrl.wantJump && physics.onGround) {
                velocity.vy = controller.jumpSpeed;
                physics.onGround = false;
            }
        }
    }
}

} // namespace Tina::ECS
