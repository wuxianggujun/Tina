//
// Created by wuxianggujun on 2025/1/31.
//
#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Context.hpp"

namespace Tina::Core
{

    class TINA_CORE_API Engine {
    public:
        Engine();
        ~Engine();

        bool initialize();
        void shutdown();

        bool run();

        // Get engine version
        const char* getVersion() const;

    private:
        Context m_context;
    };

}
