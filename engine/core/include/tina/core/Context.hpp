//
// Created by wuxianggujun on 2025/2/5.
//

#pragma once

#include "tina/core/Core.hpp"
#include <memory>

namespace Tina
{
    class WindowManager;
    class EventQueue;

    class TINA_CORE_API Context
    {
    public:
        static Context& getInstance();
        
        ~Context();
        bool initialize();
        void processEvents();

        WindowManager& getWindowManager() { return *m_windowManager; }
        EventQueue& getEventQueue() { return *m_eventQueue; }

    private:
        Context();  // 私有构造函数
        Context(const Context&) = delete;  // 禁止拷贝
        Context& operator=(const Context&) = delete;  // 禁止赋值
        
        static Context* s_instance;
        std::unique_ptr<WindowManager> m_windowManager;
        std::unique_ptr<EventQueue> m_eventQueue;
    };
} // Tina
