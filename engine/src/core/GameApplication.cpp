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

        window = new GlfwWindow();
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
