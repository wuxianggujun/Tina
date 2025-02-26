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

        // 设置视口和清屏颜色
        bgfx::setViewRect(0, 0, 0, config.width, config.height);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

        // 设置2D视图矩阵
        float view[16];
        bx::mtxIdentity(view); // 使用单位矩阵作为视图矩阵
        
        // 设置正交投影矩阵，使用左上角作为原点，扩大可视区域
        float ortho[16];
        const float viewWidth = static_cast<float>(config.width);
        const float viewHeight = static_cast<float>(config.height);
        // const float maxDimension = std::max(viewWidth, viewHeight) * 2.0f; // 扩大可视区域
        
        bx::mtxOrtho(ortho, 
            0.0f, viewWidth,           // left, right
            viewHeight, 0.0f,           // bottom, top (翻转Y轴，使Y轴向下为正)
            -1.0f, 1.0f,                  // near, far
            0.0f, bgfx::getCaps()->homogeneousDepth);
            
        bgfx::setViewTransform(0, view, ortho);

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

            // 更新场景（传入deltaTime）
            float deltaTime = m_timer.getSeconds(true); // 获取上一帧的时间间

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
            [](const EventPtr& event)
            {
                TINA_ENGINE_INFO("Window resized: {}x{}",
                                 event->windowResize.width,
                                 event->windowResize.height);
                bgfx::reset(event->windowResize.width, event->windowResize.height, BGFX_RESET_VSYNC);
                bgfx::setViewRect(0, 0, 0,
                                  static_cast<uint16_t>(event->windowResize.width),
                                  static_cast<uint16_t>(event->windowResize.height));
                
                // 更新2D视图矩阵
                float view[16];
                bx::mtxIdentity(view); // 使用单位矩阵作为视图矩阵
                
                // 更新正交投影矩阵，使用左上角作为原点，扩大可视区域
                float ortho[16];
                const float viewWidth = static_cast<float>(event->windowResize.width);
                const float viewHeight = static_cast<float>(event->windowResize.height);
                // const float maxDimension = std::max(viewWidth, viewHeight) * 2.0f; // 扩大可视区域
                
                bx::mtxOrtho(ortho, 
                    0.0f, viewWidth,           // left, right
                    viewHeight, 0.0f,           // bottom, top (翻转Y轴，使Y轴向下为正)
                    -1.0f, 1.0f,                  // near, far
                    0.0f, bgfx::getCaps()->homogeneousDepth);
                    
                bgfx::setViewTransform(0, view, ortho);
            },
            Event::WindowResize
        );
    }
}
