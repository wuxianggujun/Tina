//
// Created by wuxianggujun on 2024/4/27.
//

#include "Engine.hpp"
#include "Configuration.hpp"
#include "Application.hpp"

namespace Tina {

    Engine *Engine::singleton = nullptr;

    Engine::Engine() {
        singleton = this;
    }

    Engine *Engine::getSingleton() {
        return singleton;
    }

    int Engine::run(std::unique_ptr<Application> application) {
        isRunning = application->initialize();
        while (isRunning){
            application->run();
        }
        application->close();
        return 0;
    }


} // Tina
