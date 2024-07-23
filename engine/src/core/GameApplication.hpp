//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_GAMEAPPLICATION_HPP
#define TINA_CORE_GAMEAPPLICATION_HPP

#include "Core.hpp"
#include "window/Window.hpp"
#include "window/GlfwWindow.hpp"
#include "window/SDLWindow.hpp"

namespace Tina
{
    class GameApplication
    {
    public:
        void run();
        void mainLoop() const;

    protected:
        Scope<Window> window;
    };
} // Tina

#endif //TINA_CORE_GAMEAPPLICATION_HPP
