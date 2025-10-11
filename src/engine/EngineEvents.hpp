//
// EngineEvents.hpp - 引擎核心事件定义
// 职责：定义系统级、输入、窗口等核心事件
// 使用高性能事件系统的强类型事件
//

#pragma once

#include "EventCore.hpp"
#include "../core/Math.hpp"
#include <string>

namespace Tina::Engine::Events {

// ==================== 输入事件 ====================

// 键盘按键代码（复用InputSystem的定义）
enum class KeyCode : u16 {
    Unknown = 0,

    // 字母键
    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G',
    H = 'H', I = 'I', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N',
    O = 'O', P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U',
    V = 'V', W = 'W', X = 'X', Y = 'Y', Z = 'Z',

    // 数字键
    Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
    Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',

    // 功能键
    F1 = 128, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // 控制键
    Escape = 27, Space = 32, Enter = 13, Tab = 9,
    Backspace = 8, Delete = 127, Insert = 150,

    // 方向键
    Left = 151, Right = 152, Up = 153, Down = 154,

    // 修饰键
    LeftShift = 160, RightShift = 161,
    LeftCtrl = 162, RightCtrl = 163,
    LeftAlt = 164, RightAlt = 165,

    MaxKeys = 256
};

// 鼠标按键代码
enum class MouseButton : u8 {
    Left = 0, Right = 1, Middle = 2,
    X1 = 3, X2 = 4,
    MaxButtons = 8
};

// 键盘按下事件
struct KeyPressedEvent : public Event<KeyPressedEvent, EventTypeId::KeyPressed> {
    KeyCode key;
    bool isRepeat;
    bool shift, ctrl, alt;

    KeyPressedEvent(KeyCode k = KeyCode::Unknown, bool repeat = false,
                   bool s = false, bool c = false, bool a = false)
        : key(k), isRepeat(repeat), shift(s), ctrl(c), alt(a)
    {
        this->priority = EventPriority::High;
    }
};

// 键盘释放事件
struct KeyReleasedEvent : public Event<KeyReleasedEvent, EventTypeId::KeyReleased> {
    KeyCode key;
    bool shift, ctrl, alt;

    KeyReleasedEvent(KeyCode k = KeyCode::Unknown,
                    bool s = false, bool c = false, bool a = false)
        : key(k), shift(s), ctrl(c), alt(a)
    {
        this->priority = EventPriority::High;
    }
};

// 文本输入事件（用于输入法）
struct TextInputEvent : public Event<TextInputEvent, EventTypeId::TextInput> {
    uint32_t utf32;
    std::string text;

    TextInputEvent(uint32_t code = 0, const std::string& str = "")
        : utf32(code), text(str)
    {
        this->priority = EventPriority::Medium;
    }
};

// 鼠标按下事件
struct MouseButtonPressedEvent : public Event<MouseButtonPressedEvent, EventTypeId::MouseButtonPressed> {
    MouseButton button;
    int x, y;
    int clicks; // 单击/双击/三击

    MouseButtonPressedEvent(MouseButton btn = MouseButton::Left,
                           int px = 0, int py = 0, int c = 1)
        : button(btn), x(px), y(py), clicks(c)
    {
        this->priority = EventPriority::High;
    }
};

// 鼠标释放事件
struct MouseButtonReleasedEvent : public Event<MouseButtonReleasedEvent, EventTypeId::MouseButtonReleased> {
    MouseButton button;
    int x, y;

    MouseButtonReleasedEvent(MouseButton btn = MouseButton::Left,
                            int px = 0, int py = 0)
        : button(btn), x(px), y(py)
    {
        this->priority = EventPriority::High;
    }
};

// 鼠标移动事件
struct MouseMovedEvent : public Event<MouseMovedEvent, EventTypeId::MouseMoved> {
    int x, y;
    int deltaX, deltaY;
    bool dragging;

    MouseMovedEvent(int px = 0, int py = 0, int dx = 0, int dy = 0, bool drag = false)
        : x(px), y(py), deltaX(dx), deltaY(dy), dragging(drag)
    {
        this->priority = EventPriority::Low;  // 移动事件优先级较低
    }
};

// 鼠标滚轮事件
struct MouseWheelEvent : public Event<MouseWheelEvent, EventTypeId::MouseWheel> {
    float deltaX, deltaY;
    int x, y; // 鼠标位置

    MouseWheelEvent(float dx = 0, float dy = 0, int px = 0, int py = 0)
        : deltaX(dx), deltaY(dy), x(px), y(py)
    {
        this->priority = EventPriority::Medium;
    }
};

// ==================== 窗口事件 ====================

// 窗口大小改变事件
struct WindowResizedEvent : public Event<WindowResizedEvent, EventTypeId::WindowResized> {
    int width, height;
    float aspectRatio;

    WindowResizedEvent(int w = 0, int h = 0)
        : width(w), height(h)
        , aspectRatio(h > 0 ? float(w) / float(h) : 1.0f)
    {
        this->priority = EventPriority::High;
    }
};

// 窗口移动事件
struct WindowMovedEvent : public Event<WindowMovedEvent, EventTypeId::WindowMoved> {
    int x, y;

    WindowMovedEvent(int px = 0, int py = 0)
        : x(px), y(py)
    {
        this->priority = EventPriority::Low;
    }
};

// 窗口获得焦点事件
struct WindowFocusGainedEvent : public Event<WindowFocusGainedEvent, EventTypeId::WindowFocused> {
    WindowFocusGainedEvent()
    {
        this->priority = EventPriority::Medium;
    }
};

// 窗口失去焦点事件
struct WindowFocusLostEvent : public Event<WindowFocusLostEvent, EventTypeId::WindowUnfocused> {
    WindowFocusLostEvent()
    {
        this->priority = EventPriority::Medium;
    }
};

// 窗口最小化事件
struct WindowMinimizedEvent : public Event<WindowMinimizedEvent, EventTypeId::WindowMinimized> {
    WindowMinimizedEvent()
    {
        this->priority = EventPriority::High;
    }
};

// 窗口恢复事件
struct WindowRestoredEvent : public Event<WindowRestoredEvent, EventTypeId::WindowRestored> {
    WindowRestoredEvent()
    {
        this->priority = EventPriority::High;
    }
};

// 窗口关闭事件
struct WindowClosedEvent : public Event<WindowClosedEvent, EventTypeId::WindowClosed> {
    bool cancelable;

    WindowClosedEvent(bool can_cancel = true)
        : cancelable(can_cancel)
    {
        this->priority = EventPriority::High;
    }
};

// ==================== 文件事件 ====================

// 文件拖放事件
struct FileDroppedEvent : public Event<FileDroppedEvent, EventTypeId::DropFile> {
    std::string filepath;
    int x, y; // 拖放位置

    FileDroppedEvent(const std::string& path = "", int px = 0, int py = 0)
        : filepath(path), x(px), y(py)
    {
        this->priority = EventPriority::Medium;
    }
};

} // namespace Tina::Engine::Events
