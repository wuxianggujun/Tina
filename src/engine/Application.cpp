#include "Application.hpp"
#include "SceneManager.hpp"
#include "OSEventBus.hpp"
#include "TypedEventBus.hpp"
#include "../renderer/Primitive2D.hpp"
#include "Resource.hpp"
#include "Texture.hpp"
#include "Font.hpp"
#include "AudioManager.hpp"
#include "../core/Log.hpp"
#include "../os/OS.hpp"
// 全局 TextRenderer 实现
#include "../ui/TextRenderer.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/timer.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>

namespace Tina::Engine {

Application::Application(const Config& config)
    : m_config(config)
    , m_pixelWidth(config.windowWidth)
    , m_pixelHeight(config.windowHeight)
{
    init();
}

Application::~Application()
{
    shutdown();
}

void Application::init()
{
    TINA_INFO("Application 初始化中...");

    // 1. 创建窗口
    Tina::os::InitWindowArgs args{};
    args.name = m_config.windowTitle;
    args.width = static_cast<uint32_t>(m_config.windowWidth);
    args.height = static_cast<uint32_t>(m_config.windowHeight);

    m_window = Tina::os::createWindow(args);
    if (m_window == Tina::os::INVALID_WINDOW_HANDLE) {
        TINA_ERROR("创建窗口失败");
        return;
    }

    // 2. 获取原生窗口句柄（用于bgfx）
    void* nwh = nullptr;
    {
        SDL_Window* sdl_win = static_cast<SDL_Window*>(m_window);
        SDL_PropertiesID props = SDL_GetWindowProperties(sdl_win);
#ifdef SDL_PROP_WINDOW_WIN32_HWND_POINTER
        nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
        nwh = SDL_GetPointerProperty(props, "SDL.window.win32.hwnd", nullptr);
#endif
    }

    if (!nwh) {
        TINA_ERROR("无法获取原生窗口句柄");
        Tina::os::destroyWindow(m_window);
        m_window = Tina::os::INVALID_WINDOW_HANDLE;
        return;
    }

    // 3. 初始化bgfx
    bgfx::PlatformData pd{};
    pd.nwh = nwh;

    bgfx::Init bgfxInit{};
    bgfxInit.type = bgfx::RendererType::Count; // 自动选择渲染器
    bgfxInit.platformData = pd;

    // 获取实际像素尺寸（考虑DPI缩放）
    SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(m_window), &m_pixelWidth, &m_pixelHeight);

    bgfxInit.resolution.width = static_cast<uint32_t>(m_pixelWidth);
    bgfxInit.resolution.height = static_cast<uint32_t>(m_pixelHeight);
    bgfxInit.resolution.reset = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8;

    if (!bgfx::init(bgfxInit)) {
        TINA_ERROR("bgfx 初始化失败");
        Tina::os::destroyWindow(m_window);
        m_window = Tina::os::INVALID_WINDOW_HANDLE;
        return;
    }

    TINA_INFO("bgfx 初始化成功 - 渲染器: {}", bgfx::getRendererName(bgfx::getRendererType()));

    // 4. 初始化 SDL3_mixer（音频系统）
    if (!MIX_Init()) {
        TINA_WARN("SDL_mixer 初始化失败：{}", SDL_GetError());
    } else {
        TINA_INFO("SDL_mixer 初始化成功");
    }

    // 创建 Mixer 并打开默认播放设备（44.1kHz, 16位, 立体声）
    SDL_AudioSpec desired{}; desired.freq = 44100; desired.channels = 2; desired.format = SDL_AUDIO_S16;
    m_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired);
    if (!m_mixer) {
        TINA_ERROR("SDL_mixer 创建混音器失败：{}", SDL_GetError());
    } else {
        TINA_INFO("SDL_mixer 混音器已创建");
        // 查询实际格式
        SDL_AudioSpec actual{};
        if (MIX_GetMixerFormat(m_mixer, &actual)) {
            TINA_INFO("  采样率: {} Hz, 声道数: {}, 格式: 0x{:X}", actual.freq, actual.channels, static_cast<unsigned int>(actual.format));
        }
        // 将全局 Mixer 提供给音频资源
        AudioResource::SetGlobalMixer(m_mixer);
        // 应用全局音量
        MIX_SetMasterGain(m_mixer, std::max(0.0f, std::min(m_audioMasterVolume, 1.0f)));
        // 应用分组音量（music / sfx）
        MIX_SetTagGain(m_mixer, "music", std::max(0.0f, std::min(m_audioMusicVolume, 1.0f)));
        MIX_SetTagGain(m_mixer, "sfx",   std::max(0.0f, std::min(m_audioSfxVolume, 1.0f)));
    }

    // 5. 设置视图（view 0 = 默认视图，用于清屏）
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(m_pixelWidth), static_cast<uint16_t>(m_pixelHeight));

    // 5. 创建核心子系统
    m_osEventBus = Memory::MakeUnique<OSEventBus>();
    m_typedEvents = Memory::MakeUnique<TypedEventBus>();
    m_sceneManager = Memory::MakeUnique<SceneManager>(this);

    // 6. 创建资源系统
    m_fileSystem = CreateFileSystem();
    m_resourceHub = Memory::MakeUnique<ResourceManagerHub>();
    // 注册内置资源管理器
    {
        // 纹理管理器
        m_textureMgr = Memory::MakeUnique<TextureManager>(*m_fileSystem);
        m_resourceHub->add(Texture2DResource::TYPE, m_textureMgr.get());
        // 字体管理器
        m_fontMgr = Memory::MakeUnique<FontManager>(*m_fileSystem);
        m_resourceHub->add(FontResource::TYPE, m_fontMgr.get());
        // 音频管理器
        m_audioMgr = Memory::MakeUnique<AudioManager>(*m_fileSystem);
        m_resourceHub->add(AudioResource::TYPE, m_audioMgr.get());
        // 也可在此注册其他管理器（例如音频等）
    }

    // 7. 全局着色器管理器（必须在 bgfx 初始化后建立，且在 bgfx 关闭前销毁）
    m_shaderMgr = Memory::MakeUnique<Tina::Renderer::ShaderManager>();
    m_shaderMgr->initialize();

    // 初始化全局 TextRenderer（共享给所有 Scene 使用）
    m_textRenderer = Memory::MakeUnique<Tina::UI::TextRenderer>();
    if (!m_textRenderer->initialize(*m_shaderMgr, *m_resourceHub)) {
        TINA_ERROR("Application: TextRenderer 初始化失败");
    } else {
        // 使用 48 号字体（MenuScene 标题需要）全局 TextRenderer 只能用一个字号
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 48);
        TINA_INFO("Application: TextRenderer 已加载 48 号字体");
    }

    // 7.2 初始化简易 2D 形状渲染器（集中管理 color 程序与布局）
    m_prim2D = Memory::MakeUnique<Tina::Renderer::Primitive2D>();
    if (!m_prim2D->initialize(*m_shaderMgr)) {
        TINA_WARN("Primitive2D 初始化失败：color 程序不可用");
    }

    // 7.5 预热常用资源（字体/图标），减少首帧等待
    prewarmCommonAssets();

    if (!m_fileSystem) {
        TINA_ERROR("文件系统创建失败");
    } else {
        TINA_INFO("资源系统初始化成功");
    }

    // 8. 初始化时间戳
    m_lastFrameTime = bx::getHPCounter();

    TINA_INFO("Application 初始化成功");
}

void Application::shutdown()
{
    TINA_INFO("Application 关闭中...");

    // 清理顺序：场景 → 文本渲染器 → 资源/着色器 → 子系统 → bgfx → 窗口
    m_sceneManager.reset();
    // TextRenderer 持有 bgfx 资源（纹理/Uniform），需在 bgfx::shutdown 前销毁
    m_textRenderer.reset();
    m_resourceHub.reset();
    m_fileSystem.reset();
    m_osEventBus.reset();
    // 在 bgfx 关闭前确保销毁所有程序句柄（先销毁依赖者，再销毁管理器）
    m_prim2D.reset();
    m_shaderMgr.reset();
    m_prim2D.reset();

    // 关闭 SDL_mixer
    if (m_mixer) {
        MIX_DestroyMixer(m_mixer);
        m_mixer = nullptr;
    }
    // 清空全局混音器指针，防止后续析构阶段误用
    AudioResource::SetGlobalMixer(nullptr);
    MIX_Quit();
    TINA_INFO("SDL_mixer 已关闭");

    bgfx::shutdown();

    if (m_window != Tina::os::INVALID_WINDOW_HANDLE) {
        Tina::os::destroyWindow(m_window);
        m_window = Tina::os::INVALID_WINDOW_HANDLE;
    }

    TINA_INFO("Application 已关闭");
}

void Application::run()
{
    if (m_sceneManager->isEmpty()) {
        TINA_ERROR("未设置初始场景。请使用 app.scenes().push(scene) 添加场景");
        return;
    }

    m_running = true;
    TINA_INFO("Application 主循环启动");

    while (m_running && !m_sceneManager->isEmpty()) {
        // 1. 计算帧时间
        uint64_t now = bx::getHPCounter();
        const int64_t frameTime = now - m_lastFrameTime;
        m_lastFrameTime = now;
        const double freq = double(bx::getHPFrequency());
        m_deltaTime = float(double(frameTime) / freq);

        // 限制deltaTime防止超大跳帧（例如调试器断点）
        if (m_deltaTime > 0.1f) {
            m_deltaTime = 0.1f;
        }

        // 计算FPS
        m_fps = (m_deltaTime > 0.0f) ? (1.0f / m_deltaTime) : 60.0f;

        // 2. 处理事件
        processEvents();

        // 3. 更新逻辑
        update(m_deltaTime);

        // 4. 渲染
        render();

        // 4.5 执行主线程任务队列（在本帧安全点）
        flushTasks();

        // 5. 提交帧
        bgfx::frame();
    }

    TINA_INFO("Application 主循环结束");
}

void Application::processEvents()
{
    Tina::os::Event event;
    while (Tina::os::getEvent(event)) {
        // 1. 处理全局事件
        if (event.type == Tina::os::Event::Type::QUIT ||
            event.type == Tina::os::Event::Type::WINDOW_CLOSE) {
            TINA_INFO("接收到退出事件");
            quit();
            continue;
        }

        if (event.type == Tina::os::Event::Type::WINDOW_SIZE) {
            // 更新窗口尺寸
            m_config.windowWidth = event.win_size.w;
            m_config.windowHeight = event.win_size.h;

            // 获取实际像素尺寸
            SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(m_window), &m_pixelWidth, &m_pixelHeight);

            // 重置bgfx
            bgfx::reset(
                static_cast<uint32_t>(m_pixelWidth),
                static_cast<uint32_t>(m_pixelHeight),
                BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8
            );

            // 更新视图矩形
            bgfx::setViewRect(0, 0, 0,
                static_cast<uint16_t>(m_pixelWidth),
                static_cast<uint16_t>(m_pixelHeight)
            );

            TINA_INFO("窗口调整: {}x{} (像素: {}x{})",
                m_config.windowWidth, m_config.windowHeight,
                m_pixelWidth, m_pixelHeight);
        }

        // 2. 分发到 OS 事件总线
        m_osEventBus->dispatchOSEvent(event);

        // 3. 分发到当前场景
        m_sceneManager->handleEvent(event);
    }
    // 事件分发完成后，立即执行一次主线程任务队列，减少响应延迟
    flushTasks();
}

void Application::setAudioMasterVolume(float v)
{
    m_audioMasterVolume = std::max(0.0f, std::min(v, 1.0f));
    if (m_mixer) {
        MIX_SetMasterGain(m_mixer, m_audioMasterVolume);
    }
}

void Application::setMusicVolume(float v)
{
    m_audioMusicVolume = std::max(0.0f, std::min(v, 1.0f));
    if (m_mixer) {
        MIX_SetTagGain(m_mixer, "music", m_audioMusicVolume);
    }
}

void Application::setSfxVolume(float v)
{
    m_audioSfxVolume = std::max(0.0f, std::min(v, 1.0f));
    if (m_mixer) {
        MIX_SetTagGain(m_mixer, "sfx", m_audioSfxVolume);
    }
}

void Application::update(float dt)
{
    // 1. 驱动资源系统（处理异步加载回调）
    if (m_resourceHub) {
        m_resourceHub->update();
    }

    // 2. 驱动强类型事件总线（派发异步事件）
    if (m_typedEvents) m_typedEvents->update();

    // 3. 更新场景
    m_sceneManager->update(dt);
}

void Application::render()
{
    m_sceneManager->render();
}

void Application::post(std::function<void()> fn)
{
    if (!fn) return;
    std::lock_guard<std::mutex> _g(m_taskMutex);
    m_tasks.push_back(std::move(fn));
}

void Application::flushTasks()
{
    Tina::Container::Vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> _g(m_taskMutex);
        if (m_tasks.empty()) return;
        local.swap(m_tasks);
    }
    for (auto& fn : local) {
        if (fn) fn();
    }
    // 同步检查并执行到期的延时任务
    flushTimedTasks();
}

void Application::postDelayed(uint32_t delayMs, std::function<void()> fn)
{
    if (!fn) return;
    uint64_t nowMs = SDL_GetTicks();
    TimedTask t{}; t.dueMs = nowMs + delayMs; t.fn = std::move(fn);
    std::lock_guard<std::mutex> _g(m_taskMutex);
    m_timedTasks.push_back(std::move(t));
}

void Application::flushTimedTasks()
{
    uint64_t nowMs = SDL_GetTicks();
    Tina::Container::Vector<std::function<void()>> toRun;
    {
        std::lock_guard<std::mutex> _g(m_taskMutex);
        if (m_timedTasks.empty()) return;
        // 线性扫描收集到期任务；数量不大时足够
        auto it = m_timedTasks.begin();
        while (it != m_timedTasks.end()) {
            if (it->dueMs <= nowMs) {
                toRun.push_back(std::move(it->fn));
                it = m_timedTasks.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& fn : toRun) if (fn) fn();
}

void Application::prewarmCommonAssets()
{
    if (!m_resourceHub) return;

    // 预热字体与常用图标纹理
    // 字体：确保 24/32/48 三个字号的 Face
    Tina::Engine::FontResource* font = m_resourceHub->load<Tina::Engine::FontResource>(Tina::Engine::Path("resources/fonts/SourceHanSansSC-Regular.otf"));
    Tina::Engine::Texture2DResource* texA = m_resourceHub->load<Tina::Engine::Texture2DResource>(Tina::Engine::Path("resources/textures/player.png"));
    Tina::Engine::Texture2DResource* texB = m_resourceHub->load<Tina::Engine::Texture2DResource>(Tina::Engine::Path("resources/textures/grassland.png"));
    Tina::Engine::Texture2DResource* texC = m_resourceHub->load<Tina::Engine::Texture2DResource>(Tina::Engine::Path("resources/textures/dirt_block.png"));

    const uint32_t timeoutMs = 250; // 总等待预算（毫秒）
    uint32_t waited = 0;
    while (waited < timeoutMs) {
        m_resourceHub->update(); // 驱动异步回调
        bool fontReady = !font || font->getState() == Tina::Engine::Resource::State::READY;
        bool texAReady = !texA || texA->getState() == Tina::Engine::Resource::State::READY;
        bool texBReady = !texB || texB->getState() == Tina::Engine::Resource::State::READY;
        bool texCReady = !texC || texC->getState() == Tina::Engine::Resource::State::READY;
        if (fontReady && texAReady && texBReady && texCReady) break;
        SDL_Delay(1);
        waited += 1;
    }

    if (font && font->getState() == Tina::Engine::Resource::State::READY) {
        // 预热多个常用字号，减少首次切换等待
        int sizes[] = {24, 32, 48};
        for (int s : sizes) {
            if (font->ensureFace(s)) {
                TINA_INFO("Application: 字体 Face {} 号已预热", s);
            }
        }
    }
}

UI::TextRenderer& Application::textRenderer() const
{
    return *m_textRenderer;
}

Tina::Renderer::Primitive2D& Application::primitives2D() const
{
    return *m_prim2D;
}

} // namespace Tina::Engine
