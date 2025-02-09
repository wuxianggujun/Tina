//
// Created by wuxianggujun on 2025/1/31.
//
#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Context.hpp"
#include "tina/window/Window.hpp"

namespace Tina::Core
{
    class TINA_CORE_API Engine {
    public:
        Engine();
        ~Engine();

        bool initialize();
        bool run();
        void shutdown();
        const char* getVersion();
        Context& getContext();

    private:
        Context m_context;
        WindowHandle m_mainWindow;
    };

}
