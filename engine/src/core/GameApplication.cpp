//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"

namespace Tina
{
    void GameApplication::run()
    {
        Vector2i resolution = Vector2i(1280, 720);

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
