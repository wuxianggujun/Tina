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

} // namespace Tina
