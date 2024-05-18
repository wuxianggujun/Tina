//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>

#include "GameApplication.hpp"
#include "framework/Configuration.hpp"

namespace Tina {
    GameApplication::GameApplication(const Configuration &configuration) {
        WindowProps props;
        props.width = 1080;
        props.height = 720;
        props.windowMode = WindowMode::WINDOWED;
        props.title = "Tina";
        props.posX = 20;
        props.posY = 35;
        window = createScope<Window>(std::move(props));
    }

    void GameApplication::run() {
        window->update();
    }

    bool GameApplication::isRunning() {
        return window->shouldClose();
    }

    bool GameApplication::initialize() {
        WindowProps props;
        props.width = 1080;
        props.height = 720;
        props.windowMode = WindowMode::WINDOWED;
        props.title = "Tina";
        props.posX = 20;
        props.posY = 35;
        return window->initialize(props);
    }

    void GameApplication::close() {
        window->destroy();
    }


} // Tina
