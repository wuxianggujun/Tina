//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_CORE_GAMEAPPLICATION_HPP
#define TINA_CORE_GAMEAPPLICATION_HPP

#include "CoreApplication.hpp"
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
    
    class GameApplication : public CoreApplication
    {

    public:
        explicit GameApplication(const Path &configFilePath);
        
        ~GameApplication() override;
        
        void run() override;
        void mainLoop();

    protected:
        void initialize() override;
        
        entt::registry m_registry;

        float lastFrameTime{0.0f};

        void update(float deltaTime);
        void render();
        void createTestGui();
    };
} // Tina

#endif //TINA_CORE_GAMEAPPLICATION_HPP