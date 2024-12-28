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
#include "window/IWindow.hpp"
#include "IRenderer.hpp"
#include "gui/IGuiSystem.hpp"
#include "resource/ResourceManager.hpp"

namespace Tina
{
    
    class GameApplication
    {

    public:
        GameApplication(ScopePtr<IWindow>&& window,ScopePtr<IRenderer>&& renderer, ScopePtr<IGuiSystem>&& guiSystem, ScopePtr<EventHandler>&& eventHandler);
        
        ~GameApplication();

        void initialize();
        
        void run();
        void mainLoop();

        void shutdown();

    protected:
        ScopePtr<IWindow> m_window;
        ScopePtr<IGuiSystem> m_guiSystem;
        ScopePtr<IRenderer> m_renderer;
        ScopePtr<EventHandler> m_eventHandler;
        ScopePtr<ResourceManager> m_resourceManager;
        entt::registry m_registry;

        float lastFrameTime{0.0f};

        void update(float deltaTime);
        void render();
        void createTestGui();
        
    };
} // Tina

#endif //TINA_CORE_GAMEAPPLICATION_HPP