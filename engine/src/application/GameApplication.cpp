//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>

#include "GameApplication.hpp"
#include "framework/Configuration.hpp"
#include "framework/Window.hpp"

namespace Tina {
    GameApplication::GameApplication(const Configuration &configuration) {
        window = std::make_shared<Window>(configuration.windowTitle, configuration.windowWidth,
                                          configuration.windowHeight);
    }

    bool GameApplication::initialize() {
        return window->initialize();
    }

    void GameApplication::close() {
        window->destroy();
    }

    void GameApplication::run() {
        window->update();
    }

    bool GameApplication::isRunning() {
        return window->shouldClose();
    }


} // Tina