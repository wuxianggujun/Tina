//
// InputCodes.hpp - 输入设备代码定义
// 职责：定义键盘、鼠标等输入设备的按键/按钮代码
// 说明：独立文件，供 InputSystem 和 Events 共享
//

#pragma once

#include "../core/Core.hpp"

namespace Tina::Engine {

// ==================== 键盘按键代码 ====================

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

// ==================== 鼠标按键代码 ====================

enum class MouseButton : u8 {
    Left = 0,
    Right = 1,
    Middle = 2,
    X1 = 3,     // 侧键1
    X2 = 4,     // 侧键2

    MaxButtons = 8
};

// ==================== 标准手柄输入 ====================

enum class GamepadButton : u8 {
    A = 0, B, X, Y,
    LeftBumper, RightBumper,
    LeftStick, RightStick,
    Start, Back,
    DPadUp, DPadDown, DPadLeft, DPadRight,

    MaxButtons = 16
};

enum class GamepadAxis : u8 {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,

    MaxAxes
};

// ==================== 辅助函数（可选） ====================

// 获取按键名称（用于调试和显示）
inline const char* getKeyName(KeyCode key) {
    switch (key) {
        case KeyCode::A: return "A";
        case KeyCode::B: return "B";
        case KeyCode::C: return "C";
        case KeyCode::D: return "D";
        case KeyCode::E: return "E";
        case KeyCode::F: return "F";
        case KeyCode::G: return "G";
        case KeyCode::H: return "H";
        case KeyCode::I: return "I";
        case KeyCode::J: return "J";
        case KeyCode::K: return "K";
        case KeyCode::L: return "L";
        case KeyCode::M: return "M";
        case KeyCode::N: return "N";
        case KeyCode::O: return "O";
        case KeyCode::P: return "P";
        case KeyCode::Q: return "Q";
        case KeyCode::R: return "R";
        case KeyCode::S: return "S";
        case KeyCode::T: return "T";
        case KeyCode::U: return "U";
        case KeyCode::V: return "V";
        case KeyCode::W: return "W";
        case KeyCode::X: return "X";
        case KeyCode::Y: return "Y";
        case KeyCode::Z: return "Z";

        case KeyCode::Num0: return "0";
        case KeyCode::Num1: return "1";
        case KeyCode::Num2: return "2";
        case KeyCode::Num3: return "3";
        case KeyCode::Num4: return "4";
        case KeyCode::Num5: return "5";
        case KeyCode::Num6: return "6";
        case KeyCode::Num7: return "7";
        case KeyCode::Num8: return "8";
        case KeyCode::Num9: return "9";

        case KeyCode::Escape: return "Escape";
        case KeyCode::Space: return "Space";
        case KeyCode::Enter: return "Enter";
        case KeyCode::Tab: return "Tab";
        case KeyCode::Backspace: return "Backspace";
        case KeyCode::Delete: return "Delete";

        case KeyCode::Left: return "Left";
        case KeyCode::Right: return "Right";
        case KeyCode::Up: return "Up";
        case KeyCode::Down: return "Down";

        case KeyCode::Home: return "Home";
        case KeyCode::End: return "End";
        case KeyCode::PageUp: return "PageUp";
        case KeyCode::PageDown: return "PageDown";

        case KeyCode::LeftShift: return "LeftShift";
        case KeyCode::RightShift: return "RightShift";
        case KeyCode::LeftCtrl: return "LeftCtrl";
        case KeyCode::RightCtrl: return "RightCtrl";
        case KeyCode::LeftAlt: return "LeftAlt";
        case KeyCode::RightAlt: return "RightAlt";

        default: return "Unknown";
    }
}

// 获取鼠标按键名称
inline const char* getMouseButtonName(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return "Left";
        case MouseButton::Right: return "Right";
        case MouseButton::Middle: return "Middle";
        case MouseButton::X1: return "X1";
        case MouseButton::X2: return "X2";
        default: return "Unknown";
    }
}

} // namespace Tina::Engine
