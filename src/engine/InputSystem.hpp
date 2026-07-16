//
// InputSystem.hpp - GLFW-backed keyboard, mouse, and committed text input.
//

#pragma once

#include "../core/Core.hpp"
#include "../core/Math.hpp"
#include "InputCodes.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

struct GLFWwindow;

namespace Tina::Engine {

class EventSystem;
class Window;

class InputSystem {
public:
    InputSystem();
    ~InputSystem();

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    bool initialize();
    void shutdown();

    void beginFrame();
    void endFrame();

    bool isKeyDown(KeyCode key) const;
    bool isKeyPressed(KeyCode key) const;
    bool isKeyReleased(KeyCode key) const;
    bool isAnyKeyDown() const;
    bool isAnyKeyPressed() const;

    bool isShiftDown() const;
    bool isCtrlDown() const;
    bool isAltDown() const;
    bool isSuperDown() const;

    bool isKeyCombo(KeyCode key, bool shift, bool ctrl, bool alt) const;

    Math::Vec2 getMousePosition() const;
    Math::Vec2 getMouseDelta() const;
    float getMouseX() const { return m_mouseX; }
    float getMouseY() const { return m_mouseY; }

    bool isMouseButtonDown(MouseButton button) const;
    bool isMouseButtonPressed(MouseButton button) const;
    bool isMouseButtonReleased(MouseButton button) const;
    bool isAnyMouseButtonDown() const;

    float getMouseWheel() const { return m_mouseWheel; }
    float getMouseWheelDelta() const { return m_mouseWheelDelta; }

    void setMouseVisible(bool visible);
    void setMouseLocked(bool locked);
    bool isMouseVisible() const { return m_mouseVisible; }
    bool isMouseLocked() const { return m_mouseLocked; }

    void startTextInput();
    void stopTextInput();
    bool isTextInputActive() const { return m_textInputActive; }
    const std::string& getTextInput() const { return m_textInput; }
    void clearTextInput() { m_textInput.clear(); }

    // UI 传入 framebuffer client coordinates；Windows IMM32 使用同一坐标系
    // 定位 composition/candidate 窗口，其他平台安全退化为仅保存位置。
    void setTextInputRect(float x, float y, float width, float height);

    // 平台桥接与测试入口。composition 永远不会直接写入已提交文本。
    void beginTextComposition();
    void updateTextComposition(std::string_view utf8, uint32_t cursorCodepoint,
                               uint32_t selectionLength = 0);
    void endTextComposition(bool cancelled = false);
    bool isTextCompositionActive() const { return m_textCompositionActive; }
    const std::string& getTextComposition() const { return m_textComposition; }

    static const char* getKeyName(KeyCode key);
    static const char* getMouseButtonName(MouseButton button);

    void debugPrint() const;

    void setEventSystem(EventSystem* eventSystem) { m_eventSystem = eventSystem; }
    void setWindow(Window* window);

private:
    void installCallbacks();
    void uninstallCallbacks();
    void installNativeTextInput();
    void uninstallNativeTextInput();
    void updateNativeTextInputRect();
    void cancelNativeTextComposition();

    void updateKeyboardState();
    void updateMouseState();
    void updateCursorMode();

    KeyCode mapGLFWKey(int key) const;
    MouseButton mapGLFWMouseButton(int button) const;

    void handleKey(int key, int action);
    void handleChar(unsigned int codepoint);
    void handleScroll(double offsetX, double offsetY);
    void handleCursorPos(double x, double y);
    void handleMouseButton(int button, int action);

    void emitKeyPressed(KeyCode key, bool repeat);
    void emitKeyReleased(KeyCode key);
    void appendCommittedText(unsigned int codepoint, const std::string& utf8);

    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* window, unsigned int codepoint);
    static void scrollCallback(GLFWwindow* window, double offsetX, double offsetY);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

private:
    std::array<bool, static_cast<size_t>(KeyCode::MaxKeys)> m_keys;
    std::array<bool, static_cast<size_t>(KeyCode::MaxKeys)> m_keysLast;

    float m_mouseX;
    float m_mouseY;
    float m_mouseXLast;
    float m_mouseYLast;
    float m_mouseDeltaX;
    float m_mouseDeltaY;
    float m_mouseWheel;
    float m_mouseWheelDelta;
    float m_mouseWheelDeltaX;
    std::array<bool, static_cast<size_t>(MouseButton::MaxButtons)> m_mouseButtons;
    std::array<bool, static_cast<size_t>(MouseButton::MaxButtons)> m_mouseButtonsLast;

    bool m_mouseVisible;
    bool m_mouseLocked;

    bool m_textInputActive;
    std::string m_textInput;
    std::string m_textInputBuffer;
    bool m_textCompositionActive = false;
    std::string m_textComposition;
    uint32_t m_textCompositionCursor = 0;
    uint32_t m_textCompositionSelectionLength = 0;
    float m_textInputRectX = 0.0f;
    float m_textInputRectY = 0.0f;
    float m_textInputRectWidth = 1.0f;
    float m_textInputRectHeight = 1.0f;

    EventSystem* m_eventSystem = nullptr;
    Window* m_window = nullptr;
    GLFWwindow* m_glfwWindow = nullptr;
    bool m_callbacksInstalled = false;
    bool m_nativeTextInputInstalled = false;
};

InputSystem* GetInput();

} // namespace Tina::Engine
