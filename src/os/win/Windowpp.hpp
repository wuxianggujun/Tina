//
// Created by wuxianggujun on 25-9-27.
//

#pragma once

#include "os/OS.hpp"
#include <string>
#include <EASTL/deque.h>
#include <variant>
#include <functional>
#include <cstdint>
#include "core/utils/StringUtils.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>


namespace Tina::OS
{

    struct Utf
    {
        static std::wstring toWide(const char* str,u32 len);
        static std::wstring toWide(const std::string& str);
        static std::string toUtf8(const wchar_t* str);
    };

    enum class MouseButton : u32 { LEFT = 0, RIGHT = 1, MIDDLE = 2, EXT1 = 3, EXT2 = 4 };

    enum class CursorType : u32 { DEFAULT, SIZE_NS, SIZE_WE, SIZE_NWSE, LOAD, TEXT_INPUT, HAND };

    enum class HitTestResult : u32 { CAPTION, CLIENT, NONE };

    struct QuitEvent { };
    struct WindowCloseEvent { };
    struct KeyEvent { bool down; u32 vk; bool is_repeat; };
    struct CharEvent { u32 utf8; };
    struct MouseButtonEvent { bool down; MouseButton button; };
    struct MouseMoveEvent { int xrel, yrel; };
    struct MouseWheelEvent { float amount; };
    struct WindowSizeEvent { int w, h; };
    struct WindowMoveEvent { int x, y; };
    struct FocusEvent { bool gained; };
    struct DropFileEvent { void* handle; }; // 用 DragQueryFile 解析

    using Event = std::variant<
        QuitEvent,
        WindowCloseEvent,
        KeyEvent,
        CharEvent,
        MouseButtonEvent,
        MouseMoveEvent,
        MouseWheelEvent,
        WindowSizeEvent,
        WindowMoveEvent,
        FocusEvent,
        DropFileEvent
    >;


    struct WindowConfig {
        // 标题/图标
        const char* title = "";
        const char* icon = nullptr; // 路径，可选
        // 初始位置/尺寸
        u32 width = 800;
        u32 height = 600;
        i32 x = 0;
        i32 y = 0;
        // 标志
        bool start_hidden = false;
        bool start_maximized = false;
        bool no_decoration = false;   // 无边框
        bool no_taskbar_icon = false; // 工具窗
        bool handle_file_drops = false;
        bool raw_input_mouse = true;
        // 无边框命中测试回调（决定拖动/缩放/客户端区）
        using HitTestCB = std::function<HitTestResult(HWND, int screen_x, int screen_y)>;
        HitTestCB hit_test;

        // 链式 API（Java 风格小驼峰 + set 前缀）
        WindowConfig& setTitle(const char* t) { title = t; return *this; }
        WindowConfig& setIcon(const char* p) { icon = p; return *this; }
        WindowConfig& setSize(u32 w, u32 h) { width = w; height = h; return *this; }
        WindowConfig& setPos(i32 px, i32 py) { x = px; y = py; return *this; }
        WindowConfig& setHidden(bool v = true) { start_hidden = v; return *this; }
        WindowConfig& setMaximized(bool v = true) { start_maximized = v; return *this; }
        WindowConfig& setNoDecoration(bool v = true) { no_decoration = v; return *this; }
        WindowConfig& setNoTaskbarIcon(bool v = true) { no_taskbar_icon = v; return *this; }
        WindowConfig& setHandleFileDrops(bool v = true) { handle_file_drops = v; return *this; }
        WindowConfig& setRawInputMouse(bool v = true) { raw_input_mouse = v; return *this; }
        WindowConfig& setHitTest(HitTestCB cb) { hit_test = cb; return *this; }
    };

    class Window {
    public:
        struct Rect { int left, top, width, height; };
        struct Point { int x, y; };

        // 仅需调用一次（可选）：预加载光标等
        static void InitOnce();
        // 全局鼠标光标
        static void SetCursor(CursorType type);
        static void ShowCursor(bool show);

        explicit Window(const WindowConfig& cfg);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) noexcept;
        Window& operator=(Window&&) noexcept;

        bool valid() const { return m_hwnd != nullptr; }
        HWND hwnd() const { return m_hwnd; }

        // 事件：非阻塞轮询（内部会驱动 PeekMessage/DispatchMessage）
        bool pollEvent(Event& out);

        // 显示/隐藏/标题
        void show();
        void hide();
        void setTitle(const char* title);

        // 几何/状态
        Rect screenRect() const;
        Point clientSize() const;
        void setScreenRect(const Rect& r);
        void maximize();
        void minimize();
        bool isMaximized() const;
        bool isMinimized() const;

        // 全屏/恢复
        void toFullscreen();
        void restore();

        // 鼠标裁剪
        void clipCursorToWindow();
        void releaseCursor();

        // 拖拽文件帮助（把 DropFileEvent 里 handle 转成第 idx 个路径，返回 UTF-8 字符串）
        static bool GetDropFilePath(const DropFileEvent& ev, int idx, std::string& out);
        static int GetDropFileCount(const DropFileEvent& ev);
        static void FinishDrag(const DropFileEvent& ev);

        // 窗口过程（静态转发）
        static LRESULT CALLBACK WndProc(HWND hWnd,UINT Msg, WPARAM wParam, std::intptr_t lParam);
        
    private:
        struct WindowState { std::uintptr_t style = 0; Rect rect{}; };
        
        LRESULT onMsg(UINT Msg, WPARAM wParam, LPARAM lParam);

        // 事件入队
        void push(Event ev);
        bool pop(Event& out);
        static bool PumpOneMessage(); // 从 Win32 取出一条消息并分发

        // 命中测试（无边框）
        long onNcHitTest(int screen_x, int screen_y);

        HWND m_hwnd = nullptr;
        WindowConfig m_cfg{};
        eastl::deque<Event> m_queue;
        WindowState m_saved; // 全屏前保存
        bool m_is_shown = false;
    };

    
}
