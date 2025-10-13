//
// GameEvents - 游戏层强类型事件定义
// - 使用新事件系统（基于 CRTP 的 Event 模板）
// - 游戏自定义事件使用 CustomEvent 模板，无需修改引擎代码

#pragma once
#include "../engine/EventCore.hpp"

namespace Tina::Game::Events {

// ==================== 游戏自定义事件（使用 CustomEvent） ====================
// 所有游戏特定事件都使用 CustomEvent，无需修改引擎代码

// 玩家事件
struct PlayerMoved : public Engine::CustomEvent<PlayerMoved> {
    float x = 0.0f;
    float y = 0.0f;
};

struct PlayerJumped : public Engine::CustomEvent<PlayerJumped> {
};

// 昼夜系统事件
struct SetDayNight : public Engine::CustomEvent<SetDayNight> {
    float normalized = 0.0f; // [0,1)
};

struct AdjustDayNight : public Engine::CustomEvent<AdjustDayNight> {
    float delta = 0.0f; // 可正可负
};

// 控制系统事件
struct SwitchControlEntity : public Engine::CustomEvent<SwitchControlEntity> {
    // 事件本身不需要携带数据，GameScene 知道当前点击的是哪个角色
};

} // namespace Tina::Game::Events
