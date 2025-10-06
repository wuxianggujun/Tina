#pragma once

#include "../core/Signal.hpp"
#include "../core/Math.hpp"
#include "../os/OS.hpp"

namespace Tina::Engine {

// 事件总线：将 OS 事件转发为可订阅的 Signal
class EventBus {
public:
    // 窗口事件
    Core::Signal<int, int> onWindowResized;  // (width, height)
    Core::Signal<>         onWindowClosed;

    // 键盘事件
    Core::Signal<int, bool> onKeyPressed;    // (keycode, isRepeat)
    Core::Signal<int>       onKeyReleased;

    // 鼠标事件
    Core::Signal<int, int, int> onMouseButtonPressed;   // (button, x, y) 暂无坐标则传 0
    Core::Signal<int, int, int> onMouseButtonReleased;
    Core::Signal<int, int>      onMouseMoved;           // (dx, dy) 相对位移
    Core::Signal<float>         onMouseWheel;           // 垂直滚轮增量

    // 示例：游戏事件
    Core::Signal<float, float> onPlayerMoved;  // (x, y)
    Core::Signal<>             onPlayerJumped;
    Core::Signal<>             onPlayerDied;

    // 工具使用
    Core::Signal<int, int> onToolUsed;  // (x, y)

    // 分发 OS 事件
    void dispatchOSEvent(const Tina::os::Event& event);
};

} // namespace Tina::Engine
