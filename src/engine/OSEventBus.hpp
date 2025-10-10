#pragma once

#include "../core/Signal.hpp"
#include "../core/Math.hpp"
#include "../os/OS.hpp"

namespace Tina::Engine {

// OS 事件总线：将操作系统输入/窗口事件转发为可订阅的 Signal
class OSEventBus {
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

    // 仅承载 OS 层事件；玩法/编辑器等高层事件请使用 TypedEventBus

    // 分发 OS 事件
    void dispatchOSEvent(const Tina::os::Event& event);
};

} // namespace Tina::Engine
