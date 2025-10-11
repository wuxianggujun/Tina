//
// GameEvents - 游戏层强类型事件定义
// - 使用新事件系统（基于 CRTP 的 Event 模板）

#pragma once
#include "../engine/EventCore.hpp"

namespace Tina::Game::Events {

// 设置昼夜时间事件
struct SetDayNight : public Engine::Event<SetDayNight, Engine::EventTypeId::SetDayNight> {
    float normalized = 0.0f; // [0,1)
};

// 调整昼夜时间事件（自定义扩展事件，使用未分配的 ID）
struct AdjustDayNight : public Engine::Event<AdjustDayNight, Engine::EventTypeId::Custom> {
    float delta = 0.0f; // 可正可负
};

// 玩家跳跃事件
struct PlayerJumped : public Engine::Event<PlayerJumped, Engine::EventTypeId::PlayerJumped> {
};

// 玩家移动事件
struct PlayerMoved : public Engine::Event<PlayerMoved, Engine::EventTypeId::PlayerMoved> {
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace Tina::Game::Events
