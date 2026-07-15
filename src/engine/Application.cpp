#include "Application.hpp"
#include "IApplication.hpp"
#include "SceneManager.hpp"
#include "InputSystem.hpp"
#include "EngineEvents.hpp"
#include "EventSystem.hpp"
#include "Window.hpp"
#include "../renderer/Primitive2D.hpp"
#include "../renderer/SpriteRenderer.hpp"
#include "Resource.hpp"
#include "Texture.hpp"
#include "Font.hpp"
#include "AudioManager.hpp"
#include "AudioEngine.hpp"
#include "AudioResource.hpp"
#include "../core/Log.hpp"
#include "../ui/TextRenderer.hpp"
#include "../ui/UIConstants.hpp"  // ✅ VIEW_CLEAR等常量定义

#include <bgfx/bgfx.h>
#include <bx/timer.h>
#include <algorithm>
#include <exception>

namespace Tina::Engine {
namespace {

uint32_t makeResetFlags(const Application::Config& config)
{
    uint32_t flags = config.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    if (config.msaa >= 16) {
        flags |= BGFX_RESET_MSAA_X16;
    } else if (config.msaa >= 8) {
        flags |= BGFX_RESET_MSAA_X8;
    } else if (config.msaa >= 4) {
        flags |= BGFX_RESET_MSAA_X4;
    } else if (config.msaa >= 2) {
        flags |= BGFX_RESET_MSAA_X2;
    }
    return flags;
}

} // namespace

// 定义静态单例指针
Application* Application::s_instance = nullptr;

Application::Application(IApplication* app, const Config& config)
    : m_app(app)
    , m_config(config)
    , m_pixelWidth(config.windowWidth)
    , m_pixelHeight(config.windowHeight)
{
    if (s_instance) {
        TINA_ERROR("同一进程中只能存在一个 Application 实例");
        return;
    }

    s_instance = this;
    try {
        if (!init()) {
            TINA_ERROR("Application 初始化失败，开始回滚已创建的子系统");
            shutdown();
        }
    } catch (...) {
        TINA_ERROR("Application 初始化期间发生异常，开始回滚已创建的子系统");
        shutdown();
        s_instance = nullptr;
        throw;
    }
}

Application::~Application()
{
    shutdown();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool Application::init()
{
    TINA_INFO("Application 初始化中...");

    // 1. 创建窗口
    WindowDesc desc{};
    desc.title = m_config.windowTitle;
    desc.width = m_config.windowWidth;
    desc.height = m_config.windowHeight;
    desc.resizable = true;

    m_window = Memory::MakeUnique<Window>();
    if (!m_window->create(desc)) {
        TINA_ERROR("创建窗口失败");
        return false;
    }

    // 2. 获取原生窗口句柄
    void* nwh = m_window->getNativeHandle();
    if (!nwh) {
        TINA_ERROR("无法获取原生窗口句柄");
        return false;
    }

    // 3. 初始化bgfx
    bgfx::PlatformData pd{};
    pd.nwh = nwh;
    pd.ndt = m_window->getNativeDisplayHandle();
    if (m_window->usesWayland()) {
        pd.type = bgfx::NativeWindowHandleType::Wayland;
    }

    bgfx::Init bgfxInit{};
    bgfxInit.type = bgfx::RendererType::Count;
    bgfxInit.platformData = pd;

    m_window->getSizeInPixels(m_pixelWidth, m_pixelHeight);

    bgfxInit.resolution.width = static_cast<uint32_t>(m_pixelWidth);
    bgfxInit.resolution.height = static_cast<uint32_t>(m_pixelHeight);
    bgfxInit.resolution.reset = makeResetFlags(m_config);

    if (!bgfx::init(bgfxInit)) {
        TINA_ERROR("bgfx 初始化失败");
        return false;
    }
    m_bgfxInitialized = true;

    TINA_INFO("bgfx 初始化成功 - 渲染器: {}", bgfx::getRendererName(bgfx::getRendererType()));

    // 4. 初始化 miniaudio。音频失败不会阻止无声模式下继续运行。
    m_audioEngine = Memory::MakeUnique<AudioEngine>();
    if (!m_audioEngine->initialize()) {
        TINA_WARN("miniaudio 初始化失败，应用将以无声模式运行");
        m_audioEngine.reset();
    } else {
        m_audioEngine->setMasterVolume(m_audioMasterVolume);
        m_audioEngine->setMusicVolume(m_audioMusicVolume);
        m_audioEngine->setSfxVolume(m_audioSfxVolume);
    }

    // 5. 设置默认视图
    bgfx::setViewClear(UI::VIEW_CLEAR, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    bgfx::setViewRect(UI::VIEW_CLEAR, 0, 0, static_cast<uint16_t>(m_pixelWidth), static_cast<uint16_t>(m_pixelHeight));

    // 6. 创建核心子系统
    m_eventSystem = Memory::MakeUnique<EventSystem>();
    if (!m_eventSystem->initialize()) {
        TINA_ERROR("EventSystem 初始化失败");
        return false;
    }
    m_inputSystem = Memory::MakeUnique<InputSystem>();
    m_inputSystem->setEventSystem(m_eventSystem.get());
    m_inputSystem->setWindow(m_window.get());
    if (!m_inputSystem->initialize()) {
        TINA_ERROR("InputSystem 初始化失败");
        return false;
    }
    m_sceneManager = Memory::MakeUnique<SceneManager>(this);

    // 7. 创建资源系统
    m_fileSystem = CreateFileSystem();
    if (!m_fileSystem) {
        TINA_ERROR("文件系统创建失败");
        return false;
    }
    m_resourceHub = Memory::MakeUnique<ResourceManagerHub>(*m_fileSystem);
    {
        m_textureMgr = Memory::MakeUnique<TextureManager>(*m_fileSystem);
        m_resourceHub->add(Texture2DResource::TYPE, m_textureMgr.get());
        
        m_fontMgr = Memory::MakeUnique<FontManager>(*m_fileSystem);
        m_resourceHub->add(FontResource::TYPE, m_fontMgr.get());
        
        m_audioMgr = Memory::MakeUnique<AudioManager>(*m_fileSystem);
        m_resourceHub->add(AudioResource::TYPE, m_audioMgr.get());
    }

    // 8. 全局着色器管理器
    m_shaderMgr = Memory::MakeUnique<Tina::Renderer::ShaderManager>();
    m_shaderMgr->initialize();

    // 9. 初始化全局渲染器
    m_textRenderer = Memory::MakeUnique<Tina::UI::TextRenderer>();
    if (!m_textRenderer->initialize(*m_shaderMgr, *m_resourceHub)) {
        TINA_ERROR("Application: TextRenderer 初始化失败");
        return false;
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 48);
        TINA_INFO("Application: TextRenderer 已加载 48 号字体");
    }

    m_prim2D = Memory::MakeUnique<Tina::Renderer::Primitive2D>();
    if (!m_prim2D->initialize(*m_shaderMgr)) {
        TINA_ERROR("Primitive2D 初始化失败");
        return false;
    }

    m_sprite2D = Memory::MakeUnique<Tina::Renderer::SpriteRenderer>();
    if (!m_sprite2D->initialize(*m_shaderMgr)) {
        TINA_ERROR("SpriteRenderer 初始化失败");
        return false;
    }

    prewarmCommonAssets();

    TINA_INFO("资源系统初始化成功");

    // 10. 初始化时间戳
    m_lastFrameTime = bx::getHPCounter();

    TINA_INFO("Application 初始化成功");
    m_initialized = true;
    
    // 11. 调用用户应用初始化钩子
    if (m_app) {
        m_app->onSetup(*this);
        m_setupCompleted = true;
    }

    return true;
}

void Application::shutdown()
{
    if (m_shutdownCompleted) {
        return;
    }
    m_shutdownCompleted = true;
    m_running = false;

    TINA_INFO("Application 关闭中...");

    // 1. 调用用户应用清理钩子
    if (m_app && m_setupCompleted) {
        try {
            m_app->onCleanup(*this);
        } catch (const std::exception& error) {
            TINA_ERROR("IApplication::onCleanup 异常: {}", error.what());
        } catch (...) {
            TINA_ERROR("IApplication::onCleanup 发生未知异常");
        }
        m_setupCompleted = false;
    }

    // 2. 先销毁所有依赖 bgfx、资源与窗口的上层对象。
    m_sceneManager.reset();
    discardPendingTasks();
    m_textRenderer.reset();
    m_prim2D.reset();
    m_sprite2D.reset();

    // ResourceManager 持有具体资源；必须先于 FileSystem、AudioEngine 和 bgfx 销毁。
    m_resourceHub.reset();
    m_audioMgr.reset();
    m_fontMgr.reset();
    m_textureMgr.reset();
    m_audioEngine.reset();
    m_shaderMgr.reset();
    m_fileSystem.reset();

    m_inputSystem.reset();
    if (m_eventSystem) {
        m_eventSystem->shutdown();
    }
    m_eventSystem.reset();

    if (m_bgfxInitialized) {
        bgfx::shutdown();
        m_bgfxInitialized = false;
    }

    m_window.reset();
    m_initialized = false;

    TINA_INFO("Application 关闭完成");
}

void Application::run()
{
    if (!m_initialized || !m_window || !m_sceneManager || !m_inputSystem ||
        !m_eventSystem || !m_resourceHub || !m_bgfxInitialized) {
        TINA_ERROR("Application 未完整初始化，拒绝启动主循环");
        return;
    }

    if (m_sceneManager->isEmpty()) {
        TINA_ERROR("未设置初始场景");
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

        if (m_deltaTime > 0.1f) {
            m_deltaTime = 0.1f;
        }

        m_fps = (m_deltaTime > 0.0f) ? (1.0f / m_deltaTime) : 60.0f;

        // 2. 输入系统帧开始
        if (m_inputSystem) m_inputSystem->beginFrame();

        // 3. 处理事件
        processEvents();

        if (!m_running) {
            break;
        }

        // 4. 按优先级分发排队/延迟事件。输入快照与 UI routed event 保持独立。
        if (m_eventSystem) {
            m_eventSystem->update();
        }

        // 5. 每帧仅在这里泵送一次异步资源 completion，并受上传预算约束。
        if (m_resourceHub) {
            m_resourceHub->update();
        }

        // 6. 调用用户应用事件处理钩子
        if (m_app) {
            m_app->onEvent(*this);
        }

        // 7. 调用用户应用更新钩子
        if (m_app) {
            m_app->onUpdate(*this, m_deltaTime);
        }

        // 8. 更新场景
        update(m_deltaTime);

        // 9. 渲染场景
        render();

        // 10. 调用用户应用渲染钩子
        if (m_app) {
            m_app->onRender(*this);
        }

        // 11. 输入系统帧结束
        if (m_inputSystem) m_inputSystem->endFrame();

        // 12. 执行任务队列
        flushTasks();

        // 13. 提交帧
        bgfx::frame();

        ++m_frameIndex;
        if (m_config.maxFrames > 0 && m_frameIndex >= m_config.maxFrames) {
            TINA_INFO("已完成配置的 {} 帧，正常结束冒烟运行", m_config.maxFrames);
            quit();
        }
    }

    TINA_INFO("Application 主循环结束");
}

void Application::processEvents()
{
    m_window->pollEvents();

    if (m_window->shouldClose()) {
        TINA_INFO("接收到窗口关闭请求");
        quit();
        flushTasks();
        return;
    }

    int logicalWidth = 0;
    int logicalHeight = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    m_window->getSize(logicalWidth, logicalHeight);
    m_window->getFramebufferSize(pixelWidth, pixelHeight);

    const bool framebufferChanged = pixelWidth > 0 && pixelHeight > 0
        && (pixelWidth != m_pixelWidth || pixelHeight != m_pixelHeight);
    if (framebufferChanged) {
        m_config.windowWidth = logicalWidth;
        m_config.windowHeight = logicalHeight;
        m_pixelWidth = pixelWidth;
        m_pixelHeight = pixelHeight;

        TINA_DEBUG("Application - 调用 bgfx::reset({}x{})", m_pixelWidth, m_pixelHeight);
        bgfx::reset(
            static_cast<uint32_t>(m_pixelWidth),
            static_cast<uint32_t>(m_pixelHeight),
            makeResetFlags(m_config)
        );

        TINA_INFO("窗口调整: {}x{} (像素: {}x{})",
            m_config.windowWidth, m_config.windowHeight,
            m_pixelWidth, m_pixelHeight);

        if (m_eventSystem) {
            m_eventSystem->trigger(Events::WindowResizedEvent(m_pixelWidth, m_pixelHeight));
        }
    }
    flushTasks();
}

void Application::update(float dt)
{
    if (m_sceneManager) {
        m_sceneManager->update(dt);
    }
}

void Application::render()
{
    if (m_sceneManager) {
        m_sceneManager->render();
    }
}

// 以下是所有getter和setter方法的实现...
Tina::UI::TextRenderer& Application::textRenderer() const {
    return *m_textRenderer;
}

Tina::Renderer::Primitive2D& Application::primitives2D() const {
    return *m_prim2D;
}

Tina::Renderer::SpriteRenderer& Application::sprites2D() const {
    return *m_sprite2D;
}

void Application::setAudioMasterVolume(float v) {
    m_audioMasterVolume = std::max(0.0f, std::min(v, 1.0f));
    if (m_audioEngine) {
        m_audioEngine->setMasterVolume(m_audioMasterVolume);
    }
}

void Application::setMusicVolume(float v) {
    m_audioMusicVolume = std::max(0.0f, std::min(v, 1.0f));
    if (m_audioEngine) {
        m_audioEngine->setMusicVolume(m_audioMusicVolume);
    }
}

void Application::setSfxVolume(float v) {
    m_audioSfxVolume = std::max(0.0f, std::min(v, 1.0f));
    if (m_audioEngine) {
        m_audioEngine->setSfxVolume(m_audioSfxVolume);
    }
}

void Application::post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(m_taskMutex);
    m_tasks.push_back(std::move(fn));
}

void Application::postDelayed(uint32_t delayMs, std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(m_taskMutex);
    uint64_t dueTime = bx::getHPCounter() + (delayMs * bx::getHPFrequency() / 1000);
    m_timedTasks.push_back({dueTime, std::move(fn)});
}

void Application::flushTasks() {
    Tina::Container::Vector<std::function<void()>> localTasks;
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        localTasks.swap(m_tasks);
    }
    for (auto& task : localTasks) {
        if (task) task();
    }
    flushTimedTasks();
}

void Application::flushTimedTasks() {
    uint64_t now = bx::getHPCounter();
    Tina::Container::Vector<std::function<void()>> readyTasks;
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        auto it = m_timedTasks.begin();
        while (it != m_timedTasks.end()) {
            if (it->dueMs <= now) {
                readyTasks.push_back(std::move(it->fn));
                it = m_timedTasks.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& task : readyTasks) {
        if (task) task();
    }
}

void Application::discardPendingTasks() {
    Tina::Container::Vector<std::function<void()>> pendingTasks;
    Tina::Container::Vector<TimedTask> pendingTimedTasks;
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        pendingTasks.swap(m_tasks);
        pendingTimedTasks.swap(m_timedTasks);
    }
    // 在资源管理器仍存活时于锁外析构捕获对象。
}

void Application::prewarmCommonAssets() {
    // 预热逻辑...
}

} // namespace Tina::Engine
