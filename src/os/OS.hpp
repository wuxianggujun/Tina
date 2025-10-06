//
// Created by wuxianggujun on 25-9-26.
//

#pragma once

#include "../core/Core.hpp"
#include "../core/Math.hpp"
#include "glm/fwd.hpp"

#ifdef __linux__
#include <pthread.h>
#endif

namespace Tina
{
    struct IAllocator;

    namespace os
    {
#ifdef _WIN32
        using ThreadID = u32;
#else
        using ThreadID = pthread_t;
#endif

        enum class KeyCode : u8;

        // 使用 Tina::Math 常用几何类型
        using Rect = Tina::Math::Rect;
        using Point = Tina::Math::Point;

        enum class MouseButton : i32
        {
            LEFT = 0,
            RIGHT = 1,
            MIDDLE = 2,
            EXTENDED = 3,
            EXTENDED1 = EXTENDED,
            EXTENDED2 = EXTENDED + 1,

            MAX = 16
        };


        using WindowHandle = void*;
        constexpr WindowHandle INVALID_WINDOW_HANDLE = nullptr;

        struct Event
        {
            enum class Type
            {
                QUIT,
                KEY,
                CHAR,
                MOUSE_BUTTON,
                MOUSE_MOVE,
                MOUSE_WHEEL,
                WINDOW_CLOSE,
                WINDOW_SIZE,
                WINDOW_MOVE,
                DROP_FILE,
                FOCUS
            };

            Type type;
            WindowHandle window;

            union
            {
                struct
                {
                    u32 utf8;
                } text_input;

                struct
                {
                    int w, h;
                } win_size;

                struct
                {
                    int x, y;
                } win_move;

                struct
                {
                    bool down;
                    MouseButton button;
                } mouse_button;

                struct
                {
                    int xrel, yrel;
                } mouse_move;

                struct
                {
                    bool down;
                    KeyCode key_code;
                    bool is_repeat;
                } key;

                struct
                {
                    void* handle;
                } file_drop;

                struct
                {
                    float amount;
                } mouse_wheel;

                struct
                {
                    bool gained;
                } focus;
            };
        };

        enum class HitTestResult : i32
        {
            CAPTION,
            CLIENT,
            NONE
        };

        struct InitWindowArgs
        {
            using HitTestCallback = HitTestResult(*)(void*, os::WindowHandle, Point);

            enum Flags
            {
                NO_DECORATION = 1 << 0,
                NO_TASKBAR_ICON = 1 << 1
            };

            const char* name = "";
            const char* icon = nullptr;
            bool handle_file_drops = false;
            u32 flags = 0;
            WindowHandle parent = INVALID_WINDOW_HANDLE;
            HitTestCallback hit_test_callback = nullptr;
            void* user_data = nullptr;
            u32 width = 800;
            u32 height = 600;
            i32 x = 0;
            i32 y = 0;
            bool is_maximized = false;
            bool is_hidden = false;
        };

        struct WindowState
        {
            u64 style;
            Rect rect;
        };

        
        TINA_CORE_API WindowHandle createWindow(const InitWindowArgs& args);
        TINA_CORE_API void showWindow(WindowHandle wnd);
        TINA_CORE_API void hideWindow(WindowHandle wnd);
        TINA_CORE_API bool getEvent(Event& event);
        TINA_CORE_API void destroyWindow(WindowHandle wnd);
        TINA_CORE_API Rect getWindowScreenRect(WindowHandle win);
        TINA_CORE_API Point getWindowClientSize(WindowHandle win);
        TINA_CORE_API void setWindowTitle(WindowHandle win, const char* title);
        TINA_CORE_API void maximizeWindow(WindowHandle win);
        TINA_CORE_API void minimizeWindow(WindowHandle win);

        TINA_CORE_API WindowState setFullScreen(WindowHandle win);
        TINA_CORE_API void restoreWindow(WindowHandle win, const WindowState& prev);

        // 坐标换算（对齐文档中 Runner 用法）
        TINA_CORE_API Point clientToScreen(WindowHandle win, int x, int y);
        TINA_CORE_API Point screenToClient(WindowHandle win, int x, int y);

        // 鼠标/光标与裁剪
        TINA_CORE_API void showCursor(bool show);
        TINA_CORE_API bool setRelativeMouseMode(WindowHandle win, bool enabled);
        TINA_CORE_API void clipCursor(WindowHandle win, const Rect& rect);

        // 文件拖拽访问器（DROP_FILE 事件配套）
        TINA_CORE_API int getDropFileCount(void* handle);
        TINA_CORE_API const char* getDropFile(void* handle, int index);
        TINA_CORE_API void finishDrag(void* handle);
        

        // 键码采用 Windows VK 值，跨平台时做映射
        enum class KeyCode : u8
        {
            INVALID = 0,

            LBUTTON = 0x01,
            RBUTTON = 0x02,
            CANCEL = 0x03,
            MBUTTON = 0x04,
            BACKSPACE = 0x08,
            TAB = 0x09,
            CLEAR = 0x0C,
            RETURN = 0x0D,
            SHIFT = 0x10,
            CTRL = 0x11,
            ALT = 0x12,
            PAUSE = 0x13,
            CAPITAL = 0x14,
            KANA = 0x15,
            HANGEUL = 0x15,
            HANGUL = 0x15,
            JUNJA = 0x17,
            FINAL = 0x18,
            HANJA = 0x19,
            KANJI = 0x19,
            ESCAPE = 0x1B,
            CONVERT = 0x1C,
            NONCONVERT = 0x1D,
            ACCEPT = 0x1E,
            MODECHANGE = 0x1F,
            SPACE = 0x20,
            PAGEUP = 0x21,
            PAGEDOWN = 0x22,
            END = 0x23,
            HOME = 0x24,
            LEFT = 0x25,
            UP = 0x26,
            RIGHT = 0x27,
            DOWN = 0x28,
            SELECT = 0x29,
            PRINT = 0x2A,
            EXECUTE = 0x2B,
            SNAPSHOT = 0x2C,
            INSERT = 0x2D,
            DEL = 0x2E,
            HELP = 0x2F,

            // === 主键盘数字键（0x30-0x39）===
            KEY_0 = 0x30,
            KEY_1 = 0x31,
            KEY_2 = 0x32,
            KEY_3 = 0x33,
            KEY_4 = 0x34,
            KEY_5 = 0x35,
            KEY_6 = 0x36,
            KEY_7 = 0x37,
            KEY_8 = 0x38,
            KEY_9 = 0x39,

            // === 字母键（0x41-0x5A）===
            A = 0x41,
            B = 0x42,
            C = 0x43,
            D = 0x44,
            E = 0x45,
            F = 0x46,
            G = 0x47,
            H = 0x48,
            I = 0x49,
            J = 0x4A,
            K = 0x4B,
            L = 0x4C,
            M = 0x4D,
            N = 0x4E,
            O = 0x4F,
            P = 0x50,
            Q = 0x51,
            R = 0x52,
            S = 0x53,
            T = 0x54,
            U = 0x55,
            V = 0x56,
            W = 0x57,
            X = 0x58,
            Y = 0x59,
            Z = 0x5A,

            LWIN = 0x5B,
            RWIN = 0x5C,
            APPS = 0x5D,
            SLEEP = 0x5F,
            NUMPAD0 = 0x60,
            NUMPAD1 = 0x61,
            NUMPAD2 = 0x62,
            NUMPAD3 = 0x63,
            NUMPAD4 = 0x64,
            NUMPAD5 = 0x65,
            NUMPAD6 = 0x66,
            NUMPAD7 = 0x67,
            NUMPAD8 = 0x68,
            NUMPAD9 = 0x69,
            MULTIPLY = 0x6A,
            ADD = 0x6B,
            SEPARATOR = 0x6C,
            SUBTRACT = 0x6D,
            DECIMAL = 0x6E,
            DIVIDE = 0x6F,
            F1 = 0x70,
            F2 = 0x71,
            F3 = 0x72,
            F4 = 0x73,
            F5 = 0x74,
            F6 = 0x75,
            F7 = 0x76,
            F8 = 0x77,
            F9 = 0x78,
            F10 = 0x79,
            F11 = 0x7A,
            F12 = 0x7B,
            F13 = 0x7C,
            F14 = 0x7D,
            F15 = 0x7E,
            F16 = 0x7F,
            F17 = 0x80,
            F18 = 0x81,
            F19 = 0x82,
            F20 = 0x83,
            F21 = 0x84,
            F22 = 0x85,
            F23 = 0x86,
            F24 = 0x87,
            NUMLOCK = 0x90,
            SCROLL = 0x91,
            OEM_NEC_EQUAL = 0x92,
            OEM_FJ_JISHO = 0x92,
            OEM_FJ_MASSHOU = 0x93,
            OEM_FJ_TOUROKU = 0x94,
            OEM_FJ_LOYA = 0x95,
            OEM_FJ_ROYA = 0x96,
            LSHIFT = 0xA0,
            RSHIFT = 0xA1,
            LCTRL = 0xA2,
            RCTRL = 0xA3,
            LALT = 0xA4,
            RALT = 0xA5,
            BROWSER_BACK = 0xA6,
            BROWSER_FORWARD = 0xA7,
            BROWSER_REFRESH = 0xA8,
            BROWSER_STOP = 0xA9,
            BROWSER_SEARCH = 0xAA,
            BROWSER_FAVORITES = 0xAB,
            BROWSER_HOME = 0xAC,
            VOLUME_MUTE = 0xAD,
            VOLUME_DOWN = 0xAE,
            VOLUME_UP = 0xAF,
            MEDIA_NEXT_TRACK = 0xB0,
            MEDIA_PREV_TRACK = 0xB1,
            MEDIA_STOP = 0xB2,
            MEDIA_PLAY_PAUSE = 0xB3,
            LAUNCH_MAIL = 0xB4,
            LAUNCH_MEDIA_SELECT = 0xB5,
            LAUNCH_APP1 = 0xB6,
            LAUNCH_APP2 = 0xB7,
            OEM_1 = 0xBA,
            OEM_PLUS = 0xBB,
            OEM_COMMA = 0xBC,
            OEM_MINUS = 0xBD,
            OEM_PERIOD = 0xBE,
            OEM_2 = 0xBF,
            OEM_3 = 0xC0,
            OEM_4 = 0xDB,
            OEM_5 = 0xDC,
            OEM_6 = 0xDD,
            OEM_7 = 0xDE,
            OEM_8 = 0xDF,
            OEM_AX = 0xE1,
            OEM_102 = 0xE2,
            ICO_HELP = 0xE3,
            ICO_00 = 0xE4,
            PROCESSKEY = 0xE5,
            ICO_CLEAR = 0xE6,
            PACKET = 0xE7,
            OEM_RESET = 0xE9,
            OEM_JUMP = 0xEA,
            OEM_PA1 = 0xEB,
            OEM_PA2 = 0xEC,
            OEM_PA3 = 0xED,
            OEM_WSCTRL = 0xEE,
            OEM_CUSEL = 0xEF,
            OEM_ATTN = 0xF0,
            OEM_FINISH = 0xF1,
            OEM_COPY = 0xF2,
            OEM_AUTO = 0xF3,
            OEM_ENLW = 0xF4,
            OEM_BACKTAB = 0xF5,
            ATTN = 0xF6,
            CRSEL = 0xF7,
            EXSEL = 0xF8,
            EREOF = 0xF9,
            PLAY = 0xFA,
            ZOOM = 0xFB,
            NONAME = 0xFC,
            PA1 = 0xFD,
            OEM_CLEAR = 0xFE,

            MAX = 0xff
        };
    }
}
