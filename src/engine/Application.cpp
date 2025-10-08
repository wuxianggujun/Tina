#include "Application.hpp"
#include "SceneManager.hpp"
#include "EventBus.hpp"
#include "Resource.hpp"
#include "Texture.hpp"
#include "Font.hpp"
#include "../core/Log.hpp"
#include "../os/OS.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/timer.h>
#include <SDL3/SDL.h>

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

    // 4. 设置视图（view 0 = 默认视图，用于清屏）
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(m_pixelWidth), static_cast<uint16_t>(m_pixelHeight));

    // 5. 创建核心子系统
    m_eventBus = Memory::MakeUnique<EventBus>();
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
        // 也可在此注册其他管理器（例如音频等）
    }

    // 7. 全局着色器管理器（必须在 bgfx 初始化后建立，且在 bgfx 关闭前销毁）
    m_shaderMgr = Memory::MakeUnique<Tina::Renderer::ShaderManager>();
    m_shaderMgr->initialize();

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

    // 清理顺序：场景 → 资源 → 着色器 → 子系统 → bgfx → 窗口
    m_sceneManager.reset();
    m_resourceHub.reset();
    m_fileSystem.reset();
    m_eventBus.reset();
    // 在 bgfx 关闭前确保销毁所有程序句柄
    m_shaderMgr.reset();

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

        // 2. 分发到EventBus
        m_eventBus->dispatchOSEvent(event);

        // 3. 分发到当前场景
        m_sceneManager->handleEvent(event);
    }
}

void Application::update(float dt)
{
    // 1. 驱动资源系统（处理异步加载回调）
    if (m_resourceHub) {
        m_resourceHub->update();
    }

    // 2. 更新场景
    m_sceneManager->update(dt);
}

void Application::render()
{
    m_sceneManager->render();
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
        // 确保常用字号
        font->ensureFace(24);
        font->ensureFace(32);
        font->ensureFace(48);
    }
}

} // namespace Tina::Engine
