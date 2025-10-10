//
// GameEvents - 游戏层强类型事件定义
// - 将原 EventBus 中的玩法事件迁移为强类型事件

#pragma once

namespace Tina::Game::Events {

struct SetDayNight {
    float normalized = 0.0f; // [0,1)
};

struct AdjustDayNight {
    float delta = 0.0f; // 可正可负
};

struct PlayerJumped {
};

struct PlayerMoved {
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace Tina::Game::Events

