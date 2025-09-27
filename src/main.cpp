// 使用统一 os 接口（SDL3 实现）创建窗口，事件循环打印日志，bgfx 完成最小渲染循环。

#include <cstdint>
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <thread>
#include <chrono>
#include "core/Log.hpp"
#include "os/OS.hpp"
#include "engine/Resource.hpp"
#include "core/Path.hpp"
#include "core/Time.hpp"

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
    Tina::Core::Log::Init("Tina", Tina::Core::Log::Level::Info);
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
    init.type = bgfx::RendererType::Count; // 自动选择
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

    // 3.5) 初始化资源系统（异步文件系统 + 资源 Hub/Manager）
    using namespace Tina::Engine;
    auto fs = CreateFileSystem();
    BlobManager blob_rm(*fs);
    ResourceManagerHub hub; hub.add(BlobResource::TYPE, &blob_rm);
    // 示例：异步读取一个配置文件（展示资源 READY）
    Resource* cfg_res = hub.load(BlobResource::TYPE, Tina::Core::Path(Tina::Core::string_view{"resources/config/settings.yaml", sizeof("resources/config/settings.yaml") - 1}));

    // 3.6) 帧计时与固定步（基于 docs/frame_timing.md）
    Tina::Core::TimeConfig time_cfg{};
    time_cfg.tick_rate = 60;
    time_cfg.max_substeps = 4;
    time_cfg.dt_min = 1.0 / 120.0;
    time_cfg.dt_max = 1.0 / 20.0;
    time_cfg.frame_cap_hz = 0; // 0 关闭限帧，依赖 VSync；可改为 60 等数值
    Tina::Core::FrameTimer frame_timer; frame_timer.reset();
    Tina::Core::FixedStepTicker ticker(time_cfg);
    const bool vsync_on = (init.resolution.reset & BGFX_RESET_VSYNC) != 0;
    double fps_log_t = 0.0; // 上次 FPS 打印的时间戳（秒）

    // 4) 事件循环（使用 os::getEvent），固定逻辑步 + 可变渲染，并驱动资源系统 update()
    bool running = true;
    bool fullscreen = false;
    Tina::os::WindowState prev_state{};
    bool relative_mouse = false;
    while (running) {
        frame_timer.beginFrame();
        double dt = frame_timer.deltaSeconds();
        if (dt < time_cfg.dt_min) dt = time_cfg.dt_min;
        if (dt > time_cfg.dt_max) dt = time_cfg.dt_max;
        ticker.accumulate(dt);

        // 驱动资源系统回调
        hub.update();
        if (cfg_res && cfg_res->getState() == Resource::State::READY) {
            TINA_INFO("配置资源加载完成: {} ({} bytes)",
                      cfg_res->getPath().c_str(),
                      (int)static_cast<BlobResource*>(cfg_res)->data.size());
            cfg_res = nullptr; // 仅打印一次
        }
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
                case Event::Type::KEY:
                    if (ev.key.down) {
                        if (ev.key.key_code == Tina::os::KeyCode::F11) {
                            if (!fullscreen) { prev_state = Tina::os::setFullScreen(window); fullscreen = true; TINA_INFO("切换全屏"); }
                            else { Tina::os::restoreWindow(window, prev_state); fullscreen = false; TINA_INFO("退出全屏"); }
                        } else if (ev.key.key_code == Tina::os::KeyCode::R) {
                            relative_mouse = !relative_mouse;
                            Tina::os::setRelativeMouseMode(window, relative_mouse);
                            TINA_INFO("相对鼠标模式: {}", relative_mouse);
                        } else if (ev.key.key_code == Tina::os::KeyCode::ESCAPE) {
                            running = false;
                        }
                    }
                    break;
                case Event::Type::DROP_FILE: {
                    int n = Tina::os::getDropFileCount(ev.file_drop.handle);
                    TINA_INFO("拖拽文件数量: {}", n);
                    for (int i = 0; i < n; ++i) {
                        const char* path = Tina::os::getDropFile(ev.file_drop.handle, i);
                        TINA_INFO("  文件[{}]: {}", i, path ? path : "<null>");
                    }
                    Tina::os::finishDrag(ev.file_drop.handle);
                } break;
                default: break;
            }
        }

        // 固定逻辑步（这里示例为空，可在此调用物理/规则模拟）
        ticker.step([&](double fixed_dt){ (void)fixed_dt; /* TODO: 逻辑更新 */ });

        // 渲染（可使用 alpha 做插值渲染）
        const double alpha = ticker.alpha(); (void)alpha;

        bgfx::touch(0);
        bgfx::frame();

        // 每秒打印一次帧率/帧时间与目标设定，便于核对
        const double now_sec = frame_timer.sinceStartupSeconds();
        if (now_sec - fps_log_t >= 1.0) {
            TINA_INFO("FPS: {:.1f} | frame: {:.2f} ms | VSync: {} | Cap: {} Hz | Fixed: {} Hz",
                      frame_timer.fps(),
                      frame_timer.frameSeconds() * 1000.0,
                      vsync_on ? "on" : "off",
                      time_cfg.frame_cap_hz,
                      time_cfg.tick_rate);
            fps_log_t = now_sec;
        }

        // 可选限帧（关闭 VSync 时生效更明显）
        if (time_cfg.frame_cap_hz > 0) {
            const double target = 1.0 / double(time_cfg.frame_cap_hz);
            const double used = frame_timer.frameSeconds();
            if (used < target) {
                const double remain = target - used;
                const auto ns = (long long)(remain * 1e9);
                if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
            }
        }
    }

    // 5) 清理
    bgfx::shutdown();
    Tina::os::destroyWindow(window);
    TINA_INFO("退出 Tina");
    return 0;
}
