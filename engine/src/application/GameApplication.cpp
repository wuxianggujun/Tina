//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>

#include "GameApplication.hpp"
#include "framework/Configuration.hpp"

namespace Tina {
    GameApplication::GameApplication(const Configuration &configuration) {
       /* window = std::make_unique<Window>(configuration.windowTitle, configuration.windowWidth,
                                          configuration.windowHeight);*/
    }

    void GameApplication::run() {
       /* window->update();*/
    }

    bool GameApplication::isRunning() {
       /* return window->shouldClose();*/
        return true;
    }

    bool GameApplication::initialize() {
        return false;
    }

    void GameApplication::close() {
        /*Application::close();*/
    }


} // Tina