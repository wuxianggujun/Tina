//
// Events.hpp - 所有具体事件类型定义
// 职责：定义项目中所有事件的数据结构
// 使用：使用 TINA_EVENT 宏简化定义
//

#pragma once

#include "EventCore.hpp"
#include "InputSystem.hpp"  // KeyCode, MouseButton

namespace Tina::Engine::Events {

// ==================== 系统事件 ====================

// 窗口调整大小
TINA_EVENT_PRIORITY(WindowResizedEvent, WindowResized, High) {
    int width = 0;
    int height = 0;
};

// 窗口关闭
TINA_EVENT_PRIORITY(WindowClosedEvent, WindowClosed, High) {
    // 无额外数据
};

// 窗口获得焦点
TINA_EVENT(WindowFocusedEvent, WindowFocused) {
    // 无额外数据
};

// 窗口失去焦点
TINA_EVENT(WindowUnfocusedEvent, WindowUnfocused) {
    // 无额外数据
};

// 窗口最小化
TINA_EVENT(WindowMinimizedEvent, WindowMinimized) {
    // 无额外数据
};

// 窗口最大化
TINA_EVENT(WindowMaximizedEvent, WindowMaximized) {
    // 无额外数据
};

// 窗口恢复
TINA_EVENT(WindowRestoredEvent, WindowRestored) {
    // 无额外数据
};

// 窗口移动
TINA_EVENT(WindowMovedEvent, WindowMoved) {
    int x = 0;
    int y = 0;
};

// ==================== 输入事件 ====================

// 按键按下
TINA_EVENT(KeyPressedEvent, KeyPressed) {
    KeyCode code = KeyCode::Unknown;
    bool isRepeat = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

// 按键释放
TINA_EVENT(KeyReleasedEvent, KeyReleased) {
    KeyCode code = KeyCode::Unknown;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

// 文本输入
TINA_EVENT(TextInputEvent, TextInput) {
    uint32_t utf32 = 0;  // Unicode 字符
};

// 鼠标移动
TINA_EVENT(MouseMovedEvent, MouseMoved) {
    int x = 0;
    int y = 0;
    int deltaX = 0;
    int deltaY = 0;
};

// 鼠标按钮按下
TINA_EVENT(MouseButtonPressedEvent, MouseButtonPressed) {
    MouseButton button = MouseButton::Left;
    int x = 0;
    int y = 0;
    int clicks = 1;  // 点击次数（双击为 2）
};

// 鼠标按钮释放
TINA_EVENT(MouseButtonReleasedEvent, MouseButtonReleased) {
    MouseButton button = MouseButton::Left;
    int x = 0;
    int y = 0;
};

// 鼠标滚轮
TINA_EVENT(MouseWheelEvent, MouseWheel) {
    float deltaX = 0.0f;
    float deltaY = 0.0f;
};

// 文件拖放
TINA_EVENT(DropFileEvent, DropFile) {
    const char* path = nullptr;
};

// ==================== 游戏事件 ====================

// 玩家移动
TINA_EVENT(PlayerMovedEvent, PlayerMoved) {
    float x = 0.0f;
    float y = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
};

// 玩家跳跃
TINA_EVENT(PlayerJumpedEvent, PlayerJumped) {
    float height = 0.0f;
};

// 玩家死亡
TINA_EVENT(PlayerDiedEvent, PlayerDied) {
    int killerId = -1;  // 击杀者 ID（-1 表示环境伤害）
};

// 玩家重生
TINA_EVENT(PlayerRespawnedEvent, PlayerRespawned) {
    float x = 0.0f;
    float y = 0.0f;
};

// 敌人生成
TINA_EVENT(EnemySpawnedEvent, EnemySpawned) {
    int enemyId = 0;
    int enemyType = 0;
    float x = 0.0f;
    float y = 0.0f;
};

// 敌人死亡
TINA_EVENT(EnemyDiedEvent, EnemyDied) {
    int enemyId = 0;
    int killerId = -1;
};

// 物品拾取
TINA_EVENT(ItemPickedUpEvent, ItemPickedUp) {
    int itemId = 0;
    int itemType = 0;
};

// 物品丢弃
TINA_EVENT(ItemDroppedEvent, ItemDropped) {
    int itemId = 0;
    int itemType = 0;
    float x = 0.0f;
    float y = 0.0f;
};

// 物品使用
TINA_EVENT(ItemUsedEvent, ItemUsed) {
    int itemId = 0;
    int itemType = 0;
};

// 工具选择
TINA_EVENT(ToolSelectedEvent, ToolSelected) {
    int toolId = 0;
};

// 工具使用
TINA_EVENT(ToolUsedEvent, ToolUsed) {
    int toolId = 0;
    float worldX = 0.0f;
    float worldY = 0.0f;
};

// 游戏暂停
TINA_EVENT(GamePausedEvent, GamePaused) {
    // 无额外数据
};

// 游戏恢复
TINA_EVENT(GameResumedEvent, GameResumed) {
    // 无额外数据
};

// 游戏结束
TINA_EVENT(GameOverEvent, GameOver) {
    bool isWin = false;
    int finalScore = 0;
};

// 关卡加载
TINA_EVENT(LevelLoadedEvent, LevelLoaded) {
    int levelId = 0;
};

// 关卡卸载
TINA_EVENT(LevelUnloadedEvent, LevelUnloaded) {
    int levelId = 0;
};

// 昼夜切换
TINA_EVENT(SetDayNightEvent, SetDayNight) {
    float normalized = 0.5f;  // [0, 1)，0.0=午夜，0.5=正午
};

// 分数改变
TINA_EVENT(ScoreChangedEvent, ScoreChanged) {
    int oldScore = 0;
    int newScore = 0;
    int delta = 0;
};

// 生命值改变
TINA_EVENT(HealthChangedEvent, HealthChanged) {
    float oldHealth = 0.0f;
    float newHealth = 0.0f;
    float delta = 0.0f;
};

// ==================== UI 事件 ====================

// UI 按钮点击
TINA_EVENT_PRIORITY(UIButtonClickedEvent, UIButtonClicked, Low) {
    int buttonId = 0;
    const char* buttonName = nullptr;
};

// UI Hover 进入
TINA_EVENT_PRIORITY(UIHoverEnterEvent, UIHoverEnter, Low) {
    int elementId = 0;
};

// UI Hover 离开
TINA_EVENT_PRIORITY(UIHoverLeaveEvent, UIHoverLeave, Low) {
    int elementId = 0;
};

// UI 焦点获得
TINA_EVENT_PRIORITY(UIFocusGainedEvent, UIFocusGained, Low) {
    int elementId = 0;
};

// UI 焦点失去
TINA_EVENT_PRIORITY(UIFocusLostEvent, UIFocusLost, Low) {
    int elementId = 0;
};

// UI 文本改变
TINA_EVENT_PRIORITY(UITextChangedEvent, UITextChanged, Low) {
    int elementId = 0;
    const char* newText = nullptr;
};

} // namespace Tina::Engine::Events
