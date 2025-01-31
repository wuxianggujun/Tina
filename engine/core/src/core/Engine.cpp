//
// Created by wuxianggujun on 2025/1/31.
//

#include "tina/core/Engine.hpp"

namespace Tina {
    namespace Core {

        Engine::Engine()
        {
        }

        Engine::~Engine()
        {
            shutdown();
        }

        bool Engine::initialize()
        {
            // Add initialization code here
            return true;
        }

        void Engine::shutdown()
        {
            // Add cleanup code here
        }

        const char* Engine::getVersion() const
        {
            return TINA_VERSION;
        }

    } // namespace Core
} // namespace Tina