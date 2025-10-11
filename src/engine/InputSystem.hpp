//
// InputSystem.hpp - 输入系统
// 职责：管理键盘、鼠标、手柄等输入设备的状态查询
// 特点：提供"按下/持续/释放"三种状态查询，支持输入映射
//

#pragma once

#include "../core/Core.hpp"
#include "../core/Math.hpp"
#include <string>
#include <array>

namespace Tina::Engine {

// ==================== 输入代码定义 ====================

// 键盘按键代码
enum class KeyCode : u16 {  // 改为u16以容纳更大的值
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
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

    // 控制键
    Escape = 27,
    Space = 32,
    Enter = 13,
    Tab = 9,
    Backspace = 8,
    Delete = 127,
    Insert = 150,

    // 方向键
    Left = 151,
    Right = 152,
    Up = 153,
    Down = 154,

    // 导航键
    Home = 155,
    End = 156,
    PageUp = 157,
    PageDown = 158,

    // 修饰键
    LeftShift = 160,
    RightShift = 161,
    LeftCtrl = 162,
    RightCtrl = 163,
    LeftAlt = 164,
    RightAlt = 165,
    LeftSuper = 166,   // Windows键/Command键
    RightSuper = 167,

    // 锁定键
    CapsLock = 170,
    NumLock = 171,
    ScrollLock = 172,

    // 符号键
    Minus = '-',
    Equals = '=',
    LeftBracket = '[',
    RightBracket = ']',
    Backslash = '\\',
    Semicolon = ';',
    Quote = '\'',
    Comma = ',',
    Period = '.',
    Slash = '/',
    Grave = '`',

    // 小键盘
    Numpad0 = 180, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadDivide = 190,
    NumpadMultiply = 191,
    NumpadMinus = 192,
    NumpadPlus = 193,
    NumpadEnter = 194,
    NumpadDecimal = 195,

    // 特殊键
    PrintScreen = 200,
    Pause = 201,
    Menu = 202,

    MaxKeys = 256
};

// 鼠标按键代码
enum class MouseButton : u8 {
    Left = 0,
    Right = 1,
    Middle = 2,
    X1 = 3,     // 侧键1
    X2 = 4,     // 侧键2

    MaxButtons = 8
};

// 手柄按键代码（预留）
enum class GamepadButton : u8 {
    A = 0, B, X, Y,
    LeftBumper, RightBumper,
    LeftStick, RightStick,
    Start, Back,
    DPadUp, DPadDown, DPadLeft, DPadRight,

    MaxButtons = 16
};

// ==================== 输入系统主类 ====================

class InputSystem {
public:
    InputSystem();
    ~InputSystem();

    // 禁止拷贝
    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    // ==================== 生命周期 ====================

    // 初始化（Application调用）
    bool initialize();
    void shutdown();

    // 每帧更新（主循环调用）
    void beginFrame();
    void endFrame();
    void processSDLEvent(void* sdlEvent);  // 处理SDL事件

    // ==================== 键盘输入 ====================

    // 状态查询
    bool isKeyDown(KeyCode key) const;      // 按键当前是否按下
    bool isKeyPressed(KeyCode key) const;   // 按键是否刚按下（这一帧）
    bool isKeyReleased(KeyCode key) const;  // 按键是否刚释放（这一帧）
    bool isAnyKeyDown() const;              // 是否有任何按键按下
    bool isAnyKeyPressed() const;           // 是否有任何按键刚按下

    // 修饰键快捷查询
    bool isShiftDown() const;
    bool isCtrlDown() const;
    bool isAltDown() const;
    bool isSuperDown() const;  // Windows/Command键

    // 组合键检测
    bool isKeyCombo(KeyCode key, bool shift, bool ctrl, bool alt) const;

    // ==================== 鼠标输入 ====================

    // 位置查询
    Math::Vec2 getMousePosition() const;       // 当前位置（窗口坐标）
    Math::Vec2 getMouseDelta() const;          // 位置变化量
    float getMouseX() const { return m_mouseX; }
    float getMouseY() const { return m_mouseY; }

    // 按键查询
    bool isMouseButtonDown(MouseButton button) const;
    bool isMouseButtonPressed(MouseButton button) const;
    bool isMouseButtonReleased(MouseButton button) const;
    bool isAnyMouseButtonDown() const;

    // 滚轮查询
    float getMouseWheel() const { return m_mouseWheel; }
    float getMouseWheelDelta() const { return m_mouseWheelDelta; }

    // 鼠标模式
    void setMouseVisible(bool visible);
    void setMouseLocked(bool locked);  // 锁定到窗口中心（FPS游戏）
    bool isMouseVisible() const { return m_mouseVisible; }
    bool isMouseLocked() const { return m_mouseLocked; }

    // ==================== 文本输入 ====================

    // 用于输入框等需要文本输入的场景
    void startTextInput();
    void stopTextInput();
    bool isTextInputActive() const { return m_textInputActive; }
    const std::string& getTextInput() const { return m_textInput; }
    void clearTextInput() { m_textInput.clear(); }

    // ==================== 手柄输入（预留） ====================

    // bool isGamepadConnected(int index) const;
    // bool isGamepadButtonDown(int index, GamepadButton button) const;
    // float getGamepadAxis(int index, int axis) const;

    // ==================== 工具方法 ====================

    // 获取按键名称（用于显示）
    static const char* getKeyName(KeyCode key);
    static const char* getMouseButtonName(MouseButton button);

    // 调试输出
    void debugPrint() const;

private:
    // ==================== 内部状态 ====================

    // 键盘状态（当前帧和上一帧）
    std::array<bool, static_cast<size_t>(KeyCode::MaxKeys)> m_keys;
    std::array<bool, static_cast<size_t>(KeyCode::MaxKeys)> m_keysLast;

    // 鼠标状态
    float m_mouseX, m_mouseY;
    float m_mouseXLast, m_mouseYLast;
    float m_mouseDeltaX, m_mouseDeltaY;
    float m_mouseWheel, m_mouseWheelDelta;
    std::array<bool, static_cast<size_t>(MouseButton::MaxButtons)> m_mouseButtons;
    std::array<bool, static_cast<size_t>(MouseButton::MaxButtons)> m_mouseButtonsLast;

    // 鼠标设置
    bool m_mouseVisible;
    bool m_mouseLocked;

    // 文本输入
    bool m_textInputActive;
    std::string m_textInput;
    std::string m_textInputBuffer;  // 临时缓冲

    // ==================== 内部方法 ====================

    // SDL按键映射
    KeyCode mapSDLScancode(int scancode) const;
    MouseButton mapSDLMouseButton(int button) const;

    // 状态更新
    void updateKeyboardState();
    void updateMouseState();
};

// ==================== 全局访问（可选） ====================

// 获取全局输入系统实例（由Application管理）
InputSystem* GetInput();

} // namespace Tina::Engine