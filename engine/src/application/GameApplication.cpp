//
// Created by wuxianggujun on 2024/4/28.
//

#include <cstdio>

#include "GameApplication.hpp"

#include "framework/Configuration.hpp"
#include "framework/math/Vector2.hpp"

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

        FlatVector vectorA{3.0f, 4.0f};

        Vector2 a{1.0f, 2.0f};
        Vector2 b{3.0f, 4.0f};

        FlatVector aFlat{a.x, a.y};
        FlatVector bFlat{b.x, b.y};

        const auto duration = StopWatch::time([&aFlat, &bFlat,&a, &b] {
            for (int i = 0; i < 1000000; ++i) {
                /*a.x = a.x + b.x;
                b.y = b.y - a.y;*/
                /*aFlat += bFlat;*/
                aFlat.X = aFlat.X + bFlat.X;
                bFlat.Y = bFlat.Y - aFlat.Y;
            }
        });

        // 设置输出流的格式
        std::cout << std::fixed << std::setprecision(6);

        /*std::cout << "X: " << a.x << ", Y: " << a.y << ", Duration: " <<
                duration.count() << std::endl;*/

        std::cout << "X: " << aFlat.X << ", Y: " << aFlat.Y << ", Duration: " <<
                duration.count() << std::endl;

        return result;
    }

    void GameApplication::close() {
        delete renderer;
        window->destroy();
    }
} // Tina
