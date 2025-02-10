#pragma once

#include "tina/window/Window.hpp"

namespace Tina
{
    struct WindowHandle;

    struct Event
    {
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
            MouseButton,
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
                int32_t key;
                int32_t scancode;
                int32_t action;
                int32_t mods;
            } key;

            struct
            {
                uint32_t codepoint;
            } character;

            struct
            {
                int32_t button;
                int32_t action;
                int32_t mods;
            } mouseButton;

            struct
            {
                double x;
                double y;
            } mousePos;

            struct {
                double x;
                double y;
            } cursorPos;

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

        Event(Type type)
            : type(type)
              , windowHandle({UINT16_MAX})
              , handled(false)
        {
        }
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

    inline Event createKeyEvent(WindowHandle handle, int32_t key, int32_t scancode, int32_t action, int32_t mods)
    {
        Event event(Event::Key);
        event.windowHandle = handle;
        event.key.key = key;
        event.key.scancode = scancode;
        event.key.action = action;
        event.key.mods = mods;
        return event;
    }

    inline Event createMouseButtonEvent(WindowHandle handle, int32_t button, int32_t action, int32_t mods)
    {
        Event event(Event::MouseButton);
        event.windowHandle = handle;
        event.mouseButton.button = button;
        event.mouseButton.action = action;
        event.mouseButton.mods = mods;
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
