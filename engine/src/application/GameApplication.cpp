//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>
#include "GameApplication.hpp"
#include "framework/Configuration.hpp"
#include "framework/Window.hpp"

namespace Tina {
    GameApplication::GameApplication(const Configuration &configuration) {
        window = new Window(configuration.windowTitle,configuration.windowWidth,configuration.windowHeight);
    }

    bool GameApplication::initialize() {
        return window->initialize();
    }

    void GameApplication::close() {
        window->destroy();
        Application::close();
    }

    void GameApplication::run() {
        //printf("GameApplication::run\n");
        window->update();
    }

    bool GameApplication::isRunning() {
        return window->shouldClose();
    }


} // Tina