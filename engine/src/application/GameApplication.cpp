//
// Created by wuxianggujun on 2024/4/28.
//

#include "GameApplication.hpp"
#include "framework/Configuration.hpp"
#include "framework/Window.hpp"

namespace Tina {
    GameApplication::GameApplication(const char *title, int width, int height) {
        window = new Window(title, width, height);
    }

    GameApplication::GameApplication(const Configuration &configuration) {
        window = new Window(configuration.windowTitle,configuration.windowWidth,configuration.windowHeight);
    }


    bool GameApplication::initialize() {
        if (!window->initialize()){
            return false;
        }


        return true;
    }

    void GameApplication::close() {
        window->destroy();
        Application::close();
    }

    void GameApplication::run() {
        if (window->shouldClose()){
            return;
        }
        window->update();
    }


} // Tina