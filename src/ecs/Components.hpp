//
// ECS 组件定义（纯数据，无逻辑）
//

#pragma once

#include "../core/Core.hpp"
#include <entt/entt.hpp>

namespace Tina::ECS {

// ====================
// 核心组件
// ====================

// 1. Transform - 位置和方向（世界坐标，单位=格）
struct Transform {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f; // 未来扩展用（如：角色朝向）
};

// 2. Velocity - 速度
struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
};

// 3. PhysicsBody - 物理体（碰撞盒 + 重力）
struct PhysicsBody {
    float width = 0.8f;   // 碰撞盒宽度
    float height = 2.0f;  // 碰撞盒高度
    float gravity = 32.0f; // 重力加速度
    bool onGround = false; // 是否在地面
};

// 4. CharacterController - 角色移动参数
struct CharacterController {
    float moveSpeed = 8.0f;   // 最大水平速度
    float jumpSpeed = 14.0f;  // 跳跃初速度
    float airControl = 0.3f;  // 空中控制力（0-1）
};

// ====================
// 控制组件
// ====================

// 5. PlayerController - 玩家输入状态（只有被控制的角色有这个组件）
struct PlayerController {
    bool moveLeft = false;
    bool moveRight = false;
    bool jumpHeld = false;
};

// 6. AIController - AI状态（AI控制的角色有这个组件）
struct AIController {
    enum class Behavior {
        Idle,    // 待机
        Wander,  // 随机游荡
        Follow,  // 跟随目标
        Flee     // 逃跑
    };

    Behavior behavior = Behavior::Wander;
    float thinkTimer = 0.0f;      // 当前思考计时
    float nextThinkTime = 1.0f;   // 下次思考时间

    // AI决策输出（类似PlayerController的输入）
    bool wantMoveLeft = false;
    bool wantMoveRight = false;
    bool wantJump = false;

    // AI行为参数
    entt::entity followTarget = entt::null; // 跟随目标
    float wanderDirection = 1.0f;           // 游荡方向（-1左，1右）
};

// ====================
// 渲染组件
// ====================

// 7. Renderable - 渲染信息
struct Renderable {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f; // 颜色
    bool visible = true;
};

// ====================
// 标记组件（用于筛选实体）
// ====================

// 标记为"角色"
struct CharacterTag {};

} // namespace Tina::ECS
