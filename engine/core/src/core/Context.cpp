//
// Created by wuxianggujun on 2025/2/5.
//

#include "tina/event/EventQueue.hpp"
#include "tina/core/Context.hpp"
#include "tina/log/Logger.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

namespace Tina
{
    Context s_ctx;

    Context::Context()
        : windowManager(this)
        , eventQueue()
    {
        TINA_LOG_INFO("Context::Context", "Context created.");
    }

    Context::~Context()
    {
        windowManager.terminate();
        TINA_LOG_INFO("Context::~Context", "Context destroyed.");
    }

    bool Context::initialize()
    {
        if (!windowManager.initialize())
        {
            TINA_LOG_ERROR("Context::initialize", "WindowManager initialization failed.");
            return false;
        }

        TINA_LOG_INFO("Context::initialize", "Context initialized successfully.");
        return true;
    }

    void Context::processEvents()
    {
        windowManager.processMessage();
    }

    // bool Context::run(int argc, char** argv)
    // {
    //     if (!windowManager.initialize())
    //     {
    //         TINA_LOG_ERROR("Context::run", "WindowManager initialization failed.");
    //         return false;
    //     }
    //
    //     // 创建主窗口
    //     WindowHandle mainWindowHandle = windowManager.createWindow(100, 100, 1280, 720, 0, "Tina Main Window");
    //     if (!isValid(mainWindowHandle))
    //     {
    //         TINA_LOG_ERROR("Context::run", "Failed to create main window.");
    //         windowManager.terminate();
    //         return false;
    //     }
    //     TINA_LOG_INFO("Context::run", "Main window created. Handle: {}", mainWindowHandle.idx);
    //
    //     // 主循环
    //     bool running = true;
    //     while (running)
    //     {
    //         windowManager.processMessage();
    //
    //         Event event;
    //         while (eventQueue.peekEvent(event))
    //         {
    //             switch (event.type)
    //             {
    //                 case Event::Exit:
    //                     TINA_LOG_INFO("Context::run", "Exit event received.");
    //                     running = false;
    //                     break;
    //                 case Event::WindowResize:
    //                     TINA_LOG_INFO("Context::run", "WindowResize event. windowHandle: {}, width: {}, height: {}",
    //                                 event.windowHandle.idx, event.windowResize.width, event.windowResize.height);
    //                     bgfx::reset(event.windowResize.width, event.windowResize.height);
    //                     bgfx::setViewRect(0, 0, 0, event.windowResize.width, event.windowResize.height);
    //                     break;
    //                 case Event::Key:
    //                     TINA_LOG_TRACE("Context::run", "Key event. windowHandle: {}, key: {}, action: {}",
    //                                  event.windowHandle.idx, event.key.key, event.key.action);
    //                     if (event.key.key == GLFW_KEY_ESCAPE && event.key.action == GLFW_PRESS)
    //                     {
    //                         TINA_LOG_INFO("Context::run", "ESC key pressed. Exiting...");
    //                         running = false;
    //                     }
    //                     break;
    //                 default:
    //                     break;
    //             }
    //         }
    //
    //         bgfx::frame();
    //     }
    //
    //     return true;
    // }

} // namespace Tina
