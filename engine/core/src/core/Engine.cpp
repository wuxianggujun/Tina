//
// Created by wuxianggujun on 2025/1/31.
//

#include "tina/core/Engine.hpp"

#include "bx/math.h"
#include "tina/log/Log.hpp"
#include "tina/resource/ShaderLoader.hpp"
#include "tina/resource/TextureLoader.hpp"

namespace Tina
{
    bool Engine::initialize(const Window::WindowConfig& config)
    {
        // 初始化日志系统
        Log::init();

        // 初始化窗口管理器
        if (!m_windowManager->initialize())
        {
            TINA_ENGINE_ERROR("Failed to initialize WindowManager");
            return false;
        }

        // 注册资源加载器
        m_resourceManager->registerLoader(MakeUnique<ShaderLoader>());
        m_resourceManager->registerLoader(MakeUnique<TextureLoader>());

        // 初始化输入管理器
        InputManager::getInstance()->initialize();

        // 初始化条件处理
        initializeEventHandlers();

        m_windowHandle = m_windowManager->createWindow(config);
        if (!isValid(m_windowHandle))
        {
            TINA_ENGINE_ERROR("Failed to create window");
            return false;
        }

        // 初始化窗口指针
        m_window = m_windowManager->getWindow(m_windowHandle);
        if (!m_window)
        {
            TINA_ENGINE_ERROR("Failed to get window pointer");
            return false;
        }

        // 初始化场景管理器
        if (!m_sceneManager->initialize())
        {
            TINA_ENGINE_ERROR("Failed to initialize SceneManager");
            return false;
        }

        // 创建主相机
        m_mainCamera = MakeUnique<Camera2D>(static_cast<float>(config.width), static_cast<float>(config.height));

        // 设置视口和清屏颜色
        bgfx::setViewRect(0, 0, 0, config.width, config.height);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

        // 设置相机视图和投影矩阵
        bgfx::setViewTransform(0, m_mainCamera->getViewMatrix(), m_mainCamera->getProjectionMatrix());

        TINA_ENGINE_INFO("Engine initialized successfully with target FPS: {}", m_targetFPS);
        return true;
    }

    bool Engine::run()
    {
        const float targetFrameTime = 1.0f / m_targetFPS; // 目标帧时间（秒）

        while (!m_window->shouldClose())
        {
            float frameStartTime = m_timer.getSeconds(); // 已经是秒为单位

            // 处理窗口事件
            m_windowManager->processMessage();

            // 更新输入管理器
            InputManager::getInstance()->update();

            // 更新场景（传入deltaTime）
            float deltaTime = m_timer.getSeconds(true); // 获取上一帧的时间间

            // 确保相机矩阵是最新的
            m_mainCamera->updateMatrices();
            bgfx::setViewTransform(0, m_mainCamera->getViewMatrix(), m_mainCamera->getProjectionMatrix());

            // 更新场景
            m_sceneManager->update(deltaTime);

            // 触发视图渲染
            bgfx::touch(0);

            // 渲染场景
            m_sceneManager->render();

            // 提交帧到GPU
            bgfx::frame();

            // 计算当前帧耗时并等待
            float frameTime = m_timer.getSeconds() - frameStartTime;
            if (frameTime < targetFrameTime)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    static_cast<int32_t>((targetFrameTime - frameTime) * 1000.0f)));
            }
        }
        return true;
    }

    void Engine::shutdown()
    {
        // 关闭输入管理器
        InputManager::getInstance()->shutdown();

        if (m_sceneManager)
        {
            m_sceneManager->shutdown();
        }

        if (m_resourceManager)
        {
            m_resourceManager->shutdown();
        }

        if (m_windowManager)
        {
            if (isValid(m_windowHandle))
            {
                m_windowManager->destroyWindow(m_windowHandle);
            }
        }

        // 释放相机资源
        m_mainCamera = nullptr;

        // 关闭日志系统
        Log::shutdown();
    }

    Engine::Engine(): m_timer(true)
    {
        m_windowManager = WindowManager::getInstance();
        m_eventManager = EventManager::getInstance();
        m_resourceManager = ResourceManager::getInstance();
        m_sceneManager = SceneManager::getInstance();
    }

    Engine::~Engine()
    {
        shutdown();
    }

    void Engine::initializeEventHandlers()
    {
        // 注册窗口创建事件
        m_eventManager->addListener(
            [](const EventPtr& event)
            {
                TINA_ENGINE_INFO("Window created: {}x{}",
                                 event->windowCreate.width,
                                 event->windowCreate.height);
            },
            Event::WindowCreate
        );

        // 注册窗口调整大小事件
        m_eventManager->addListener(
            [this](const EventPtr& event)
            {
                TINA_ENGINE_INFO("Window resized: {}x{}",
                                 event->windowResize.width,
                                 event->windowResize.height);
                
                bgfx::reset(event->windowResize.width, event->windowResize.height, BGFX_RESET_VSYNC);
                bgfx::setViewRect(0, 0, 0,
                                  static_cast<uint16_t>(event->windowResize.width),
                                  static_cast<uint16_t>(event->windowResize.height));
                
                // 更新相机视口大小
                if (m_mainCamera)
                {
                    m_mainCamera->setViewport(
                        static_cast<float>(event->windowResize.width),
                        static_cast<float>(event->windowResize.height)
                    );
                    m_mainCamera->updateMatrices();
                    bgfx::setViewTransform(0, m_mainCamera->getViewMatrix(), m_mainCamera->getProjectionMatrix());
                }
            },
            Event::WindowResize
        );
    }
}
