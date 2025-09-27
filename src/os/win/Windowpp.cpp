//
// Created by wuxianggujun on 25-9-27.
//

#include "Windowpp.hpp"

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <Psapi.h>
#include <cassert>
#include <cstring>
#include <cwchar>
#include <iterator>


namespace Tina::OS
{
    std::wstring Utf::toWide(const char* str, u32 len)
    {
        if (!str || len == 0) return {};
        return Core::StringUtils::utf8ToWstring(std::string_view(str, len));
    }

    std::wstring Utf::toWide(const std::string& str)
    {
        return Core::StringUtils::utf8ToWstring(str);
    }

    std::string Utf::toUtf8(const wchar_t* str)
    {
        if (!str) return {};
        return Core::StringUtils::wstringToUtf8(str);
    }

    static struct
    {
        HCURSOR arrow = nullptr;
        HCURSOR text = nullptr;
        HCURSOR waitc = nullptr;
        HCURSOR size_ns = nullptr;
        HCURSOR size_ws = nullptr;
        HCURSOR size_nwse = nullptr;
        HCURSOR hand = nullptr;
        CursorType current = CursorType::DEFAULT;
    } GCursor;

    void Window::InitOnce()
    {
        GCursor.arrow = ::LoadCursor(nullptr, IDC_ARROW);
        GCursor.text = ::LoadCursor(nullptr, IDC_ARROW);
        GCursor.waitc = ::LoadCursor(nullptr, IDC_WAIT);
        GCursor.size_ns = ::LoadCursor(nullptr, IDC_SIZENS);
        GCursor.size_ws = ::LoadCursor(nullptr, IDC_SIZEWE);
        GCursor.size_nwse = ::LoadCursor(nullptr, IDC_SIZENWSE);
        GCursor.hand = ::LoadCursor(nullptr, IDC_HAND);
    }

    void Window::SetCursor(CursorType type)
    {
        GCursor.current = type;
        switch (type)
        {
        case CursorType::DEFAULT:
            ::SetCursor(GCursor.arrow);
            break;
        case CursorType::TEXT_INPUT:
            ::SetCursor(GCursor.text);
            break;
        case CursorType::LOAD:
            ::SetCursor(GCursor.waitc);
            break;
        case CursorType::SIZE_NS:
            ::SetCursor(GCursor.size_ns);
            break;
        case CursorType::SIZE_WE:
            ::SetCursor(GCursor.size_ws);
            break;
        case CursorType::SIZE_NWSE:
            ::SetCursor(GCursor.size_nwse);
            break;
        case CursorType::HAND:
            ::SetCursor(GCursor.hand);
            break;
        }
    }

    void Window::ShowCursor(bool show)
    {
        if (show)
        {
            while (::ShowCursor(TRUE) < 0)
            {
            }
        }
        else
        {
            while (::ShowCursor(FALSE) >= 0)
            {
            }
        }
    }

    static const wchar_t* GetClassNameW()
    {
        static wchar_t kClass[] = L"winpp_window";
        static bool once = false;
        if (!once)
        {
            WNDCLASSW wc{};
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = &Window::WndProc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
            wc.hCursor = nullptr;
            wc.hbrBackground = nullptr;
            wc.lpszClassName = kClass;
            ::RegisterClassW(&wc);
            once = true;
        }
        return kClass;
    }


    Window::Window(const WindowConfig& cfg): m_cfg(cfg)
    {
        // 计算窗口样式
        DWORD style = cfg.no_decoration
                          ? (cfg.hit_test ? (WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX) : 0)
                          : WS_OVERLAPPEDWINDOW;
        DWORD exStyle = cfg.no_taskbar_icon ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;

        // 窗口名/图标
        std::wstring wtitle = Utf::toWide(cfg.title, std::strlen(cfg.title));
        HICON hIcon = nullptr;
        if (cfg.icon && *cfg.icon)
        {
            std::wstring wicon = Utf::toWide(cfg.icon, std::strlen(cfg.icon));
            hIcon = static_cast<HICON>(::LoadImageW(nullptr, wicon.c_str(),IMAGE_ICON, 32, 32,LR_LOADFROMFILE));
        }

        if (!hIcon) hIcon = ::LoadIcon(nullptr,IDI_APPLICATION);

        GetClassNameW();

        m_hwnd = ::CreateWindowExW(exStyle, GetClassNameW(), wtitle.c_str(), style, cfg.x, cfg.y,
                                   static_cast<int>(cfg.width), static_cast<int>(cfg.height), nullptr,
                                   nullptr, ::GetModuleHandleW(nullptr), this);

        if (!m_hwnd) return;

        // 设置大图标
        ::SendMessageW(m_hwnd,WM_SETICON,ICON_BIG, reinterpret_cast<LPARAM>(hIcon));

        // 文件拖拽
        if (cfg.handle_file_drops) ::DragAcceptFiles(m_hwnd,TRUE);

        if (cfg.raw_input_mouse)
        {
            RAWINPUTDEVICE device{};
            device.usUsagePage = 0x01;
            device.usUsage = 0x02;
            device.dwFlags = RIDEV_INPUTSINK;
            device.hwndTarget = m_hwnd;
            ::RegisterRawInputDevices(&device, 1, sizeof(device));
        }

        if (!cfg.start_hidden)
        {
            m_is_shown = true;
            ::ShowWindow(m_hwnd, SW_SHOW);
            if (cfg.start_maximized) ::ShowWindow(m_hwnd,SW_MAXIMIZE);
        }
    }

    Window::~Window()
    {
        if (m_hwnd)
        {
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    Window::Window(Window&& rhs) noexcept
    {
        m_hwnd = rhs.m_hwnd;
        rhs.m_hwnd = nullptr;

        m_cfg = rhs.m_cfg;
        m_queue = std::move(rhs.m_queue);
        m_saved = rhs.m_saved;
        m_is_shown = rhs.m_is_shown;
    }

    Window& Window::operator=(Window&& rhs) noexcept
    {
        if (this == &rhs) return *this;
        if (m_hwnd) ::DestroyWindow(m_hwnd);
        m_hwnd = rhs.m_hwnd;
        rhs.m_hwnd = nullptr;

        m_cfg = rhs.m_cfg;
        m_queue = std::move(rhs.m_queue);
        m_saved = rhs.m_saved;
        m_is_shown = rhs.m_is_shown;
        return *this;
    }

    bool Window::pollEvent(Event& out)
    {
        if (pop(out)) return true;
        while (PumpOneMessage()){
            if (pop(out)) return true;
        }
        return false;
    }

    void Window::show()
    {
        if (m_hwnd)
        {
            m_is_shown = true;
            ::ShowWindow(m_hwnd, SW_SHOW);
        }
    }

    void Window::hide()
    {
        if (m_hwnd)
        {
            m_is_shown = false;
            ::ShowWindow(m_hwnd, SW_HIDE);
        }
    }

    void Window::setTitle(const char* title)
    {
        if (!m_hwnd) return;
        std::wstring wtitle = Utf::toWide(title, std::strlen(title));
        ::SetWindowTextW(m_hwnd, wtitle.c_str());
    }

    Window::Rect Window::screenRect() const
    {
        RECT r{};
        ::GetWindowRect(m_hwnd, &r);
        return {r.left, r.top, r.right - r.left, r.bottom - r.top};
    }

    Window::Point Window::clientSize() const
    {
        RECT r{};
        ::GetClientRect(m_hwnd, &r);
        return {r.right - r.left, r.bottom - r.top};
    }

    void Window::setScreenRect(const Rect& r)
    {
        ::MoveWindow(m_hwnd, r.left, r.top, r.width(), r.height(), TRUE);
    }

    void Window::maximize()
    {
        if (m_hwnd && m_is_shown) ::ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);
    }

    void Window::minimize()
    {
        if (m_hwnd && m_is_shown) ::ShowWindow(m_hwnd, SW_SHOWMINIMIZED);
    }

    bool Window::isMaximized() const
    {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        ::GetWindowPlacement(m_hwnd, &wp);
        return wp.showCmd == SW_SHOWMAXIMIZED;
    }

    bool Window::isMinimized() const
    {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        ::GetWindowPlacement(m_hwnd, &wp);
        return wp.showCmd == SW_SHOWMINIMIZED;
    }

    void Window::toFullscreen()
    {
        if (!m_hwnd) return;

        m_saved.style = ::GetWindowLongPtrW(m_hwnd, GWL_STYLE);
        RECT r{};
        ::GetWindowRect(m_hwnd, &r);
        m_saved.rect = {r.left, r.top, r.right - r.left, r.bottom - r.top};

        ::SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_VISIBLE);
        int w = ::GetSystemMetrics(SM_CXSCREEN);
        int h = ::GetSystemMetrics(SM_CYSCREEN);
        ::SetWindowPos(m_hwnd, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED);
    }

    void Window::restore()
    {
        if (!m_hwnd) return;
        if (m_saved.style)
        {
            ::SetWindowLongPtrW(m_hwnd, GWL_STYLE, static_cast<LONG_PTR>(m_saved.style));
            setScreenRect(m_saved.rect);
        }else if (m_is_shown)
        {
            ::ShowWindow(m_hwnd, SW_RESTORE);
        }
    }

    void Window::clipCursorToWindow()
    {
        if (!m_hwnd) return;
        RECT r{};
        ::GetWindowRect(m_hwnd, &r);
        ::ClipCursor(&r);
    }

    void Window::releaseCursor()
    {
        ::ClipCursor(nullptr);
    }

    bool Window::GetDropFilePath(const DropFileEvent& ev, int idx, std::string& out)
    {
        HDROP drop = static_cast<HDROP>(ev.handle);
        wchar_t buf[MAX_PATH];
        if (::DragQueryFileW(drop, idx, buf, MAX_PATH))
        {
            out = Utf::toUtf8(buf);
            return true;
        }
        return false;
    }

    int Window::GetDropFileCount(const DropFileEvent& ev)
    {
        return static_cast<int>(::DragQueryFileW(static_cast<HDROP>(ev.handle), 0xFFFFFFFF, nullptr, 0));
    }

    void Window::FinishDrag(const DropFileEvent& ev)
    {
        ::DragFinish(static_cast<HDROP>(ev.handle));
    }

 LRESULT Window::onMsg(UINT Msg, WPARAM wParam, LPARAM lParam) {
    switch (Msg) {
        case WM_NCCREATE: {
            // 已在 WndProc 里处理：保存 GWLP_USERDATA
            break;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) { SetCursor(GCursor.current); return 1; }
            break;
        }
        case WM_EXITSIZEMOVE: {
            push(MouseButtonEvent{ false, MouseButton::LEFT });
            break;
        }
        case WM_MOVE: {
            push(WindowMoveEvent{ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) });
            return 0;
        }
        case WM_SIZE: {
            push(WindowSizeEvent{ (int)LOWORD(lParam), (int)HIWORD(lParam) });
            return 0;
        }
        case WM_CLOSE: { push(WindowCloseEvent{}); return 0; }
        case WM_ACTIVATE: {
            if (wParam == WA_INACTIVE) ShowCursor(true);
            push(FocusEvent{ wParam != WA_INACTIVE });
            break;
        }
        case WM_SYSKEYDOWN: {
            if (wParam == VK_MENU) break; // Alt 键防止系统“哔声”
            push(KeyEvent{ true, (u32)wParam, (bool)(lParam & (1 << 30)) });
            break;
        }
        case WM_SYSKEYUP: {
            push(KeyEvent{ false, (u32)wParam, false });
            break;
        }
        case WM_KEYDOWN: { push(KeyEvent{ true, (u32)wParam, (bool)(lParam & (1 << 30)) }); break; }
        case WM_KEYUP: { push(KeyEvent{ false, (u32)wParam, false }); break; }
        case WM_CHAR: {
            // 将 UTF-16 代理项组装为 UTF-32，再编码到 u32 小缓冲里（参考 os 实现）
            static wchar_t s_surrogate = 0;
            wchar_t wc = (wchar_t)wParam;
            u32 utf32 = 0;
            if (wc >= 0xd800 && wc <= 0xdbff) { s_surrogate = wc; return 0; }
            if (wc >= 0xdc00 && wc <= 0xdfff && s_surrogate) {
                utf32 = (((u32)s_surrogate - 0xd800) << 10) + ((u32)wc - 0xdc00) + 0x10000;
            } else {
                utf32 = (u32)wc;
            }
            s_surrogate = 0;

            // UTF-32 -> UTF-8（最多 4 字节），放到 u32 中
            u32 out_utf8 = 0;
            char* p = (char*)&out_utf8;
            if (utf32 <= 0x7F) { p[0] = (char)utf32; }
            else if (utf32 <= 0x7FF) { p[0] = 0xC0 | (char)((utf32 >> 6) & 0x1F); p[1] = 0x80 | (char)(utf32 & 0x3F); }
            else if (utf32 <= 0xFFFF) { p[0] = 0xE0 | (char)((utf32 >> 12) & 0x0F); p[1] = 0x80 | (char)((utf32 >> 6) & 0x3F); p[2] = 0x80 | (char)(utf32 & 0x3F); }
            else { p[0] = 0xF0 | (char)((utf32 >> 18) & 0x0F); p[1] = 0x80 | (char)((utf32 >> 12) & 0x3F); p[2] = 0x80 | (char)((utf32 >> 6) & 0x3F); p[3] = 0x80 | (char)(utf32 & 0x3F); }
            push(CharEvent{ out_utf8 });
            break;
        }
        case WM_INPUT: {
            HRAWINPUT hRaw = (HRAWINPUT)lParam;
            UINT sz = 0;
            ::GetRawInputData(hRaw, RID_INPUT, nullptr, &sz, sizeof(RAWINPUTHEADER));
            alignas(RAWINPUT) char buf[1024]; if (sz == 0 || sz > sizeof(buf)) break;
            ::GetRawInputData(hRaw, RID_INPUT, buf, &sz, sizeof(RAWINPUTHEADER));
            const RAWINPUT* raw = (const RAWINPUT*)buf;
            if (raw->header.dwType != RIM_TYPEMOUSE) break;
            const RAWMOUSE& m = raw->data.mouse;
            const USHORT flags = m.usButtonFlags;
            const short wheel_delta = (short)m.usButtonData;

            if (wheel_delta) push(MouseWheelEvent{ (float)wheel_delta / WHEEL_DELTA });

            if (flags & RI_MOUSE_LEFT_BUTTON_DOWN) push(MouseButtonEvent{ true, MouseButton::LEFT });
            if (flags & RI_MOUSE_LEFT_BUTTON_UP) push(MouseButtonEvent{ false, MouseButton::LEFT });
            if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN) push(MouseButtonEvent{ true, MouseButton::RIGHT });
            if (flags & RI_MOUSE_RIGHT_BUTTON_UP) push(MouseButtonEvent{ false, MouseButton::RIGHT });
            if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) push(MouseButtonEvent{ true, MouseButton::MIDDLE });
            if (flags & RI_MOUSE_MIDDLE_BUTTON_UP) push(MouseButtonEvent{ false, MouseButton::MIDDLE });
            if (flags & RI_MOUSE_BUTTON_4_DOWN) push(MouseButtonEvent{ true, MouseButton::EXT1 });
            if (flags & RI_MOUSE_BUTTON_4_UP) push(MouseButtonEvent{ false, MouseButton::EXT1 });
            if (flags & RI_MOUSE_BUTTON_5_DOWN) push(MouseButtonEvent{ true, MouseButton::EXT2 });
            if (flags & RI_MOUSE_BUTTON_5_UP) push(MouseButtonEvent{ false, MouseButton::EXT2 });

            LONG x = m.lLastX, y = m.lLastY;
            if (x != 0 || y != 0) push(MouseMoveEvent{ (int)x, (int)y });
            break;
        }
        case WM_DROPFILES: { push(DropFileEvent{ (void*)wParam }); break; }
        default: break;
    }

    return ::DefWindowProcW(m_hwnd, Msg, wParam, lParam);
}

LRESULT CALLBACK Window::WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_NCCREATE) {
        // 在创建时把 this 存到 GWLP_USERDATA
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = reinterpret_cast<Window*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hWnd;
        return ::DefWindowProcW(hWnd, Msg, wParam, lParam);
    }
    Window* self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!self) return ::DefWindowProcW(hWnd, Msg, wParam, lParam);

    if (Msg == WM_NCHITTEST && self->m_cfg.no_decoration) {
      return self->onNcHitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    }

    return self->onMsg(Msg, wParam, lParam);
}

    void Window::push(Event ev)
    {
        m_queue.emplace_back(ev);
    }

    bool Window::pop(Event& out)
    {
        if (m_queue.empty()) return false;
        out = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    bool Window::PumpOneMessage()
    {
        MSG msg{};
        if (!::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            return false;

        if (msg.message == WM_SYSCOMMAND )
        {
            if (msg.wParam != SC_KEYMENU || (msg.lParam >> 16) > 0)
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            return true;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        return true;
    }

    long Window::onNcHitTest(int screen_x, int screen_y) {
        // 无边框命中测试：
        // 1) 若用户提供命中测试回调，先询问其意图（CAPTION/CLIENT/NONE）
        // 2) 边缘判断：返回 HT* 以支持缩放/拖动
        POINT pt{ screen_x, screen_y };
        RECT r{}; ::GetWindowRect(m_hwnd, &r);

        if (m_cfg.hit_test) {
            switch (m_cfg.hit_test(m_hwnd, pt.x, pt.y)) {
            case HitTestResult::CAPTION: {
                    const int border_y = ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
                    if (pt.y < r.top + border_y) break; // 边框区域走默认分支
                    return HTCAPTION;
            }
            case HitTestResult::CLIENT: return HTCLIENT;
            case HitTestResult::NONE: default: break;
            }
        }

        const POINT border{ ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
                            ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER) };
        const bool left = pt.x < (r.left + border.x);
        const bool right = pt.x >= (r.right - border.x);
        const bool top = pt.y < (r.top + border.y);
        const bool bottom = pt.y >= (r.bottom - border.y);
        if (left && top) return HTTOPLEFT;
        if (right && top) return HTTOPRIGHT;
        if (left && bottom) return HTBOTTOMLEFT;
        if (right && bottom) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }

}
