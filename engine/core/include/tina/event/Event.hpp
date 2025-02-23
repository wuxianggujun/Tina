#pragma once

#include "tina/window/Window.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include "tina/core/Core.hpp"
#include <string>

namespace Tina
{
    struct WindowHandle;

    using EventID = uint32_t;

    class TINA_CORE_API Event
    {
    public:
        enum Type
        {
            None = 0,
            WindowCreate,
            WindowClose,
            WindowResize,
            WindowFocus,
            WindowLostFocus,
            WindowMoved,
            Key,
            Char,
            MouseButtonEvent,
            MouseMove,
            MouseScroll,
            DropFile
        };

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

        enum class KeyModifier : int32_t
        {
            None = 0,
            Shift = 1,
            Control = 2,
            Alt = 4,
            Super = 8
        };

        enum class MouseButton : int32_t
        {
            None = -1,
            Left = 0,
            Right = 1,
            Middle = 2
        };

        struct WindowCreateData
        {
            int32_t width;
            int32_t height;
            void* nativeWindowHandle;
        };

        struct WindowResizeData
        {
            int32_t width;
            int32_t height;
        };

        struct KeyData
        {
            KeyCode key;
            int32_t scancode;
            int32_t action;
            KeyModifier mods;
        };

        struct CharData
        {
            uint32_t codepoint;
        };

        struct MouseButtonData
        {
            MouseButton button;
            int32_t action;
            KeyModifier mods;
            double x;
            double y;
        };

        struct MousePosData
        {
            double x;
            double y;
        };

        struct MouseScrollData
        {
            double xoffset;
            double yoffset;
        };

        struct DropFileData
        {
            int32_t count;
            const char** paths;
        };

        explicit Event(Type type) : m_type(type), handled(false) {}
        virtual ~Event() = default;

        [[nodiscard]] Type getType() const { return m_type; }
        [[nodiscard]] const char* getTypeName() const { return getTypeString(m_type); }
        [[nodiscard]] EventID getEventID() const { return static_cast<EventID>(m_type); }

        bool handled;
        WindowHandle windowHandle;

        union
        {
            WindowCreateData windowCreate;
            WindowResizeData windowResize;
            KeyData key;
            CharData character;
            MouseButtonData mouseButton;
            MousePosData mousePos;
            MouseScrollData mouseScroll;
            DropFileData dropFile;
        };

    protected:
        static const char* getTypeString(Type type)
        {
            switch (type)
            {
                case WindowCreate: return "WindowCreate";
                case WindowClose: return "WindowClose";
                case WindowResize: return "WindowResize";
                case WindowFocus: return "WindowFocus";
                case WindowLostFocus: return "WindowLostFocus";
                case WindowMoved: return "WindowMoved";
                case Key: return "Key";
                case Char: return "Char";
                case MouseButtonEvent: return "MouseButton";
                case MouseMove: return "MouseMove";
                case MouseScroll: return "MouseScroll";
                case DropFile: return "DropFile";
                default: return "Unknown";
            }
        }

    private:
        Type m_type;
    };

    // Event creation helper functions
    inline Event createWindowEvent(Event::Type type, WindowHandle handle, void* nativeHandle = nullptr)
    {
        Event event(type);
        event.windowHandle = handle;
        event.windowCreate.width = 0;
        event.windowCreate.height = 0;
        event.windowCreate.nativeWindowHandle = nativeHandle;
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
        Event event(Event::MouseButtonEvent);
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

    // Event pointer type
    using EventPtr = std::shared_ptr<Event>;

    // Event declaration macro
    #define TINA_DECLARE_EVENT(type) \
        static EventID ID() { return reinterpret_cast<EventID>(&ID); } \
        EventID getEventID() const override { return ID(); } \
        const char* getName() const override { return #type; }

    // Event definition macro
    #define TINA_EVENT_BEGIN(NAME) \
    class NAME : public Event { \
    public: \
        TINA_DECLARE_EVENT(NAME) \
        static SharedPtr<NAME> create() { return MakeShared<NAME>(); } \
    private: \
        explicit NAME() = default; \
    public:

    #define TINA_EVENT_END() };
} // namespace Tina
