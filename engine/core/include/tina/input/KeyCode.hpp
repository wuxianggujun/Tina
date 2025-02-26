#pragma once

namespace Tina {

enum class KeyCode {
    // 字母键
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,

    // 数字键
    Key0 = 48,
    Key1 = 49,
    Key2 = 50,
    Key3 = 51,
    Key4 = 52,
    Key5 = 53,
    Key6 = 54,
    Key7 = 55,
    Key8 = 56,
    Key9 = 57,

    // 功能键
    Space = 32,
    Enter = 13,
    Tab = 9,
    Escape = 27,
    Backspace = 8,

    // 方向键
    Left = 37,
    Up = 38,
    Right = 39,
    Down = 40,

    // 修饰键
    Shift = 16,
    Control = 17,
    Alt = 18,

    Unknown = -1
};

} // namespace Tina