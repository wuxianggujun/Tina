// 使用 SDL3 创建窗口，并将原生窗口句柄交由 bgfx 使用，完成最小渲染循环。

#include <cstdint>
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

// 将窗口尺寸更新到 bgfx
static void ResetBgfxWithWindow(SDL_Window* window, uint32_t resetFlags)
{
    int w = 0, h = 0;
    // SDL3 高 DPI 情况下应获取像素尺寸
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) {
        // 兜底，避免无效尺寸
        w = 1280; h = 720;
    }
    bgfx::reset((uint32_t)w, (uint32_t)h, resetFlags);
    bgfx::setViewRect(0, 0, 0, (uint16_t)w, (uint16_t)h);
}

int main(int /*argc*/, char* /*argv*/[])
{
    // 1) 初始化 SDL（仅视频与事件）
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init 失败: %s", SDL_GetError());
        return -1;
    }

    // 2) 创建 SDL 窗口
    const int winW = 1280;
    const int winH = 720;
    const SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Window* window = SDL_CreateWindow("Tina (SDL3 + bgfx)", winW, winH, flags);
    if (!window) {
        SDL_Log("SDL_CreateWindow 失败: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    // 3) 通过 SDL3 窗口属性获取原生窗口句柄，并注入 bgfx 平台数据
    void* nwh = nullptr; // native window handle
    {
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
#if defined(_WIN32)
        // SDL3 提供属性键 SDL_PROP_WINDOW_WIN32_HWND_POINTER，若头文件宏不可用则使用等价字面串
#ifdef SDL_PROP_WINDOW_WIN32_HWND_POINTER
        nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
        nwh = SDL_GetPointerProperty(props, "SDL.window.win32.hwnd", nullptr);
#endif
#elif defined(__APPLE__)
        // macOS 可使用 Cocoa/Metal 句柄，后续扩展：
        // nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(__linux__)
        // Linux 平台可按实际后端（X11/Wayland）选择：
        // Wayland: SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER
        // X11: SDL_PROP_WINDOW_X11_WINDOW_NUMBER（注意是整数）
#endif
    }

    if (nwh == nullptr) {
        SDL_Log("获取原生窗口句柄失败（nwh=null）。当前视频后端: %s", SDL_GetCurrentVideoDriver());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    bgfx::PlatformData pd{};
    pd.nwh = nwh; // Windows: HWND；其他平台后续补齐对应句柄
    // 可选：同时写入 init.platformData，兼容不同版本用法

    // 4) 初始化 bgfx
    bgfx::Init init{};
#if defined(_WIN32)
    // 在部分环境下自动选择可能挑到 D3D12 而初始化失败，强制 D3D11 兼容性更好
    init.type = bgfx::RendererType::Direct3D11;
#else
    init.type = bgfx::RendererType::Count; // 自动选择最优后端
#endif
    init.platformData = pd;
    // 初始分辨率将由 reset 时更新
    int pxW = winW, pxH = winH;
    SDL_GetWindowSizeInPixels(window, &pxW, &pxH);
    init.resolution.width = (uint32_t)pxW;
    init.resolution.height = (uint32_t)pxH;
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        SDL_Log("bgfx::init 失败。后端=%d 像素尺寸=%dx%d nwh=%p", (int)init.type, (int)init.resolution.width, (int)init.resolution.height, nwh);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // 设置清屏（颜色/深度/模板）
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    ResetBgfxWithWindow(window, init.resolution.reset);

    // 5) 主循环：事件处理 + 渲染
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                ResetBgfxWithWindow(window, init.resolution.reset);
                break;
            default:
                break;
            }
        }

        // 触摸视图保证提交，执行清屏
        bgfx::touch(0);
        bgfx::frame();
    }

    // 6) 资源释放
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
