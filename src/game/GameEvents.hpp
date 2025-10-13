//
// GameEvents - 游戏层强类型事件定义
// - 使用新事件系统（基于 CRTP 的 Event 模板）
// - 游戏自定义事件使用 CustomEvent 模板，无需修改引擎代码

#pragma once
#include "../engine/EventCore.hpp"

namespace Tina::Game::Events {

// ==================== 引擎预定义事件（可选使用） ====================

// 设置昼夜时间事件
struct SetDayNight : public Engine::Event<SetDayNight, Engine::EventTypeId::SetDayNight> {
    float normalized = 0.0f; // [0,1)
};

// 玩家跳跃事件
struct PlayerJumped : public Engine::Event<PlayerJumped, Engine::EventTypeId::PlayerJumped> {
};

// 玩家移动事件
struct PlayerMoved : public Engine::Event<PlayerMoved, Engine::EventTypeId::PlayerMoved> {
    float x = 0.0f;
    float y = 0.0f;
};

// ==================== 游戏自定义事件（使用 CustomEvent） ====================
// 这些事件不需要在引擎中预定义，完全由游戏层控制

// 调整昼夜时间事件（游戏自定义）
struct AdjustDayNight : public Engine::CustomEvent<AdjustDayNight> {
    float delta = 0.0f; // 可正可负
};

// 切换控制角色事件（游戏自定义）
struct SwitchControlEntity : public Engine::CustomEvent<SwitchControlEntity> {
    // 事件本身不需要携带数据，GameScene 知道当前点击的是哪个角色
};

} // namespace Tina::Game::Events
