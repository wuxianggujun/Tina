//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"

namespace Tina
{
    void GameApplication::run()
    {
        window = new SDLWindow();
        window->create({"Tina Game Engine", 1280, 720, false, true, true});

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
