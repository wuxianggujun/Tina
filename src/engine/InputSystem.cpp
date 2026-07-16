//
// InputSystem.cpp - GLFW-backed keyboard, mouse, and committed text input.
//

#include "InputSystem.hpp"
#include "Window.hpp"
#include "EventSystem.hpp"
#include "EngineEvents.hpp"
#include "../core/Log.hpp"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>

namespace Tina::Engine {
namespace {

InputSystem* g_inputSystem = nullptr;

InputSystem* inputFromWindow(GLFWwindow* window)
{
    return window ? static_cast<InputSystem*>(glfwGetWindowUserPointer(window)) : nullptr;
}

bool isValidIndex(KeyCode key)
{
    return static_cast<size_t>(key) < static_cast<size_t>(KeyCode::MaxKeys);
}

bool isValidIndex(MouseButton button)
{
    return static_cast<size_t>(button) < static_cast<size_t>(MouseButton::MaxButtons);
}

std::string encodeUtf8(unsigned int codepoint)
{
    std::string out;

    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            return {};
        }
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    return out;
}

} // namespace

InputSystem* GetInput()
{
    return g_inputSystem;
}

InputSystem::InputSystem()
    : m_mouseX(0.0f)
    , m_mouseY(0.0f)
    , m_mouseXLast(0.0f)
    , m_mouseYLast(0.0f)
    , m_mouseDeltaX(0.0f)
    , m_mouseDeltaY(0.0f)
    , m_mouseWheel(0.0f)
    , m_mouseWheelDelta(0.0f)
    , m_mouseWheelDeltaX(0.0f)
    , m_mouseVisible(true)
    , m_mouseLocked(false)
    , m_textInputActive(false)
{
    m_keys.fill(false);
    m_keysLast.fill(false);
    m_mouseButtons.fill(false);
    m_mouseButtonsLast.fill(false);
}

InputSystem::~InputSystem()
{
    shutdown();
}

bool InputSystem::initialize()
{
    g_inputSystem = this;
    installCallbacks();
    updateKeyboardState();
    updateMouseState();
    TINA_INFO("InputSystem initialized with GLFW");
    return true;
}

void InputSystem::shutdown()
{
    if (m_textInputActive) {
        stopTextInput();
    }

    uninstallCallbacks();

    if (g_inputSystem == this) {
        g_inputSystem = nullptr;
    }

    TINA_INFO("InputSystem shutdown");
}

void InputSystem::setWindow(Window* window)
{
    if (m_window == window) {
        return;
    }

    uninstallCallbacks();
    m_window = window;
    m_glfwWindow = m_window ? m_window->getGLFWWindow() : nullptr;
    installCallbacks();
}

void InputSystem::beginFrame()
{
    m_keysLast = m_keys;
    m_mouseButtonsLast = m_mouseButtons;

    m_mouseXLast = m_mouseX;
    m_mouseYLast = m_mouseY;
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;

    m_mouseWheelDelta = 0.0f;
    m_mouseWheelDeltaX = 0.0f;
    m_textInputBuffer.clear();

    updateKeyboardState();
    updateMouseState();
}

void InputSystem::endFrame()
{
    if (m_textInputActive && !m_textInputBuffer.empty()) {
        m_textInput += m_textInputBuffer;
    }
}

bool InputSystem::isKeyDown(KeyCode key) const
{
    return isValidIndex(key) ? m_keys[static_cast<size_t>(key)] : false;
}

bool InputSystem::isKeyPressed(KeyCode key) const
{
    const size_t index = static_cast<size_t>(key);
    return index < m_keys.size() ? (m_keys[index] && !m_keysLast[index]) : false;
}

bool InputSystem::isKeyReleased(KeyCode key) const
{
    const size_t index = static_cast<size_t>(key);
    return index < m_keys.size() ? (!m_keys[index] && m_keysLast[index]) : false;
}

bool InputSystem::isAnyKeyDown() const
{
    for (bool key : m_keys) {
        if (key) {
            return true;
        }
    }
    return false;
}

bool InputSystem::isAnyKeyPressed() const
{
    for (size_t i = 0; i < m_keys.size(); ++i) {
        if (m_keys[i] && !m_keysLast[i]) {
            return true;
        }
    }
    return false;
}

bool InputSystem::isShiftDown() const
{
    return isKeyDown(KeyCode::LeftShift) || isKeyDown(KeyCode::RightShift);
}

bool InputSystem::isCtrlDown() const
{
    return isKeyDown(KeyCode::LeftCtrl) || isKeyDown(KeyCode::RightCtrl);
}

bool InputSystem::isAltDown() const
{
    return isKeyDown(KeyCode::LeftAlt) || isKeyDown(KeyCode::RightAlt);
}

bool InputSystem::isSuperDown() const
{
    return isKeyDown(KeyCode::LeftSuper) || isKeyDown(KeyCode::RightSuper);
}

bool InputSystem::isKeyCombo(KeyCode key, bool shift, bool ctrl, bool alt) const
{
    return isKeyDown(key)
        && shift == isShiftDown()
        && ctrl == isCtrlDown()
        && alt == isAltDown();
}

Math::Vec2 InputSystem::getMousePosition() const
{
    return Math::Vec2(m_mouseX, m_mouseY);
}

Math::Vec2 InputSystem::getMouseDelta() const
{
    return Math::Vec2(m_mouseDeltaX, m_mouseDeltaY);
}

bool InputSystem::isMouseButtonDown(MouseButton button) const
{
    return isValidIndex(button) ? m_mouseButtons[static_cast<size_t>(button)] : false;
}

bool InputSystem::isMouseButtonPressed(MouseButton button) const
{
    const size_t index = static_cast<size_t>(button);
    return index < m_mouseButtons.size()
        ? (m_mouseButtons[index] && !m_mouseButtonsLast[index])
        : false;
}

bool InputSystem::isMouseButtonReleased(MouseButton button) const
{
    const size_t index = static_cast<size_t>(button);
    return index < m_mouseButtons.size()
        ? (!m_mouseButtons[index] && m_mouseButtonsLast[index])
        : false;
}

bool InputSystem::isAnyMouseButtonDown() const
{
    for (bool button : m_mouseButtons) {
        if (button) {
            return true;
        }
    }
    return false;
}

void InputSystem::setMouseVisible(bool visible)
{
    if (m_mouseVisible == visible) {
        return;
    }

    m_mouseVisible = visible;
    updateCursorMode();
}

void InputSystem::setMouseLocked(bool locked)
{
    if (m_mouseLocked == locked) {
        return;
    }

    m_mouseLocked = locked;
    updateCursorMode();
}

void InputSystem::startTextInput()
{
    if (m_textInputActive) {
        return;
    }

    m_textInputActive = true;
    m_textInput.clear();
    m_textInputBuffer.clear();
}

void InputSystem::stopTextInput()
{
    if (!m_textInputActive) {
        return;
    }

    m_textInputActive = false;
}

const char* InputSystem::getKeyName(KeyCode key)
{
    return Tina::Engine::getKeyName(key);
}

const char* InputSystem::getMouseButtonName(MouseButton button)
{
    return Tina::Engine::getMouseButtonName(button);
}

void InputSystem::debugPrint() const
{
    TINA_DEBUG("=== InputSystem Debug ===");
    TINA_DEBUG("Mouse: ({:.0f}, {:.0f}) Delta: ({:.0f}, {:.0f})",
               m_mouseX, m_mouseY, m_mouseDeltaX, m_mouseDeltaY);

    std::string keys;
    for (size_t i = 0; i < m_keys.size(); ++i) {
        if (m_keys[i]) {
            if (!keys.empty()) {
                keys += ", ";
            }
            keys += getKeyName(static_cast<KeyCode>(i));
        }
    }
    if (!keys.empty()) {
        TINA_DEBUG("Keys down: {}", keys);
    }

    std::string buttons;
    for (size_t i = 0; i < m_mouseButtons.size(); ++i) {
        if (m_mouseButtons[i]) {
            if (!buttons.empty()) {
                buttons += ", ";
            }
            buttons += getMouseButtonName(static_cast<MouseButton>(i));
        }
    }
    if (!buttons.empty()) {
        TINA_DEBUG("Mouse buttons: {}", buttons);
    }
}

void InputSystem::installCallbacks()
{
    if (!m_glfwWindow || m_callbacksInstalled) {
        return;
    }

    glfwSetWindowUserPointer(m_glfwWindow, this);
    glfwSetKeyCallback(m_glfwWindow, &InputSystem::keyCallback);
    glfwSetCharCallback(m_glfwWindow, &InputSystem::charCallback);
    glfwSetScrollCallback(m_glfwWindow, &InputSystem::scrollCallback);
    glfwSetCursorPosCallback(m_glfwWindow, &InputSystem::cursorPosCallback);
    glfwSetMouseButtonCallback(m_glfwWindow, &InputSystem::mouseButtonCallback);
    updateCursorMode();
    m_callbacksInstalled = true;
}

void InputSystem::uninstallCallbacks()
{
    if (!m_glfwWindow || !m_callbacksInstalled) {
        m_callbacksInstalled = false;
        return;
    }

    if (glfwGetWindowUserPointer(m_glfwWindow) == this) {
        glfwSetKeyCallback(m_glfwWindow, nullptr);
        glfwSetCharCallback(m_glfwWindow, nullptr);
        glfwSetScrollCallback(m_glfwWindow, nullptr);
        glfwSetCursorPosCallback(m_glfwWindow, nullptr);
        glfwSetMouseButtonCallback(m_glfwWindow, nullptr);
        glfwSetWindowUserPointer(m_glfwWindow, nullptr);
    }

    m_callbacksInstalled = false;
}

void InputSystem::updateKeyboardState()
{
    if (!m_glfwWindow) {
        m_keys.fill(false);
        return;
    }

    const auto update = [this](KeyCode key, int glfwKey) {
        const size_t index = static_cast<size_t>(key);
        if (index >= m_keys.size()) {
            return;
        }
        const int state = glfwGetKey(m_glfwWindow, glfwKey);
        m_keys[index] = state == GLFW_PRESS || state == GLFW_REPEAT;
    };

    update(KeyCode::A, GLFW_KEY_A);
    update(KeyCode::B, GLFW_KEY_B);
    update(KeyCode::C, GLFW_KEY_C);
    update(KeyCode::D, GLFW_KEY_D);
    update(KeyCode::E, GLFW_KEY_E);
    update(KeyCode::F, GLFW_KEY_F);
    update(KeyCode::G, GLFW_KEY_G);
    update(KeyCode::H, GLFW_KEY_H);
    update(KeyCode::I, GLFW_KEY_I);
    update(KeyCode::J, GLFW_KEY_J);
    update(KeyCode::K, GLFW_KEY_K);
    update(KeyCode::L, GLFW_KEY_L);
    update(KeyCode::M, GLFW_KEY_M);
    update(KeyCode::N, GLFW_KEY_N);
    update(KeyCode::O, GLFW_KEY_O);
    update(KeyCode::P, GLFW_KEY_P);
    update(KeyCode::Q, GLFW_KEY_Q);
    update(KeyCode::R, GLFW_KEY_R);
    update(KeyCode::S, GLFW_KEY_S);
    update(KeyCode::T, GLFW_KEY_T);
    update(KeyCode::U, GLFW_KEY_U);
    update(KeyCode::V, GLFW_KEY_V);
    update(KeyCode::W, GLFW_KEY_W);
    update(KeyCode::X, GLFW_KEY_X);
    update(KeyCode::Y, GLFW_KEY_Y);
    update(KeyCode::Z, GLFW_KEY_Z);

    update(KeyCode::Num0, GLFW_KEY_0);
    update(KeyCode::Num1, GLFW_KEY_1);
    update(KeyCode::Num2, GLFW_KEY_2);
    update(KeyCode::Num3, GLFW_KEY_3);
    update(KeyCode::Num4, GLFW_KEY_4);
    update(KeyCode::Num5, GLFW_KEY_5);
    update(KeyCode::Num6, GLFW_KEY_6);
    update(KeyCode::Num7, GLFW_KEY_7);
    update(KeyCode::Num8, GLFW_KEY_8);
    update(KeyCode::Num9, GLFW_KEY_9);

    update(KeyCode::Space, GLFW_KEY_SPACE);
    update(KeyCode::Enter, GLFW_KEY_ENTER);
    update(KeyCode::Escape, GLFW_KEY_ESCAPE);
    update(KeyCode::Tab, GLFW_KEY_TAB);
    update(KeyCode::Backspace, GLFW_KEY_BACKSPACE);
    update(KeyCode::Delete, GLFW_KEY_DELETE);
    update(KeyCode::Insert, GLFW_KEY_INSERT);

    update(KeyCode::Left, GLFW_KEY_LEFT);
    update(KeyCode::Right, GLFW_KEY_RIGHT);
    update(KeyCode::Up, GLFW_KEY_UP);
    update(KeyCode::Down, GLFW_KEY_DOWN);

    update(KeyCode::Home, GLFW_KEY_HOME);
    update(KeyCode::End, GLFW_KEY_END);
    update(KeyCode::PageUp, GLFW_KEY_PAGE_UP);
    update(KeyCode::PageDown, GLFW_KEY_PAGE_DOWN);

    update(KeyCode::LeftShift, GLFW_KEY_LEFT_SHIFT);
    update(KeyCode::RightShift, GLFW_KEY_RIGHT_SHIFT);
    update(KeyCode::LeftCtrl, GLFW_KEY_LEFT_CONTROL);
    update(KeyCode::RightCtrl, GLFW_KEY_RIGHT_CONTROL);
    update(KeyCode::LeftAlt, GLFW_KEY_LEFT_ALT);
    update(KeyCode::RightAlt, GLFW_KEY_RIGHT_ALT);
    update(KeyCode::LeftSuper, GLFW_KEY_LEFT_SUPER);
    update(KeyCode::RightSuper, GLFW_KEY_RIGHT_SUPER);

    update(KeyCode::F1, GLFW_KEY_F1);
    update(KeyCode::F2, GLFW_KEY_F2);
    update(KeyCode::F3, GLFW_KEY_F3);
    update(KeyCode::F4, GLFW_KEY_F4);
    update(KeyCode::F5, GLFW_KEY_F5);
    update(KeyCode::F6, GLFW_KEY_F6);
    update(KeyCode::F7, GLFW_KEY_F7);
    update(KeyCode::F8, GLFW_KEY_F8);
    update(KeyCode::F9, GLFW_KEY_F9);
    update(KeyCode::F10, GLFW_KEY_F10);
    update(KeyCode::F11, GLFW_KEY_F11);
    update(KeyCode::F12, GLFW_KEY_F12);
}

void InputSystem::updateMouseState()
{
    if (!m_glfwWindow) {
        m_mouseButtons.fill(false);
        return;
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(m_glfwWindow, &x, &y);
    m_mouseX = static_cast<float>(x);
    m_mouseY = static_cast<float>(y);

    const auto update = [this](MouseButton button, int glfwButton) {
        const size_t index = static_cast<size_t>(button);
        if (index >= m_mouseButtons.size()) {
            return;
        }
        m_mouseButtons[index] = glfwGetMouseButton(m_glfwWindow, glfwButton) == GLFW_PRESS;
    };

    update(MouseButton::Left, GLFW_MOUSE_BUTTON_LEFT);
    update(MouseButton::Right, GLFW_MOUSE_BUTTON_RIGHT);
    update(MouseButton::Middle, GLFW_MOUSE_BUTTON_MIDDLE);
    update(MouseButton::X1, GLFW_MOUSE_BUTTON_4);
    update(MouseButton::X2, GLFW_MOUSE_BUTTON_5);
}

void InputSystem::updateCursorMode()
{
    if (!m_glfwWindow) {
        return;
    }

    if (m_mouseLocked) {
        glfwSetInputMode(m_glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        glfwSetInputMode(m_glfwWindow, GLFW_CURSOR, m_mouseVisible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }
}

KeyCode InputSystem::mapGLFWKey(int key) const
{
    switch (key) {
        case GLFW_KEY_A: return KeyCode::A;
        case GLFW_KEY_B: return KeyCode::B;
        case GLFW_KEY_C: return KeyCode::C;
        case GLFW_KEY_D: return KeyCode::D;
        case GLFW_KEY_E: return KeyCode::E;
        case GLFW_KEY_F: return KeyCode::F;
        case GLFW_KEY_G: return KeyCode::G;
        case GLFW_KEY_H: return KeyCode::H;
        case GLFW_KEY_I: return KeyCode::I;
        case GLFW_KEY_J: return KeyCode::J;
        case GLFW_KEY_K: return KeyCode::K;
        case GLFW_KEY_L: return KeyCode::L;
        case GLFW_KEY_M: return KeyCode::M;
        case GLFW_KEY_N: return KeyCode::N;
        case GLFW_KEY_O: return KeyCode::O;
        case GLFW_KEY_P: return KeyCode::P;
        case GLFW_KEY_Q: return KeyCode::Q;
        case GLFW_KEY_R: return KeyCode::R;
        case GLFW_KEY_S: return KeyCode::S;
        case GLFW_KEY_T: return KeyCode::T;
        case GLFW_KEY_U: return KeyCode::U;
        case GLFW_KEY_V: return KeyCode::V;
        case GLFW_KEY_W: return KeyCode::W;
        case GLFW_KEY_X: return KeyCode::X;
        case GLFW_KEY_Y: return KeyCode::Y;
        case GLFW_KEY_Z: return KeyCode::Z;

        case GLFW_KEY_0: return KeyCode::Num0;
        case GLFW_KEY_1: return KeyCode::Num1;
        case GLFW_KEY_2: return KeyCode::Num2;
        case GLFW_KEY_3: return KeyCode::Num3;
        case GLFW_KEY_4: return KeyCode::Num4;
        case GLFW_KEY_5: return KeyCode::Num5;
        case GLFW_KEY_6: return KeyCode::Num6;
        case GLFW_KEY_7: return KeyCode::Num7;
        case GLFW_KEY_8: return KeyCode::Num8;
        case GLFW_KEY_9: return KeyCode::Num9;

        case GLFW_KEY_F1: return KeyCode::F1;
        case GLFW_KEY_F2: return KeyCode::F2;
        case GLFW_KEY_F3: return KeyCode::F3;
        case GLFW_KEY_F4: return KeyCode::F4;
        case GLFW_KEY_F5: return KeyCode::F5;
        case GLFW_KEY_F6: return KeyCode::F6;
        case GLFW_KEY_F7: return KeyCode::F7;
        case GLFW_KEY_F8: return KeyCode::F8;
        case GLFW_KEY_F9: return KeyCode::F9;
        case GLFW_KEY_F10: return KeyCode::F10;
        case GLFW_KEY_F11: return KeyCode::F11;
        case GLFW_KEY_F12: return KeyCode::F12;
        case GLFW_KEY_F13: return KeyCode::F13;
        case GLFW_KEY_F14: return KeyCode::F14;
        case GLFW_KEY_F15: return KeyCode::F15;
        case GLFW_KEY_F16: return KeyCode::F16;
        case GLFW_KEY_F17: return KeyCode::F17;
        case GLFW_KEY_F18: return KeyCode::F18;
        case GLFW_KEY_F19: return KeyCode::F19;
        case GLFW_KEY_F20: return KeyCode::F20;
        case GLFW_KEY_F21: return KeyCode::F21;
        case GLFW_KEY_F22: return KeyCode::F22;
        case GLFW_KEY_F23: return KeyCode::F23;
        case GLFW_KEY_F24: return KeyCode::F24;

        case GLFW_KEY_ESCAPE: return KeyCode::Escape;
        case GLFW_KEY_SPACE: return KeyCode::Space;
        case GLFW_KEY_ENTER: return KeyCode::Enter;
        case GLFW_KEY_TAB: return KeyCode::Tab;
        case GLFW_KEY_BACKSPACE: return KeyCode::Backspace;
        case GLFW_KEY_DELETE: return KeyCode::Delete;
        case GLFW_KEY_INSERT: return KeyCode::Insert;

        case GLFW_KEY_LEFT: return KeyCode::Left;
        case GLFW_KEY_RIGHT: return KeyCode::Right;
        case GLFW_KEY_UP: return KeyCode::Up;
        case GLFW_KEY_DOWN: return KeyCode::Down;
        case GLFW_KEY_HOME: return KeyCode::Home;
        case GLFW_KEY_END: return KeyCode::End;
        case GLFW_KEY_PAGE_UP: return KeyCode::PageUp;
        case GLFW_KEY_PAGE_DOWN: return KeyCode::PageDown;

        case GLFW_KEY_LEFT_SHIFT: return KeyCode::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return KeyCode::RightShift;
        case GLFW_KEY_LEFT_CONTROL: return KeyCode::LeftCtrl;
        case GLFW_KEY_RIGHT_CONTROL: return KeyCode::RightCtrl;
        case GLFW_KEY_LEFT_ALT: return KeyCode::LeftAlt;
        case GLFW_KEY_RIGHT_ALT: return KeyCode::RightAlt;
        case GLFW_KEY_LEFT_SUPER: return KeyCode::LeftSuper;
        case GLFW_KEY_RIGHT_SUPER: return KeyCode::RightSuper;

        case GLFW_KEY_CAPS_LOCK: return KeyCode::CapsLock;
        case GLFW_KEY_NUM_LOCK: return KeyCode::NumLock;
        case GLFW_KEY_SCROLL_LOCK: return KeyCode::ScrollLock;

        case GLFW_KEY_MINUS: return KeyCode::Minus;
        case GLFW_KEY_EQUAL: return KeyCode::Equals;
        case GLFW_KEY_LEFT_BRACKET: return KeyCode::LeftBracket;
        case GLFW_KEY_RIGHT_BRACKET: return KeyCode::RightBracket;
        case GLFW_KEY_BACKSLASH: return KeyCode::Backslash;
        case GLFW_KEY_SEMICOLON: return KeyCode::Semicolon;
        case GLFW_KEY_APOSTROPHE: return KeyCode::Quote;
        case GLFW_KEY_COMMA: return KeyCode::Comma;
        case GLFW_KEY_PERIOD: return KeyCode::Period;
        case GLFW_KEY_SLASH: return KeyCode::Slash;
        case GLFW_KEY_GRAVE_ACCENT: return KeyCode::Grave;

        case GLFW_KEY_KP_0: return KeyCode::Numpad0;
        case GLFW_KEY_KP_1: return KeyCode::Numpad1;
        case GLFW_KEY_KP_2: return KeyCode::Numpad2;
        case GLFW_KEY_KP_3: return KeyCode::Numpad3;
        case GLFW_KEY_KP_4: return KeyCode::Numpad4;
        case GLFW_KEY_KP_5: return KeyCode::Numpad5;
        case GLFW_KEY_KP_6: return KeyCode::Numpad6;
        case GLFW_KEY_KP_7: return KeyCode::Numpad7;
        case GLFW_KEY_KP_8: return KeyCode::Numpad8;
        case GLFW_KEY_KP_9: return KeyCode::Numpad9;
        case GLFW_KEY_KP_DIVIDE: return KeyCode::NumpadDivide;
        case GLFW_KEY_KP_MULTIPLY: return KeyCode::NumpadMultiply;
        case GLFW_KEY_KP_SUBTRACT: return KeyCode::NumpadMinus;
        case GLFW_KEY_KP_ADD: return KeyCode::NumpadPlus;
        case GLFW_KEY_KP_ENTER: return KeyCode::NumpadEnter;
        case GLFW_KEY_KP_DECIMAL: return KeyCode::NumpadDecimal;

        case GLFW_KEY_PRINT_SCREEN: return KeyCode::PrintScreen;
        case GLFW_KEY_PAUSE: return KeyCode::Pause;
        case GLFW_KEY_MENU: return KeyCode::Menu;

        default: return KeyCode::Unknown;
    }
}

MouseButton InputSystem::mapGLFWMouseButton(int button) const
{
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
        case GLFW_MOUSE_BUTTON_4: return MouseButton::X1;
        case GLFW_MOUSE_BUTTON_5: return MouseButton::X2;
        default: return MouseButton::MaxButtons;
    }
}

void InputSystem::handleKey(int key, int action)
{
    const KeyCode mapped = mapGLFWKey(key);
    if (mapped == KeyCode::Unknown || !isValidIndex(mapped)) {
        return;
    }

    const size_t index = static_cast<size_t>(mapped);
    if (action == GLFW_PRESS) {
        m_keys[index] = true;
        emitKeyPressed(mapped, false);
    } else if (action == GLFW_REPEAT) {
        m_keys[index] = true;
        emitKeyPressed(mapped, true);
    } else if (action == GLFW_RELEASE) {
        m_keys[index] = false;
    }
}

void InputSystem::handleChar(unsigned int codepoint)
{
    if (!m_textInputActive) {
        return;
    }

    const std::string utf8 = encodeUtf8(codepoint);
    if (utf8.empty()) {
        return;
    }

    appendCommittedText(codepoint, utf8);
}

void InputSystem::handleScroll(double offsetX, double offsetY)
{
    m_mouseWheelDeltaX += static_cast<float>(offsetX);
    m_mouseWheelDelta += static_cast<float>(offsetY);
    m_mouseWheel += static_cast<float>(offsetY);
}

void InputSystem::handleCursorPos(double x, double y)
{
    m_mouseX = static_cast<float>(x);
    m_mouseY = static_cast<float>(y);
    m_mouseDeltaX = m_mouseX - m_mouseXLast;
    m_mouseDeltaY = m_mouseY - m_mouseYLast;
}

void InputSystem::handleMouseButton(int button, int action)
{
    const MouseButton mapped = mapGLFWMouseButton(button);
    if (!isValidIndex(mapped)) {
        return;
    }

    const size_t index = static_cast<size_t>(mapped);
    if (action == GLFW_PRESS) {
        m_mouseButtons[index] = true;
    } else if (action == GLFW_RELEASE) {
        m_mouseButtons[index] = false;
    }
}

void InputSystem::emitKeyPressed(KeyCode key, bool repeat)
{
    if (!m_eventSystem) {
        return;
    }

    Events::KeyPressedEvent event;
    event.key = key;
    event.isRepeat = repeat;
    event.shift = isShiftDown();
    event.ctrl = isCtrlDown();
    event.alt = isAltDown();
    if (key == KeyCode::Tab && !repeat) {
        m_eventSystem->focusNext(event.shift);
    }
    m_eventSystem->trigger(event);
}

void InputSystem::appendCommittedText(unsigned int codepoint, const std::string& utf8)
{
    m_textInputBuffer += utf8;

    if (m_eventSystem) {
        Events::TextInputEvent event(codepoint, utf8.c_str());
        m_eventSystem->trigger(event);
    }
}

void InputSystem::keyCallback(GLFWwindow* window, int key, int, int action, int)
{
    if (InputSystem* input = inputFromWindow(window)) {
        input->handleKey(key, action);
    }
}

void InputSystem::charCallback(GLFWwindow* window, unsigned int codepoint)
{
    if (InputSystem* input = inputFromWindow(window)) {
        input->handleChar(codepoint);
    }
}

void InputSystem::scrollCallback(GLFWwindow* window, double offsetX, double offsetY)
{
    if (InputSystem* input = inputFromWindow(window)) {
        input->handleScroll(offsetX, offsetY);
    }
}

void InputSystem::cursorPosCallback(GLFWwindow* window, double x, double y)
{
    if (InputSystem* input = inputFromWindow(window)) {
        input->handleCursorPos(x, y);
    }
}

void InputSystem::mouseButtonCallback(GLFWwindow* window, int button, int action, int)
{
    if (InputSystem* input = inputFromWindow(window)) {
        input->handleMouseButton(button, action);
    }
}

} // namespace Tina::Engine
