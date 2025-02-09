//
// Created by wuxianggujun on 2025/1/31.
//

#include "tina/core/Engine.hpp"

#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"

namespace Tina::Core
{
    Engine::Engine()
    {
        TINA_LOG_INFO("Engine created.");
    }

    Engine::~Engine()
    {
        TINA_LOG_INFO("Engine destroyed.");
        // 确保在析构时调用 shutdown
        if (bgfx::getInternalData()->context)
        {
            shutdown();
        }
    }

    bool Engine::initialize()
    {
        if (!m_context.initialize())
        {
            TINA_LOG_ERROR("Failed to initialize context");
            return false;
        }

        // 创建主窗口
        WindowHandle mainWindow = m_context.getWindowManager().createWindow(100, 100, 1280, 720, 0, "Tina Engine");
        if (!isValid(mainWindow))
        {
            TINA_LOG_ERROR("Failed to create main window");
            return false;
        }

        // 初始化bgfx
        bgfx::Init bgfxInit;
        bgfxInit.type = bgfx::RendererType::Count; // 自动选择渲染后端
        bgfxInit.resolution.width = 1280;
        bgfxInit.resolution.height = 720;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.platformData.nwh = m_context.getWindowManager().getNativeWindowHandle(mainWindow);
        bgfxInit.platformData.ndt = m_context.getWindowManager().getNativeDisplayHandle();
        bgfxInit.platformData.type = m_context.getWindowManager().getNativeWindowHandleType();

        if (!bgfx::init(bgfxInit))
        {
            TINA_LOG_ERROR("Failed to initialize bgfx");
            return false;
        }

        // 设置调试标志
        bgfx::setDebug(BGFX_DEBUG_TEXT);

        // 设置初始视口和清除颜色
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, bgfxInit.resolution.width, bgfxInit.resolution.height);

        TINA_LOG_INFO("Engine initialized successfully");
        return true;
    }

    void Engine::shutdown()
    {
        TINA_LOG_INFO("Engine shutting down");
        // 先关闭 bgfx
        if (bgfx::getInternalData()->context)
        {
            bgfx::shutdown();
        }
        // 再关闭窗口管理器
        m_context.getWindowManager().terminate();
        TINA_LOG_INFO("Engine shutdown completed");
    }

    bool Engine::run()
    {
        const uint32_t clearColor = 0x443355FF;
        bool running = true;

        while (running)
        {
            // 处理窗口事件
            m_context.processEvents();

            // 处理事件队列中的所有事件
            Event event;
            while (m_context.getEventQueue().peekEvent(event))
            {
                switch (event.type)
                {
                case Event::WindowDestroy:
                case Event::WindowClose:
                    running = false;
                    TINA_LOG_INFO("Window close event received");
                    break;
                case Event::Key:
                    if (event.key.key == GLFW_KEY_ESCAPE && event.key.action == GLFW_PRESS)
                    {
                        running = false;
                        TINA_LOG_INFO("ESC key pressed, closing window");
                    }
                    break;
                case Event::WindowResize:
                    {
                        // 更新视口和渲染目标
                        bgfx::reset(event.windowResize.width, event.windowResize.height, BGFX_RESET_VSYNC);
                        bgfx::setViewRect(0, 0, 0, uint16_t(event.windowResize.width), uint16_t(event.windowResize.height));
                        TINA_LOG_INFO("Window resized");
                    }
                    break;
                default:
                    break;
                }
                // 从队列中移除已处理的事件
                m_context.getEventQueue().pollEvent();
            }

            // 如果窗口要关闭，就不要再渲染了
            if (!running)
            {
                break;
            }

            // 设置渲染状态
            bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor, 1.0f, 0);

            // 确保视图 0 被更新
            bgfx::touch(0);

            // 提交帧缓冲并等待垂直同步
            bgfx::frame();
        }

        // 在退出主循环后确保正确关闭
        shutdown();
        return true;
    }

    const char* Engine::getVersion() const
    {
        return TINA_VERSION;
    }
}
