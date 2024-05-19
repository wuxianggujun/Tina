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

    void GameApplication::run() {
        window->update();
        renderer->render(0.1f);
    }

    bool GameApplication::isRunning() {
        return window->isRunning();
    }

    bool GameApplication::initialize() {
        return window->initialize();
    }

    void GameApplication::close() {
        delete renderer;
        window->destroy();

    }


} // Tina
