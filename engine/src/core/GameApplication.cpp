//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"

namespace Tina {
    void GameApplication::run() {
        Vector2i resolution = Vector2i(1280, 720);

        window = createScopePtr<GLFWWindow>();
        eventHandler = createScopePtr<EventHandler>();
        window->create(Window::WindowConfig{"Tina", resolution, false, false, false});
        window->setEventHandler(std::move(eventHandler));
        renderer = createScopePtr<Renderer>(resolution, 0);

        mainLoop();
    }

    void GameApplication::mainLoop() const {
        while (window->shouldClose()) {
            window->pollEvents();
            renderer->render();
            //eventHandler->processEvents();
        }
        renderer->shutdown();
        window->destroy();
    }
} // Tina
