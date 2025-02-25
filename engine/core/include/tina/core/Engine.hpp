//
// Created by wuxianggujun on 2025/1/31.
//
#pragma once

#include "tina/window/WindowManager.hpp"
#include "tina/event/EventManager.hpp"
#include "tina/resource/ResourceManager.hpp"
#include "tina/core/SceneManager.hpp"
#include "tina/core/Singleton.hpp"
#include "tina/core/Timer.hpp"
#include "tina/input/InputManager.hpp"

namespace Tina
{
    class TINA_CORE_API Engine : public Singleton<Engine>
    {
        friend class Singleton<Engine>;

    public:
        bool initialize(const Window::WindowConfig& config);
        bool run();
        void shutdown();

        [[nodiscard]] WindowManager* getWindowManager() const { return m_windowManager; }
        [[nodiscard]] EventManager* getEventManager() const { return m_eventManager; }
        [[nodiscard]] ResourceManager* getResourceManager() const { return m_resourceManager; }
        [[nodiscard]] SceneManager* getSceneManager() const { return m_sceneManager; }
        [[nodiscard]] InputManager* getInputManager() const { return InputManager::getInstance(); }
        [[nodiscard]] Window* getMainWindow() const { return m_window; }

    private:
        Engine();
        ~Engine() override;

        void initializeEventHandlers();

        WindowManager* m_windowManager{nullptr};
        EventManager* m_eventManager{nullptr};
        ResourceManager* m_resourceManager{nullptr};
        SceneManager* m_sceneManager{nullptr};

        WindowHandle m_windowHandle{};
        Window* m_window{nullptr};

        Timer m_timer;
        float m_targetFPS{60.0f};
    };
}
