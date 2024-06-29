//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>

#include "GameApplication.hpp"

#include "framework/Configuration.hpp"


namespace Tina {
    GameApplication::GameApplication(Configuration &configuration) {
        window = createScope<Window>(configuration);
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

        const auto duration = StopWatch::milliseconds([] {
            float x1 = 0.05f;
            float y1 = 0.002f;
            float x2 = 0.001f;
            float y2 = 0.003f;

            for (int i = 0; i < INT_MAX; i++) {
                x1 += x2;
                y1 += y2;
            }
        });


        std::cout << "it time " << duration << " milliseconds." << std::endl;

        return result;
    }

    void GameApplication::close() {
        delete renderer;
        window->destroy();
    }
} // Tina
