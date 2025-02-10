//
// Created by wuxianggujun on 2025/2/5.
//

#include "tina/core/Context.hpp"
#include "tina/window/WindowManager.hpp"
#include "tina/event/EventQueue.hpp"
#include "tina/log/Logger.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <memory>

namespace Tina
{
    Context* Context::s_instance = nullptr;

    Context& Context::getInstance()
    {
        if (!s_instance)
        {
            s_instance = new Context();
        }
        return *s_instance;
    }

    Context::Context() 
        : m_windowManager(std::make_unique<WindowManager>(this))
        , m_eventQueue(std::make_unique<EventQueue>())
    {
        TINA_LOG_INFO("Context created");
    }

    Context::~Context() {
        TINA_LOG_INFO("Context destroyed");
        m_windowManager.reset();
        m_eventQueue.reset();
        s_instance = nullptr;
    }

    bool Context::initialize() {
        TINA_LOG_INFO("Initializing context");
        return true;
    }

    void Context::processEvents() {
        if (m_windowManager) {
            m_windowManager->processMessage();
        }
    }
} // namespace Tina
