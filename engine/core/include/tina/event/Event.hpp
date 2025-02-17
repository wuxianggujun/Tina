#pragma once

#include "tina/window/Window.hpp"
#include <glm/glm.hpp>
#include <cstdint>

namespace Tina
{
    struct WindowHandle;

    struct Event
    {
        // 鼠标按钮枚举
        enum class MouseButton : int32_t
        {
            None = -1,
            Left = 0,
            Right = 1,
            Middle = 2
        };

        // 按键码枚举
        enum class KeyCode : int32_t
        {
            None = 0,
            A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
            N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
            Key0 = 48, Key1, Key2, Key3, Key4, Key5, Key6, Key7, Key8, Key9,
            F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
            Space = 32,
            Enter = 257,
            Tab = 258,
            Backspace = 259,
            Escape = 256,
            Left = 263,
            Right = 262,
            Up = 265,
            Down = 264,
            Delete = 261,
            Home = 268,
            End = 269,
            PageUp = 266,
            PageDown = 267,
            Insert = 260,
            LeftShift = 340,
            RightShift = 344,
            LeftControl = 341,
            RightControl = 345,
            LeftAlt = 342,
            RightAlt = 346,
            LeftSuper = 343,
            RightSuper = 347
        };

        // 按键修饰符
        enum class KeyModifier : int32_t
        {
            None = 0,
            Shift = 1,
            Control = 2,
            Alt = 4,
            Super = 8
        };

        // 事件类型枚举
        enum Type
        {
            None = 0,
            WindowCreate,
            WindowDestroy,
            WindowResize,
            WindowMove,
            WindowFocus,
            WindowLostFocus,
            WindowClose,
            Key,
            Char,
            MouseButtonEvent,  // 重命名以避免与MouseButton枚举类冲突
            MouseMove,
            MouseScroll,
            MouseEnter,
            MouseLeave,
            CursorPos,
            Gamepad,
            DropFile,
            Exit,
            Count
        };

        Type type;
        WindowHandle windowHandle;
        bool handled = false;

        union
        {
            struct
            {
                void* nativeWindowHandle;
            } window;

            struct
            {
                int32_t width;
                int32_t height;
            } windowResize;

            struct
            {
                int32_t x;
                int32_t y;
            } windowPos;

            struct
            {
                KeyCode key;
                int32_t scancode;
                int32_t action;
                KeyModifier mods;
            } key;

            struct
            {
                uint32_t codepoint;
            } character;

            struct
            {
                MouseButton button;
                int32_t action;
                KeyModifier mods;
                double x;
                double y;
            } mouseButton;

            struct
            {
                double x;
                double y;
            } mousePos;

            struct
            {
                double xoffset;
                double yoffset;
            } mouseScroll;

            struct
            {
                int32_t entered;
            } mouseEnter;

            struct
            {
                int32_t id;
                int32_t connected;
            } gamepadStatus;

            struct
            {
                int32_t id;
                int32_t axis;
                float value;
            } gamepadAxis;

            struct
            {
                int32_t id;
                int32_t button;
                int32_t action;
            } gamepadButton;

            struct {
                int32_t jid;
                int32_t action;
            } gamepad;

            struct {
                int32_t count;
                const char** paths;
            } dropFile;
        };

        Event()
            : type(None)
              , windowHandle({UINT16_MAX})
              , handled(false)
        {
        }

        explicit Event(Type type)
            : type(type)
              , windowHandle({UINT16_MAX})
              , handled(false)
        {
        }

        static bool isKeyPressed(KeyCode key);
        static bool isMouseButtonPressed(MouseButton button);
        static glm::vec2 getMousePosition();
        static void setMousePosition(float x, float y);
        static void setMouseVisible(bool visible);
    };

    // Event creation helper functions
    inline Event createWindowEvent(Event::Type type, WindowHandle handle, void* nativeHandle = nullptr)
    {
        Event event(type);
        event.windowHandle = handle;
        event.window.nativeWindowHandle = nativeHandle;
        return event;
    }

    inline Event createWindowResizeEvent(WindowHandle handle, int32_t width, int32_t height)
    {
        Event event(Event::WindowResize);
        event.windowHandle = handle;
        event.windowResize.width = width;
        event.windowResize.height = height;
        return event;
    }

    inline Event createKeyEvent(WindowHandle handle, Event::KeyCode key, int32_t scancode, int32_t action, Event::KeyModifier mods)
    {
        Event event(Event::Key);
        event.windowHandle = handle;
        event.key.key = key;
        event.key.scancode = scancode;
        event.key.action = action;
        event.key.mods = mods;
        return event;
    }

    inline Event createMouseButtonEvent(WindowHandle handle, Event::MouseButton button, int32_t action, Event::KeyModifier mods, double x, double y)
    {
        Event event(Event::MouseButtonEvent);  // 使用新的事件类型名称
        event.windowHandle = handle;
        event.mouseButton.button = button;
        event.mouseButton.action = action;
        event.mouseButton.mods = mods;
        event.mouseButton.x = x;
        event.mouseButton.y = y;
        return event;
    }

    inline Event createMouseMoveEvent(WindowHandle handle, double x, double y)
    {
        Event event(Event::MouseMove);
        event.windowHandle = handle;
        event.mousePos.x = x;
        event.mousePos.y = y;
        return event;
    }

    inline Event createMouseScrollEvent(WindowHandle handle, double xoffset, double yoffset)
    {
        Event event(Event::MouseScroll);
        event.windowHandle = handle;
        event.mouseScroll.xoffset = xoffset;
        event.mouseScroll.yoffset = yoffset;
        return event;
    }
} // namespace Tina
