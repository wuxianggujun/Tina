//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"

namespace Tina {
    void GameApplication::run() {
        Vector2i resolution = Vector2i(1280, 720);

        window = createScopePtr<GLFWWindow>();
        window->create(Window::WindowConfig{"Tina", resolution, false, false, false});

        renderer = createScopePtr<Renderer>(resolution, 0);

        mainLoop();
    }

    void GameApplication::mainLoop() const {
        while (window->shouldClose()) {
            renderer->render();
        }
        renderer->shutdown();
        window->destroy();
    }
} // Tina
