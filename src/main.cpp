// 使用统一 os 接口（SDL3 实现）创建窗口，事件循环打印日志，bgfx 完成最小渲染循环。

#include <cstdint>
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include "core/Log.hpp"
#include "os/OS.hpp"

using Tina::os::Event;

// 将窗口尺寸更新到 bgfx
static void ResetBgfxWithSize(int w, int h, uint32_t resetFlags)
{
    if (w <= 0 || h <= 0) { w = 1280; h = 720; }
    bgfx::reset((uint32_t)w, (uint32_t)h, resetFlags);
    bgfx::setViewRect(0, 0, 0, (uint16_t)w, (uint16_t)h);
}

// 打印事件的简单帮助函数（用于测试事件系统）
static void LogEvent(const Event& e)
{
    switch (e.type) {
        case Event::Type::QUIT: TINA_INFO("事件: QUIT"); break;
        case Event::Type::WINDOW_CLOSE: TINA_INFO("事件: WINDOW_CLOSE"); break;
        case Event::Type::WINDOW_MOVE: TINA_INFO("事件: WINDOW_MOVE x={}, y={}", e.win_move.x, e.win_move.y); break;
        case Event::Type::WINDOW_SIZE: TINA_INFO("事件: WINDOW_SIZE w={}, h={}", e.win_size.w, e.win_size.h); break;
        case Event::Type::FOCUS: TINA_INFO("事件: FOCUS gained={}", e.focus.gained); break;
        case Event::Type::KEY: TINA_INFO("事件: KEY down={}, code={}, repeat={}", e.key.down, (int)e.key.key_code, e.key.is_repeat); break;
        case Event::Type::CHAR: TINA_INFO("事件: CHAR utf8_first_byte=0x{:02x}", e.text_input.utf8 & 0xff); break;
        case Event::Type::MOUSE_BUTTON: TINA_INFO("事件: MOUSE_BUTTON down={}, button={}", e.mouse_button.down, (int)e.mouse_button.button); break;
        case Event::Type::MOUSE_MOVE: TINA_INFO("事件: MOUSE_MOVE dx={}, dy={}", e.mouse_move.xrel, e.mouse_move.yrel); break;
        case Event::Type::MOUSE_WHEEL: TINA_INFO("事件: MOUSE_WHEEL amount={}", e.mouse_wheel.amount); break;
        case Event::Type::DROP_FILE: TINA_INFO("事件: DROP_FILE handle={}", e.file_drop.handle); break;
        default: break;
    }
}

int main(int /*argc*/, char* /*argv*/[])
{
    Tina::Core::Log::Init("Tina", spdlog::level::info);
    TINA_INFO("启动 Tina，使用 SDL3 后端的 os 事件系统");

    // 1) 通过 os 接口创建窗口（底层 SDL3）
    const int winW = 1280;
    const int winH = 720;
    Tina::os::InitWindowArgs args{};
    args.width = winW; args.height = winH; args.name = "Tina (os + SDL3 + bgfx)";
    Tina::os::WindowHandle window = Tina::os::createWindow(args);
    if (window == Tina::os::INVALID_WINDOW_HANDLE) {
        TINA_ERROR("创建窗口失败");
        return -1;
    }

    // 2) 获取原生窗口句柄以初始化 bgfx（从 SDL_Window 属性提取）
    void* nwh = nullptr; // native window handle
    {
        SDL_Window* sdl_win = (SDL_Window*)window;
        SDL_PropertiesID props = SDL_GetWindowProperties(sdl_win);
#if defined(_WIN32)
#ifdef SDL_PROP_WINDOW_WIN32_HWND_POINTER
        nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
        nwh = SDL_GetPointerProperty(props, "SDL.window.win32.hwnd", nullptr);
#endif
#elif defined(__APPLE__)
        // TODO: Cocoa/Metal 句柄
#elif defined(__linux__)
        // TODO: Wayland/X11 句柄
#endif
    }
    if (nwh == nullptr) {
        TINA_ERROR("获取原生窗口句柄失败。当前视频后端: {}", SDL_GetCurrentVideoDriver());
        Tina::os::destroyWindow(window);
        return -1;
    }
    TINA_INFO("窗口创建完成，原生句柄 nwh={}", (void*)nwh);

    // 3) 初始化 bgfx
    bgfx::PlatformData pd{}; pd.nwh = nwh;
    bgfx::Init init{};
#if defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11; // 避免自动挑到 D3D12 导致失败
#else
    init.type = bgfx::RendererType::Count; // 自动选择
#endif
    init.platformData = pd;
    int pxW = winW, pxH = winH;
    SDL_GetWindowSizeInPixels((SDL_Window*)window, &pxW, &pxH);
    init.resolution.width = (uint32_t)pxW;
    init.resolution.height = (uint32_t)pxH;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        TINA_ERROR("bgfx 初始化失败: 后端={} 像素尺寸={}x{} nwh={} ", (int)init.type, (int)init.resolution.width, (int)init.resolution.height, nwh);
        Tina::os::destroyWindow(window);
        return -1;
    }
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    ResetBgfxWithSize(pxW, pxH, init.resolution.reset);

    // 4) 事件循环（使用 os::getEvent），打印日志并在尺寸变化时 reset bgfx
    bool running = true;
    while (running) {
        Event ev;
        while (Tina::os::getEvent(ev)) {
            LogEvent(ev);
            switch (ev.type) {
                case Event::Type::QUIT:
                case Event::Type::WINDOW_CLOSE:
                    running = false; break;
                case Event::Type::WINDOW_SIZE:
                    ResetBgfxWithSize(ev.win_size.w, ev.win_size.h, init.resolution.reset);
                    break;
                default: break;
            }
        }

        bgfx::touch(0);
        bgfx::frame();
    }

    // 5) 清理
    bgfx::shutdown();
    Tina::os::destroyWindow(window);
    TINA_INFO("退出 Tina");
    return 0;
}
