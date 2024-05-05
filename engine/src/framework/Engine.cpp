//
// Created by wuxianggujun on 2024/4/27.
//

#include "Engine.hpp"
#include "Configuration.hpp"
#include "Application.hpp"

namespace Tina {

    Engine *Engine::instance_ = nullptr;

    Engine *Engine::getSingleton() {
        if (instance_ == nullptr){
            instance_ = new Engine();
        }
        return instance_;
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
