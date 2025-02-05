//
// Created by wuxianggujun on 2025/2/5.
//

#pragma once

#include "tina/window/WindowManager.hpp"
#include "tina/event/EventQueue.hpp"

namespace Tina
{
    class Context
    {
    public:
        Context();
        ~Context();

        bool initialize();
        // bool run(int argc, char** argv);
        void processEvents();

        WindowManager& getWindowManager() { return windowManager; }
        EventQueue& getEventQueue() { return eventQueue; }

    private:
        bx::AllocatorI* m_allocator;
        WindowManager windowManager;
        EventQueue eventQueue;

        friend class WindowManager;
    };
} // Tina
