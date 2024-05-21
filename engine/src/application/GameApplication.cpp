//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>

#include "GameApplication.hpp"

#include "framework/Configuration.hpp"

namespace Tina {

    GameApplication::GameApplication(const Configuration &configuration) {
        window = createScope<Window>();
        renderer = new WorldRenderer();
    }

    void GameApplication::run(float deltaTime) {
        window->update();
        renderer->render(deltaTime);
    }

    bool GameApplication::isRunning() {
        return window->isRunning();
    }

    bool GameApplication::initialize() {
        bool result = window->initialize();
        renderer->initialize();
        return result;
    }

    void GameApplication::close() {
        delete renderer;
        window->destroy();
    }


} // Tina
