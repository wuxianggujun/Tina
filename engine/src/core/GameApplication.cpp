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

        window = createScope<GLFWWindow>();
        window->create(Window::WindowConfig{"Tina", resolution, false, false, false});
        inputHandler = createInputHandler(InputHandlerGLFW, window.get());
        inputHandler->initialize();
        mainLoop();
    }

    void GameApplication::mainLoop() const
    {
        while (window->shouldClose())
        {
            inputHandler->pollEvents();
            window->render();
            //window->pollEvents()
            inputHandler->processEvent();
        }
        window->destroy();
    }
} // Tina
