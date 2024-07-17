//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector2.hpp"

namespace Tina
{
    void GameApplication::run()
    {
        Vector2 resolution = Vector2(1280, 720);

        window = new SDLWindow();
        window->create({
            "Tina Game Engine", static_cast<int>(resolution.x), static_cast<int>(resolution.y), false, true, true
        });

        mainLoop();
    }

    void GameApplication::mainLoop()
    {
        while (window->shouldClose())
        {
            window->pollEvents();
        }
    }
} // Tina
