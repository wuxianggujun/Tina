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
        TINA_LOG_INFO("Engine", "Engine created.");
    }

    Engine::~Engine()
    {
        TINA_LOG_INFO("Engine", "Engine destroyed.");
        shutdown();
    }

    bool Engine::initialize()
    {
        if (!s_ctx.initialize())
        {
            TINA_LOG_ERROR("Engine::initialize", "Failed to initialize context");
            return false;
        }

        // 创建主窗口
        WindowHandle mainWindow = s_ctx.getWindowManager().createWindow(100, 100, 1280, 720, 0, "Tina Engine");
        if (!isValid(mainWindow))
        {
            TINA_LOG_ERROR("Engine::initialize", "Failed to create main window");
            return false;
        }

        // 初始化bgfx
        bgfx::Init bgfxInit;
        bgfxInit.type = bgfx::RendererType::Count; // 自动选择渲染后端
        bgfxInit.resolution.width = 1280;
        bgfxInit.resolution.height = 720;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.platformData.nwh = s_ctx.getWindowManager().getNativeWindowHandle(mainWindow);
        bgfxInit.platformData.ndt = s_ctx.getWindowManager().getNativeDisplayHandle();
        bgfxInit.platformData.type = s_ctx.getWindowManager().getNativeWindowHandleType();

        if (!bgfx::init(bgfxInit))
        {
            TINA_LOG_ERROR("Engine::initialize", "Failed to initialize bgfx");
            return false;
        }

        // 设置调试标志
        bgfx::setDebug(BGFX_DEBUG_TEXT);

        // 设置视口
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, 1280, 720);

        TINA_LOG_INFO("Engine::initialize", "Engine initialized successfully");
        return true;
    }

    void Engine::shutdown()
    {
        bgfx::shutdown();
        s_ctx.getWindowManager().terminate();
    }

    bool Engine::run()
    {
        while (true)
        {
            // 处理事件
            s_ctx.getWindowManager().processMessage();

            Event event;
            while (s_ctx.getEventQueue().peekEvent(event))
            {
                switch (event.type)
                {
                    case Event::WindowDestroy:
                        return true;
                    case Event::Key:
                        if (event.key.key == GLFW_KEY_ESCAPE && event.key.action == GLFW_PRESS)
                        {
                            return true;
                        }
                        break;
                    case Event::WindowResize:
                        bgfx::reset(event.windowResize.width, event.windowResize.height, BGFX_RESET_VSYNC);
                        bgfx::setViewRect(0, 0, 0, event.windowResize.width, event.windowResize.height);
                        break;
                    default:
                        break;
                }
            }

            // 渲染
            bgfx::touch(0);
            bgfx::frame();
        }
    }

    const char* Engine::getVersion() const
    {
        return TINA_VERSION;
    }

}
