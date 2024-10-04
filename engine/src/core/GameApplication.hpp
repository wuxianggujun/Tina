//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_GAMEAPPLICATION_HPP
#define TINA_CORE_GAMEAPPLICATION_HPP

#include "Core.hpp"
#include "window/Window.hpp"
#include "window/GLFWWindow.hpp"
#include "window/InputHandler.hpp"
#include "core/Renderer.hpp"
#include "window/GLFWInput.hpp"

namespace Tina
{
    class GameApplication
    {
    public:
        void run();
        void mainLoop() const;

    protected:
        ScopePtr<Window> window;
        ScopePtr<InputHandler> inputHandler;
        ScopePtr<Renderer> renderer;
    };
} // Tina

#endif //TINA_CORE_GAMEAPPLICATION_HPP
