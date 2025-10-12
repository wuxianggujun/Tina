//
// Window.cpp - 窗口管理实现（SDL3）
//

#include "Window.hpp"
#include "InputSystem.hpp"  // 包含 KeyCode 和 MouseButton 定义
#include "../core/Log.hpp"
#include <SDL3/SDL.h>

namespace Tina::Engine {

Window::Window()
    : m_window(nullptr)
{
}

Window::~Window() {
    destroy();
}

bool Window::create(const WindowDesc& desc) {
    if (m_window) {
        TINA_ERROR("Window already created");
        return false;
    }

    // 保存描述
    m_desc = desc;
    m_title = desc.title;

    // SDL初始化（如果还没初始化）
    // SDL3 中 SDL_Init 返回 bool
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        TINA_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    // 设置窗口标志
    // 注意：移除SDL_WINDOW_HIGH_PIXEL_DENSITY，因为它在Windows上可能导致坐标不一致
    uint32_t flags = 0;

    if (desc.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (!desc.visible) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (desc.maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
    }
    if (desc.borderless) {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    // SDL3 中 SDL_CreateWindow 的参数改变了，不再包含位置参数
    // 创建窗口
    m_window = SDL_CreateWindow(
        desc.title,
        desc.width, desc.height,
        flags
    );

    // 如果需要设置位置，在创建后设置
    if (m_window) {
        if (desc.x != -1 && desc.y != -1) {
            SDL_SetWindowPosition((SDL_Window*)m_window, desc.x, desc.y);
        }
    }

    if (!m_window) {
        TINA_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    TINA_INFO("Window created: {}x{} '{}'", desc.width, desc.height, desc.title);
    return true;
}

void Window::destroy() {
    if (m_window) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(m_window));
        m_window = nullptr;
        TINA_INFO("Window destroyed");
    }
}

bool Window::isValid() const {
    return m_window != nullptr;
}

// ==================== 窗口属性 ====================

void Window::setTitle(const char* title) {
    if (!m_window) return;
    m_title = title;
    SDL_SetWindowTitle(static_cast<SDL_Window*>(m_window), title);
}

const char* Window::getTitle() const {
    return m_title.c_str();
}

void Window::setSize(int width, int height) {
    if (!m_window) return;
    SDL_SetWindowSize(static_cast<SDL_Window*>(m_window), width, height);
    m_desc.width = width;
    m_desc.height = height;
}

void Window::getSize(int& width, int& height) const {
    if (!m_window) {
        width = height = 0;
        return;
    }
    // 使用SDL_GetWindowSize获取逻辑尺寸，这是SDL事件和鼠标坐标使用的坐标系
    SDL_GetWindowSize(static_cast<SDL_Window*>(m_window), &width, &height);
}

int Window::getWidth() const {
    int w, h;
    getSize(w, h);
    return w;
}

int Window::getHeight() const {
    int w, h;
    getSize(w, h);
    return h;
}

void Window::getSizeInPixels(int& width, int& height) const {
    if (!m_window) {
        width = height = 0;
        return;
    }
    // 使用SDL_GetWindowSizeInPixels获取物理像素尺寸（用于渲染）
    SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(m_window), &width, &height);
}

void Window::setPosition(int x, int y) {
    if (!m_window) return;
    SDL_SetWindowPosition(static_cast<SDL_Window*>(m_window), x, y);
}

void Window::getPosition(int& x, int& y) const {
    if (!m_window) {
        x = y = 0;
        return;
    }
    SDL_GetWindowPosition(static_cast<SDL_Window*>(m_window), &x, &y);
}

// ==================== 显示状态 ====================

void Window::show() {
    if (!m_window) return;
    SDL_ShowWindow(static_cast<SDL_Window*>(m_window));
    m_desc.visible = true;
}

void Window::hide() {
    if (!m_window) return;
    SDL_HideWindow(static_cast<SDL_Window*>(m_window));
    m_desc.visible = false;
}

bool Window::isVisible() const {
    if (!m_window) return false;
    uint32_t flags = SDL_GetWindowFlags(static_cast<SDL_Window*>(m_window));
    return !(flags & SDL_WINDOW_HIDDEN);
}

void Window::minimize() {
    if (!m_window) return;
    SDL_MinimizeWindow(static_cast<SDL_Window*>(m_window));
}

void Window::maximize() {
    if (!m_window) return;
    SDL_MaximizeWindow(static_cast<SDL_Window*>(m_window));
    m_desc.maximized = true;
}

void Window::restore() {
    if (!m_window) return;
    SDL_RestoreWindow(static_cast<SDL_Window*>(m_window));
    m_desc.maximized = false;
}

bool Window::isMinimized() const {
    if (!m_window) return false;
    uint32_t flags = SDL_GetWindowFlags(static_cast<SDL_Window*>(m_window));
    return (flags & SDL_WINDOW_MINIMIZED) != 0;
}

bool Window::isMaximized() const {
    if (!m_window) return false;
    uint32_t flags = SDL_GetWindowFlags(static_cast<SDL_Window*>(m_window));
    return (flags & SDL_WINDOW_MAXIMIZED) != 0;
}

// ==================== 全屏 ====================

void Window::setFullscreen(bool fullscreen) {
    if (!m_window) return;

    SDL_SetWindowFullscreen(
        static_cast<SDL_Window*>(m_window),
        fullscreen ? SDL_WINDOW_FULLSCREEN : 0
    );
    m_desc.fullscreen = fullscreen;
}

bool Window::isFullscreen() const {
    if (!m_window) return false;
    uint32_t flags = SDL_GetWindowFlags(static_cast<SDL_Window*>(m_window));
    return (flags & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::toggleFullscreen() {
    setFullscreen(!isFullscreen());
}

// ==================== 其他属性 ====================

void Window::setResizable(bool resizable) {
    if (!m_window) return;
    SDL_SetWindowResizable(static_cast<SDL_Window*>(m_window), resizable ? true : false);
    m_desc.resizable = resizable;
}

bool Window::isResizable() const {
    if (!m_window) return false;
    uint32_t flags = SDL_GetWindowFlags(static_cast<SDL_Window*>(m_window));
    return (flags & SDL_WINDOW_RESIZABLE) != 0;
}

void Window::setBorderless(bool borderless) {
    if (!m_window) return;
    SDL_SetWindowBordered(static_cast<SDL_Window*>(m_window), borderless ? false : true);
    m_desc.borderless = borderless;
}

bool Window::isBorderless() const {
    if (!m_window) return false;
    uint32_t flags = SDL_GetWindowFlags(static_cast<SDL_Window*>(m_window));
    return (flags & SDL_WINDOW_BORDERLESS) != 0;
}

void Window::focus() {
    if (!m_window) return;
    SDL_RaiseWindow(static_cast<SDL_Window*>(m_window));
}

bool Window::hasFocus() const {
    if (!m_window) return false;
    uint32_t flags = SDL_GetWindowFlags(static_cast<SDL_Window*>(m_window));
    return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

// ==================== 平台相关 ====================

void* Window::getNativeHandle() const {
    if (!m_window) return nullptr;

    SDL_PropertiesID props = SDL_GetWindowProperties(static_cast<SDL_Window*>(m_window));

#ifdef _WIN32
    // Windows: 返回HWND
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    // macOS: 返回NSWindow*
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(__linux__)
    // Linux: 返回X11 Window或Wayland surface
    void* handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_WINDOW_POINTER, nullptr);
    if (!handle) {
        handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    }
    return handle;
#else
    return nullptr;
#endif
}

void* Window::getNativeDisplayHandle() const {
    if (!m_window) return nullptr;

    SDL_PropertiesID props = SDL_GetWindowProperties(static_cast<SDL_Window*>(m_window));

#ifdef __linux__
    // Linux X11: 返回Display*
    void* display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    if (display) return display;

    // Linux Wayland: 返回wl_display*
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
#else
    return nullptr;
#endif
}

// ==================== 工具方法 ====================

void Window::center() {
    if (!m_window) return;

    // 获取显示器尺寸
    SDL_DisplayID displayID = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(displayID);
    if (mode) {
        int w, h;
        getSize(w, h);

        int x = (mode->w - w) / 2;
        int y = (mode->h - h) / 2;

        setPosition(x, y);
    }
}

void Window::flash() {
    if (!m_window) return;
    SDL_FlashWindow(static_cast<SDL_Window*>(m_window), SDL_FLASH_UNTIL_FOCUSED);
}

void Window::setOpacity(float opacity) {
    if (!m_window) return;
    SDL_SetWindowOpacity(static_cast<SDL_Window*>(m_window), opacity);
}

// SDL扫描码到自定义KeyCode的映射
static KeyCode mapSDLScancodeToKeyCode(int scancode) {
    // 字母键 A-Z (SDL_SCANCODE_A=4 → KeyCode::A='A'=65)
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return static_cast<KeyCode>('A' + (scancode - SDL_SCANCODE_A));
    }

    // 数字键 0-9 (SDL_SCANCODE_0=39 → KeyCode::Num0='0'=48)
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
        return static_cast<KeyCode>('1' + (scancode - SDL_SCANCODE_1));
    }
    if (scancode == SDL_SCANCODE_0) {
        return KeyCode::Num0;
    }

    // 功能键和特殊键（直接映射）
    switch (scancode) {
        case SDL_SCANCODE_ESCAPE: return KeyCode::Escape;
        case SDL_SCANCODE_SPACE: return KeyCode::Space;
        case SDL_SCANCODE_RETURN: return KeyCode::Enter;
        case SDL_SCANCODE_TAB: return KeyCode::Tab;
        case SDL_SCANCODE_BACKSPACE: return KeyCode::Backspace;
        case SDL_SCANCODE_DELETE: return KeyCode::Delete;

        case SDL_SCANCODE_LEFT: return KeyCode::Left;
        case SDL_SCANCODE_RIGHT: return KeyCode::Right;
        case SDL_SCANCODE_UP: return KeyCode::Up;
        case SDL_SCANCODE_DOWN: return KeyCode::Down;

        case SDL_SCANCODE_HOME: return KeyCode::Home;
        case SDL_SCANCODE_END: return KeyCode::End;
        case SDL_SCANCODE_PAGEUP: return KeyCode::PageUp;
        case SDL_SCANCODE_PAGEDOWN: return KeyCode::PageDown;

        case SDL_SCANCODE_LSHIFT: return KeyCode::LeftShift;
        case SDL_SCANCODE_RSHIFT: return KeyCode::RightShift;
        case SDL_SCANCODE_LCTRL: return KeyCode::LeftCtrl;
        case SDL_SCANCODE_RCTRL: return KeyCode::RightCtrl;
        case SDL_SCANCODE_LALT: return KeyCode::LeftAlt;
        case SDL_SCANCODE_RALT: return KeyCode::RightAlt;

        case SDL_SCANCODE_F1: return KeyCode::F1;
        case SDL_SCANCODE_F2: return KeyCode::F2;
        case SDL_SCANCODE_F3: return KeyCode::F3;
        case SDL_SCANCODE_F4: return KeyCode::F4;
        case SDL_SCANCODE_F5: return KeyCode::F5;
        case SDL_SCANCODE_F6: return KeyCode::F6;
        case SDL_SCANCODE_F7: return KeyCode::F7;
        case SDL_SCANCODE_F8: return KeyCode::F8;
        case SDL_SCANCODE_F9: return KeyCode::F9;
        case SDL_SCANCODE_F10: return KeyCode::F10;
        case SDL_SCANCODE_F11: return KeyCode::F11;
        case SDL_SCANCODE_F12: return KeyCode::F12;

        default: return KeyCode::Unknown;
    }
}

// 事件轮询实现（直接返回 SDL_Event）
bool Window::pollEvent(SDL_Event& outEvent) {
    return SDL_PollEvent(&outEvent) != 0;
}

void Window::pumpEvents() {
    SDL_PumpEvents();
}

} // namespace Tina::Engine