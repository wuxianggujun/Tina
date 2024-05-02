//
// Created by wuxianggujun on 2024/4/28.
//

#include "GameApplication.hpp"
#include "framework/Configuration.hpp"

namespace Tina {
    bool GameApplication::initialize() {
        return false;
    }

    void GameApplication::close() {
        Application::close();
    }

    GameApplication::GameApplication(Configuration config) {

    }

} // Tina