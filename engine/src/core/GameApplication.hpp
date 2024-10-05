//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_GAMEAPPLICATION_HPP
#define TINA_CORE_GAMEAPPLICATION_HPP

#include "Core.hpp"
#include "window/Window.hpp"
#include "window/GLFWWindow.hpp"
#include "core/Renderer.hpp"
#include "window/EventHandle.hpp"

namespace Tina
{
    class GameApplication
    {
    public:
        void run();
        void mainLoop() const;

    protected:
        ScopePtr<Window> window;
        ScopePtr<Renderer> renderer;
        ScopePtr<EventHandle> eventHandler;
    };
} // Tina

#endif //TINA_CORE_GAMEAPPLICATION_HPP
