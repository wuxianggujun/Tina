//
// Created by wuxianggujun on 2025/1/31.
//

#include "tina/core/Engine.hpp"
#include "tina/core/Context.hpp"
#include "tina/window/WindowManager.hpp"
#include "tina/event/EventQueue.hpp"
#include "tina/log/Logger.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include "tina/renderer/Renderer2D.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include "tina/core/Timer.hpp"

namespace Tina::Core
{
    Engine* Engine::s_Instance = nullptr;

    Engine::Engine()
        : m_context(Context::getInstance())
          , m_mainWindow(BGFX_INVALID_HANDLE)
          , m_windowWidth(1280)
          , m_windowHeight(720)
          , m_isShutdown(false)
          , m_isInitialized(false)
    {
        s_Instance = this;
        TINA_LOG_INFO("Engine created.");
    }

    Engine::~Engine()
    {
        if (!m_isShutdown)
        {
            shutdown();
        }
        s_Instance = nullptr;
        TINA_LOG_INFO("Engine destroyed.");
    }

    bool Engine::initialize()
    {
        if (m_isInitialized)
        {
            TINA_LOG_WARN("Engine already initialized");
            return true;
        }

        try
        {
            // 初始化窗口管理器
            if (!m_context.getWindowManager().initialize())
            {
                TINA_LOG_ERROR("Failed to initialize window manager");
                return false;
            }

            // 创建主窗口
            Window::WindowConfig config;
            config.width = m_windowWidth;
            config.height = m_windowHeight;
            config.title = "Tina Engine";

            m_mainWindow = m_context.getWindowManager().createWindow(config);

            if (!isValid(m_mainWindow))
            {
                TINA_LOG_ERROR("Failed to create main window");
                return false;
            }

            // 初始化渲染系统
            bgfx::Init init;
            init.type = bgfx::RendererType::Count; // 自动选择渲染器
            init.resolution.width = m_windowWidth;
            init.resolution.height = m_windowHeight;
            init.resolution.reset = BGFX_RESET_VSYNC;
            init.platformData.nwh = m_context.getWindowManager().getNativeWindowHandle(m_mainWindow);
            init.platformData.ndt = m_context.getWindowManager().getNativeDisplayHandle();

            if (!bgfx::init(init))
            {
                TINA_LOG_ERROR("Failed to initialize bgfx");
                return false;
            }

            // 设置视口
            bgfx::setViewClear(0
                               , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
                               , 0x303030ff
                               , 1.0f
                               , 0
            );

            bgfx::setViewRect(0, 0, 0, uint16_t(m_windowWidth), uint16_t(m_windowHeight));

            m_isInitialized = true;
            TINA_LOG_INFO("Engine initialized successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during initialization: {}", e.what());
            return false;
        }
    }

    bool Engine::run()
    {
        if (!m_isInitialized)
        {
            TINA_LOG_ERROR("Engine not initialized");
            return false;
        }

        try
        {
            bool running = true;
            Timer timer(true);  // 单个Timer实例
            const float targetFrameTime = 1.0f / 60.0f; // 60 FPS

            while (running && !m_isShutdown)
            {
                float deltaTime = timer.getSeconds(true);
                timer.reset();

                // 处理窗口事件
                m_context.getWindowManager().pollEvents();

                // 处理事件队列
                Event event;
                while (m_context.getWindowManager().pollEvent(event))
                {
                    if (event.type == Event::WindowClose &&
                        event.windowHandle.idx == m_mainWindow.idx)
                    {
                        running = false;
                        break;
                    }

                    if (event.type == Event::WindowResize)
                    {
                        // 确保引擎层面也处理窗口大小改变
                        m_windowWidth = event.windowResize.width;
                        m_windowHeight = event.windowResize.height;
                        TINA_LOG_INFO("Render2DLayer: Window resized to {}x{}",
                                      event.windowResize.width,
                                      event.windowResize.height);
                    }

                    if (m_activeScene)
                    {
                        m_activeScene->onEvent(event);
                    }
                }

                // 检查窗口是否应该关闭
                Window* mainWindow = m_context.getWindowManager().getWindow(m_mainWindow);
                if (!mainWindow || mainWindow->shouldClose())
                {
                    running = false;
                    continue;
                }

                // 设置视口
                bgfx::setViewRect(0, 0, 0,
                                  uint16_t(mainWindow->getWidth()),
                                  uint16_t(mainWindow->getHeight()));

                // 清除背景
                bgfx::setViewClear(0,
                                   BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                                   0x303030ff,
                                   1.0f,
                                   0);

                // 触摸视口
                bgfx::touch(0);

                // 更新和渲染场景
                if (m_activeScene)
                {
                    m_activeScene->onUpdate(deltaTime);
                    m_activeScene->onRender();
                }

                // 提交帧
                bgfx::frame();

                // 帧率限制
                float frameTime = timer.getSeconds(true);
                if (frameTime < targetFrameTime)
                {
                    float sleepTime = (targetFrameTime - frameTime) * 1000.0f;
                    if (sleepTime > 0)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(sleepTime)));
                    }
                }
            }

            return true;
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during main loop: {}", e.what());
            return false;
        }
    }

    void Engine::shutdown()
    {
        if (m_isShutdown)
        {
            TINA_LOG_WARN("Engine already shut down");
            return;
        }

        try
        {
            TINA_LOG_INFO("Engine shutting down");
            m_isShutdown = true;

            // 1. 首先禁用 VSync
            if (bgfx::getInternalData() && bgfx::getInternalData()->context)
            {
                bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
                uint32_t reset = BGFX_RESET_NONE;
                bgfx::reset(m_windowWidth, m_windowHeight, reset);
            }

            if (m_activeScene)
            {
                TINA_LOG_DEBUG("Destroying active scene");
                m_activeScene.reset();
                TINA_LOG_DEBUG("Active scene destroyed");
            }

            if (bgfx::getInternalData() && bgfx::getInternalData()->context)
            {
                TINA_LOG_DEBUG("Finalizing render commands");
                bgfx::frame(false); // 不等待 VSync
                TINA_LOG_DEBUG("Shutting down BGFX");
                bgfx::shutdown();
            }
            TINA_LOG_INFO("Engine shutdown completed");
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during shutdown: {}", e.what());
            m_isShutdown = true;
        }
    }

    const char* Engine::getVersion() const
    {
        return TINA_VERSION;
    }

    Context& Engine::getContext()
    {
        return m_context;
    }

    Scene* Engine::createScene(const std::string& name)
    {
        m_activeScene = MakeUnique<Scene>(name);
        return m_activeScene.get();
    }

    void Engine::setActiveScene(Scene* scene)
    {
        if (scene)
        {
            m_activeScene.reset(scene);
            TINA_LOG_INFO("Set active scene: {}", scene->getName());
        }
    }
}
