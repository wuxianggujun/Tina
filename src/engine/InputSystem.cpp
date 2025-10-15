//
// InputSystem.cpp - 输入系统实现
//

#include "InputSystem.hpp"
#include "Window.hpp"
#include "EventSystem.hpp"
#include "EngineEvents.hpp"
#include "../core/Log.hpp"
#include <SDL3/SDL.h>
#include <cstring>

namespace Tina::Engine {

// 全局输入系统实例（由Application管理）
static InputSystem* g_inputSystem = nullptr;

InputSystem* GetInput() {
    return g_inputSystem;
}

InputSystem::InputSystem()
    : m_mouseX(0), m_mouseY(0)
    , m_mouseXLast(0), m_mouseYLast(0)
    , m_mouseDeltaX(0), m_mouseDeltaY(0)
    , m_mouseWheel(0), m_mouseWheelDelta(0)
    , m_mouseVisible(true)
    , m_mouseLocked(false)
    , m_textInputActive(false)
{
    // 清零所有数组
    m_keys.fill(false);
    m_keysLast.fill(false);
    m_mouseButtons.fill(false);
    m_mouseButtonsLast.fill(false);
}

InputSystem::~InputSystem() {
    shutdown();
}

bool InputSystem::initialize() {
    // 设置为全局实例
    g_inputSystem = this;

    TINA_INFO("InputSystem initialized");
    return true;
}

void InputSystem::shutdown() {
    if (g_inputSystem == this) {
        g_inputSystem = nullptr;
    }

    // 停止文本输入
    if (m_textInputActive) {
        stopTextInput();
    }

    TINA_INFO("InputSystem shutdown");
}

// ==================== 生命周期 ====================

void InputSystem::beginFrame() {
    // 保存上一帧状态
    m_keysLast = m_keys;
    m_mouseButtonsLast = m_mouseButtons;

    // 保存上一帧鼠标位置
    m_mouseXLast = m_mouseX;
    m_mouseYLast = m_mouseY;

    // 重置增量值
    m_mouseWheelDelta = 0;
    m_textInputBuffer.clear();

    // 更新输入状态（从SDL获取）
    updateKeyboardState();
    updateMouseState();

    // 计算鼠标增量
    m_mouseDeltaX = m_mouseX - m_mouseXLast;
    m_mouseDeltaY = m_mouseY - m_mouseYLast;
}

void InputSystem::endFrame() {
    // 处理文本输入缓冲
    if (m_textInputActive && !m_textInputBuffer.empty()) {
        m_textInput += m_textInputBuffer;
    }
}

void InputSystem::processSDLEvent(void* sdlEvent) {
    SDL_Event* event = static_cast<SDL_Event*>(sdlEvent);

    switch (event->type) {
        case SDL_EVENT_MOUSE_WHEEL:
            m_mouseWheelDelta = event->wheel.y;
            m_mouseWheel += m_mouseWheelDelta;
            break;

        case SDL_EVENT_TEXT_INPUT:
            if (m_textInputActive) {
                m_textInputBuffer += event->text.text;
                
                // 发布文本输入事件（立即发布，不等待endFrame）
                if (m_eventSystem && event->text.text[0] != '\0') {
                    Events::TextInputEvent textEvent(0, event->text.text);
                    m_eventSystem->trigger(textEvent);
                }
            }
            break;

        // 注意：键盘和鼠标按键事件在updateKeyboardState/updateMouseState中处理
    }
}

// ==================== 键盘输入 ====================

bool InputSystem::isKeyDown(KeyCode key) const {
    size_t index = static_cast<size_t>(key);
    return index < m_keys.size() ? m_keys[index] : false;
}

bool InputSystem::isKeyPressed(KeyCode key) const {
    size_t index = static_cast<size_t>(key);
    return index < m_keys.size() ? (m_keys[index] && !m_keysLast[index]) : false;
}

bool InputSystem::isKeyReleased(KeyCode key) const {
    size_t index = static_cast<size_t>(key);
    return index < m_keys.size() ? (!m_keys[index] && m_keysLast[index]) : false;
}

bool InputSystem::isAnyKeyDown() const {
    for (bool key : m_keys) {
        if (key) return true;
    }
    return false;
}

bool InputSystem::isAnyKeyPressed() const {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        if (m_keys[i] && !m_keysLast[i]) return true;
    }
    return false;
}

bool InputSystem::isShiftDown() const {
    return isKeyDown(KeyCode::LeftShift) || isKeyDown(KeyCode::RightShift);
}

bool InputSystem::isCtrlDown() const {
    return isKeyDown(KeyCode::LeftCtrl) || isKeyDown(KeyCode::RightCtrl);
}

bool InputSystem::isAltDown() const {
    return isKeyDown(KeyCode::LeftAlt) || isKeyDown(KeyCode::RightAlt);
}

bool InputSystem::isSuperDown() const {
    return isKeyDown(KeyCode::LeftSuper) || isKeyDown(KeyCode::RightSuper);
}

bool InputSystem::isKeyCombo(KeyCode key, bool shift, bool ctrl, bool alt) const {
    return isKeyDown(key) &&
           (shift == isShiftDown()) &&
           (ctrl == isCtrlDown()) &&
           (alt == isAltDown());
}

// ==================== 鼠标输入 ====================

Math::Vec2 InputSystem::getMousePosition() const {
    return Math::Vec2(m_mouseX, m_mouseY);
}

Math::Vec2 InputSystem::getMouseDelta() const {
    return Math::Vec2(m_mouseDeltaX, m_mouseDeltaY);
}

bool InputSystem::isMouseButtonDown(MouseButton button) const {
    size_t index = static_cast<size_t>(button);
    return index < m_mouseButtons.size() ? m_mouseButtons[index] : false;
}

bool InputSystem::isMouseButtonPressed(MouseButton button) const {
    size_t index = static_cast<size_t>(button);
    return index < m_mouseButtons.size() ?
           (m_mouseButtons[index] && !m_mouseButtonsLast[index]) : false;
}

bool InputSystem::isMouseButtonReleased(MouseButton button) const {
    size_t index = static_cast<size_t>(button);
    return index < m_mouseButtons.size() ?
           (!m_mouseButtons[index] && m_mouseButtonsLast[index]) : false;
}

bool InputSystem::isAnyMouseButtonDown() const {
    for (bool button : m_mouseButtons) {
        if (button) return true;
    }
    return false;
}

void InputSystem::setMouseVisible(bool visible) {
    if (m_mouseVisible != visible) {
        m_mouseVisible = visible;
        // SDL3 中 SDL_ShowCursor 不再接受参数，使用 SDL_SetCursorVisible
        if (visible) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
}

void InputSystem::setMouseLocked(bool locked) {
    if (m_mouseLocked != locked) {
        m_mouseLocked = locked;
        // SDL3 中 SDL_SetRelativeMouseMode 改名为 SDL_SetWindowRelativeMouseMode
        // 但这需要窗口句柄，暂时注释掉
        // TODO: 需要传入窗口句柄来设置相对鼠标模式
        // SDL_SetWindowRelativeMouseMode(window, locked);
    }
}

// ==================== 文本输入 ====================

void InputSystem::startTextInput() {
    if (!m_textInputActive) {
        m_textInputActive = true;
        m_textInput.clear();
        
        // 获取SDL窗口句柄
        SDL_Window* sdlWindow = m_window ? static_cast<SDL_Window*>(m_window->getSDLWindow()) : nullptr;
        SDL_StartTextInput(sdlWindow);
        
        // 设置输入法候选框位置（可选）
        if (sdlWindow) {
            SDL_Rect rect = { 0, 0, 400, 30 };  // 默认位置，后续可以根据输入框位置动态设置
            SDL_SetTextInputArea(sdlWindow, &rect, 0);
        }
    }
}

void InputSystem::stopTextInput() {
    if (m_textInputActive) {
        m_textInputActive = false;
        
        // 获取SDL窗口句柄
        SDL_Window* sdlWindow = m_window ? static_cast<SDL_Window*>(m_window->getSDLWindow()) : nullptr;
        SDL_StopTextInput(sdlWindow);
    }
}

// ==================== 工具方法 ====================

const char* InputSystem::getKeyName(KeyCode key) {
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
        case KeyCode::Space: return "Space";
        case KeyCode::Enter: return "Enter";
        case KeyCode::Escape: return "Escape";
        case KeyCode::Left: return "Left";
        case KeyCode::Right: return "Right";
        case KeyCode::Up: return "Up";
        case KeyCode::Down: return "Down";
        case KeyCode::LeftShift: return "LShift";
        case KeyCode::RightShift: return "RShift";
        case KeyCode::LeftCtrl: return "LCtrl";
        case KeyCode::RightCtrl: return "RCtrl";
        case KeyCode::LeftAlt: return "LAlt";
        case KeyCode::RightAlt: return "RAlt";
        default: return "Unknown";
    }
}

const char* InputSystem::getMouseButtonName(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return "Left";
        case MouseButton::Right: return "Right";
        case MouseButton::Middle: return "Middle";
        case MouseButton::X1: return "X1";
        case MouseButton::X2: return "X2";
        default: return "Unknown";
    }
}

void InputSystem::debugPrint() const {
    TINA_DEBUG("=== InputSystem Debug ===");
    TINA_DEBUG("Mouse: ({:.0f}, {:.0f}) Delta: ({:.0f}, {:.0f})",
               m_mouseX, m_mouseY, m_mouseDeltaX, m_mouseDeltaY);

    // 打印按下的键
    std::string keys;
    for (size_t i = 0; i < m_keys.size(); ++i) {
        if (m_keys[i]) {
            if (!keys.empty()) keys += ", ";
            keys += getKeyName(static_cast<KeyCode>(i));
        }
    }
    if (!keys.empty()) {
        TINA_DEBUG("Keys down: {}", keys);
    }

    // 打印按下的鼠标按键
    std::string buttons;
    for (size_t i = 0; i < m_mouseButtons.size(); ++i) {
        if (m_mouseButtons[i]) {
            if (!buttons.empty()) buttons += ", ";
            buttons += getMouseButtonName(static_cast<MouseButton>(i));
        }
    }
    if (!buttons.empty()) {
        TINA_DEBUG("Mouse buttons: {}", buttons);
    }
}

// ==================== 内部方法 ====================

void InputSystem::updateKeyboardState() {
    const bool* sdlKeys = SDL_GetKeyboardState(nullptr);

    // 映射SDL扫描码到我们的KeyCode
    // 注意：这里只映射常用按键，可以根据需要扩展

    // ✅ 发送按键事件（检测新按下的按键）
    auto sendKeyEvent = [this](KeyCode key, const bool* sdlKeys, SDL_Scancode scancode) {
        size_t idx = static_cast<size_t>(key);
        bool isDown = sdlKeys[scancode];
        bool wasDown = m_keysLast[idx];
        m_keys[idx] = isDown;
        
        // 如果按键刚按下，发送KeyPressedEvent
        if (isDown && !wasDown && m_eventSystem) {
            Events::KeyPressedEvent event;
            event.key = key;
            event.shift = isShiftDown();
            event.ctrl = isCtrlDown();
            event.alt = isAltDown();
            m_eventSystem->trigger(event);
        }
    };

    // 字母键 A-Z
    sendKeyEvent(KeyCode::A, sdlKeys, SDL_SCANCODE_A);
    sendKeyEvent(KeyCode::B, sdlKeys, SDL_SCANCODE_B);
    sendKeyEvent(KeyCode::C, sdlKeys, SDL_SCANCODE_C);
    sendKeyEvent(KeyCode::D, sdlKeys, SDL_SCANCODE_D);
    sendKeyEvent(KeyCode::E, sdlKeys, SDL_SCANCODE_E);
    sendKeyEvent(KeyCode::F, sdlKeys, SDL_SCANCODE_F);
    sendKeyEvent(KeyCode::G, sdlKeys, SDL_SCANCODE_G);
    sendKeyEvent(KeyCode::H, sdlKeys, SDL_SCANCODE_H);
    sendKeyEvent(KeyCode::I, sdlKeys, SDL_SCANCODE_I);
    sendKeyEvent(KeyCode::J, sdlKeys, SDL_SCANCODE_J);
    sendKeyEvent(KeyCode::K, sdlKeys, SDL_SCANCODE_K);
    sendKeyEvent(KeyCode::L, sdlKeys, SDL_SCANCODE_L);
    sendKeyEvent(KeyCode::M, sdlKeys, SDL_SCANCODE_M);
    sendKeyEvent(KeyCode::N, sdlKeys, SDL_SCANCODE_N);
    sendKeyEvent(KeyCode::O, sdlKeys, SDL_SCANCODE_O);
    sendKeyEvent(KeyCode::P, sdlKeys, SDL_SCANCODE_P);
    sendKeyEvent(KeyCode::Q, sdlKeys, SDL_SCANCODE_Q);
    sendKeyEvent(KeyCode::R, sdlKeys, SDL_SCANCODE_R);
    sendKeyEvent(KeyCode::S, sdlKeys, SDL_SCANCODE_S);
    sendKeyEvent(KeyCode::T, sdlKeys, SDL_SCANCODE_T);
    sendKeyEvent(KeyCode::U, sdlKeys, SDL_SCANCODE_U);
    sendKeyEvent(KeyCode::V, sdlKeys, SDL_SCANCODE_V);
    sendKeyEvent(KeyCode::W, sdlKeys, SDL_SCANCODE_W);
    sendKeyEvent(KeyCode::X, sdlKeys, SDL_SCANCODE_X);
    sendKeyEvent(KeyCode::Y, sdlKeys, SDL_SCANCODE_Y);
    sendKeyEvent(KeyCode::Z, sdlKeys, SDL_SCANCODE_Z);

    // 数字键 0-9  
    sendKeyEvent(KeyCode::Num0, sdlKeys, SDL_SCANCODE_0);
    sendKeyEvent(KeyCode::Num1, sdlKeys, SDL_SCANCODE_1);
    sendKeyEvent(KeyCode::Num2, sdlKeys, SDL_SCANCODE_2);
    sendKeyEvent(KeyCode::Num3, sdlKeys, SDL_SCANCODE_3);
    sendKeyEvent(KeyCode::Num4, sdlKeys, SDL_SCANCODE_4);
    sendKeyEvent(KeyCode::Num5, sdlKeys, SDL_SCANCODE_5);
    sendKeyEvent(KeyCode::Num6, sdlKeys, SDL_SCANCODE_6);
    sendKeyEvent(KeyCode::Num7, sdlKeys, SDL_SCANCODE_7);
    sendKeyEvent(KeyCode::Num8, sdlKeys, SDL_SCANCODE_8);
    sendKeyEvent(KeyCode::Num9, sdlKeys, SDL_SCANCODE_9);

    // 控制键（✅ 关键：Backspace和Delete）
    sendKeyEvent(KeyCode::Space, sdlKeys, SDL_SCANCODE_SPACE);
    sendKeyEvent(KeyCode::Enter, sdlKeys, SDL_SCANCODE_RETURN);
    sendKeyEvent(KeyCode::Escape, sdlKeys, SDL_SCANCODE_ESCAPE);
    sendKeyEvent(KeyCode::Tab, sdlKeys, SDL_SCANCODE_TAB);
    sendKeyEvent(KeyCode::Backspace, sdlKeys, SDL_SCANCODE_BACKSPACE);
    sendKeyEvent(KeyCode::Delete, sdlKeys, SDL_SCANCODE_DELETE);
    sendKeyEvent(KeyCode::Insert, sdlKeys, SDL_SCANCODE_INSERT);

    // 方向键
    sendKeyEvent(KeyCode::Left, sdlKeys, SDL_SCANCODE_LEFT);
    sendKeyEvent(KeyCode::Right, sdlKeys, SDL_SCANCODE_RIGHT);
    sendKeyEvent(KeyCode::Up, sdlKeys, SDL_SCANCODE_UP);
    sendKeyEvent(KeyCode::Down, sdlKeys, SDL_SCANCODE_DOWN);

    // 导航键
    sendKeyEvent(KeyCode::Home, sdlKeys, SDL_SCANCODE_HOME);
    sendKeyEvent(KeyCode::End, sdlKeys, SDL_SCANCODE_END);
    sendKeyEvent(KeyCode::PageUp, sdlKeys, SDL_SCANCODE_PAGEUP);
    sendKeyEvent(KeyCode::PageDown, sdlKeys, SDL_SCANCODE_PAGEDOWN);

    // 修饰键（不发送事件，只更新状态）
    m_keys[static_cast<size_t>(KeyCode::LeftShift)] = sdlKeys[SDL_SCANCODE_LSHIFT];
    m_keys[static_cast<size_t>(KeyCode::RightShift)] = sdlKeys[SDL_SCANCODE_RSHIFT];
    m_keys[static_cast<size_t>(KeyCode::LeftCtrl)] = sdlKeys[SDL_SCANCODE_LCTRL];
    m_keys[static_cast<size_t>(KeyCode::RightCtrl)] = sdlKeys[SDL_SCANCODE_RCTRL];
    m_keys[static_cast<size_t>(KeyCode::LeftAlt)] = sdlKeys[SDL_SCANCODE_LALT];
    m_keys[static_cast<size_t>(KeyCode::RightAlt)] = sdlKeys[SDL_SCANCODE_RALT];
    m_keys[static_cast<size_t>(KeyCode::LeftSuper)] = sdlKeys[SDL_SCANCODE_LGUI];
    m_keys[static_cast<size_t>(KeyCode::RightSuper)] = sdlKeys[SDL_SCANCODE_RGUI];

    // 功能键 F1-F12
    sendKeyEvent(KeyCode::F1, sdlKeys, SDL_SCANCODE_F1);
    sendKeyEvent(KeyCode::F2, sdlKeys, SDL_SCANCODE_F2);
    sendKeyEvent(KeyCode::F3, sdlKeys, SDL_SCANCODE_F3);
    sendKeyEvent(KeyCode::F4, sdlKeys, SDL_SCANCODE_F4);
    sendKeyEvent(KeyCode::F5, sdlKeys, SDL_SCANCODE_F5);
    sendKeyEvent(KeyCode::F6, sdlKeys, SDL_SCANCODE_F6);
    sendKeyEvent(KeyCode::F7, sdlKeys, SDL_SCANCODE_F7);
    sendKeyEvent(KeyCode::F8, sdlKeys, SDL_SCANCODE_F8);
    sendKeyEvent(KeyCode::F9, sdlKeys, SDL_SCANCODE_F9);
    sendKeyEvent(KeyCode::F10, sdlKeys, SDL_SCANCODE_F10);
    sendKeyEvent(KeyCode::F11, sdlKeys, SDL_SCANCODE_F11);
    sendKeyEvent(KeyCode::F12, sdlKeys, SDL_SCANCODE_F12);
}

void InputSystem::updateMouseState() {
    // 获取鼠标位置
    float mx = 0.0f, my = 0.0f;
    uint32_t btnMask = SDL_GetMouseState(&mx, &my);

    // 调试：检查鼠标坐标系统
    static int debugCounter = 0;
    if (++debugCounter % 30 == 0 && (btnMask & SDL_BUTTON_MASK(1))) {
        SDL_Window* window = SDL_GetMouseFocus();
        if (window) {
            int logicalW = 0, logicalH = 0;
            int pixelW = 0, pixelH = 0;
            SDL_GetWindowSize(window, &logicalW, &logicalH);
            SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);

            TINA_DEBUG("鼠标坐标调试:");
            TINA_DEBUG("  SDL返回鼠标: ({}, {})", mx, my);
            TINA_DEBUG("  逻辑窗口: {}x{}", logicalW, logicalH);
            TINA_DEBUG("  像素窗口: {}x{}", pixelW, pixelH);
            TINA_DEBUG("  鼠标/逻辑比例: ({:.4f}, {:.4f})", mx/logicalW, my/logicalH);
            TINA_DEBUG("  鼠标/像素比例: ({:.4f}, {:.4f})", mx/pixelW, my/pixelH);
        }
    }

    // 暂时不做任何转换，直接使用SDL返回的坐标

    m_mouseX = mx;
    m_mouseY = my;

    // 更新鼠标按键状态
    m_mouseButtons[static_cast<size_t>(MouseButton::Left)] = (btnMask & SDL_BUTTON_MASK(1)) != 0;
    m_mouseButtons[static_cast<size_t>(MouseButton::Right)] = (btnMask & SDL_BUTTON_MASK(3)) != 0;
    m_mouseButtons[static_cast<size_t>(MouseButton::Middle)] = (btnMask & SDL_BUTTON_MASK(2)) != 0;
    m_mouseButtons[static_cast<size_t>(MouseButton::X1)] = (btnMask & SDL_BUTTON_MASK(4)) != 0;
    m_mouseButtons[static_cast<size_t>(MouseButton::X2)] = (btnMask & SDL_BUTTON_MASK(5)) != 0;
}

KeyCode InputSystem::mapSDLScancode(int scancode) const {
    // SDL扫描码到KeyCode的映射
    // 这是一个简化版本，可以根据需要扩展
    switch (scancode) {
        case SDL_SCANCODE_A: return KeyCode::A;
        case SDL_SCANCODE_B: return KeyCode::B;
        case SDL_SCANCODE_C: return KeyCode::C;
        case SDL_SCANCODE_D: return KeyCode::D;
        case SDL_SCANCODE_W: return KeyCode::W;
        case SDL_SCANCODE_S: return KeyCode::S;
        case SDL_SCANCODE_SPACE: return KeyCode::Space;
        case SDL_SCANCODE_ESCAPE: return KeyCode::Escape;
        case SDL_SCANCODE_RETURN: return KeyCode::Enter;
        case SDL_SCANCODE_LEFT: return KeyCode::Left;
        case SDL_SCANCODE_RIGHT: return KeyCode::Right;
        case SDL_SCANCODE_UP: return KeyCode::Up;
        case SDL_SCANCODE_DOWN: return KeyCode::Down;
        default: return KeyCode::Unknown;
    }
}

MouseButton InputSystem::mapSDLMouseButton(int button) const {
    switch (button) {
        case SDL_BUTTON_LEFT: return MouseButton::Left;
        case SDL_BUTTON_RIGHT: return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_X1: return MouseButton::X1;
        case SDL_BUTTON_X2: return MouseButton::X2;
        default: return MouseButton::Left;
    }
}

} // namespace Tina::Engine