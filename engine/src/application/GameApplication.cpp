//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>

#include "GameApplication.hpp"
#include "framework/Configuration.hpp"

namespace Tina {
    GameApplication::GameApplication(const Configuration &configuration) {
        window = createScope<Window>();
    }

    void GameApplication::run() {
        window->update();
    }

    bool GameApplication::isRunning() {
        return window->isRunning();
    }

    bool GameApplication::initialize() {
        return true;
    }

    void GameApplication::close() {
        window->destroy();
    }


} // Tina
