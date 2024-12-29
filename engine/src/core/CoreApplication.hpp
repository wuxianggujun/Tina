#ifndef TINA_CORE_APPLICATION_HPP
#define TINA_CORE_APPLICATION_HPP

#include <cstdint>
#include <string>

#include "Config.hpp"
#include "Core.hpp"
#include "filesystem/Path.hpp"
#include "gui/IGuiSystem.hpp"
#include "resource/ResourceManager.hpp"
#include "window/IWindow.hpp"

namespace Tina {
    class CoreApplication {
    public:
        explicit CoreApplication(const Path &configFilePath);

        virtual ~CoreApplication();

        virtual void run() = 0;

    protected:
        virtual void initialize() = 0;

        virtual void shutdown();

        void loadConfig(const Path &configFilePath);

        virtual ScopePtr<IWindow> createWindow(const IWindow::WindowConfig &config);

        virtual ScopePtr<IGuiSystem> createGuiSystem();

        virtual ScopePtr<EventHandler> createEventHandler();

        ScopePtr<Config> m_config;
        ScopePtr<IWindow> m_window;
        ScopePtr<IGuiSystem> m_guiSystem;
        ScopePtr<EventHandler> m_eventHandler;
        ScopePtr<ResourceManager> m_resourceManager;
    };
}


#endif //TINA_CORE_APPLICATION_HPP
