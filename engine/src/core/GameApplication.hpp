//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_GAMEAPPLICATION_HPP
#define TINA_CORE_GAMEAPPLICATION_HPP

#include "Core.hpp"
#include "window/Window.hpp"
#include "window/GLFWWindow.hpp"
#include "window/SDLWindow.hpp"
#include "window/InputHandler.hpp"
#include "window/GLFWInput.hpp"
#include "window/SDLInput.hpp"
#include <ctrack.hpp>

namespace Tina
{
    class GameApplication
    {
    public:
        void run();
        void mainLoop() const;

    protected:
        Scope<Window> window;
        Scope<InputHandler> inputHandler;
    };
} // Tina

#endif //TINA_CORE_GAMEAPPLICATION_HPP
