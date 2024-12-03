//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_GAMEAPPLICATION_HPP
#define TINA_CORE_GAMEAPPLICATION_HPP

#include "Core.hpp"
#include "window/Window.hpp"
#include "window/GLFWWindow.hpp"
#include "core/Renderer.hpp"
#include "window/EventHandler.hpp"
#include "gui/GuiSystem.hpp"
#include <entt/entt.hpp>

namespace Tina
{
    
    class GameApplication
    {

    public:
        GameApplication();
        ~GameApplication();

        void initialize();
        
        void run();
        void mainLoop();

        void shutdown();

    protected:
        ScopePtr<Window> window;
        ScopePtr<GuiSystem> guiSystem;
        ScopePtr<Renderer> renderer;
        ScopePtr<EventHandler> eventHandler;
        entt::registry registry;

        float lastFrameTime{0.0f};

        void update(float deltaTime);
        void render();
        void createTestGui();
        
    };
} // Tina

#endif //TINA_CORE_GAMEAPPLICATION_HPP