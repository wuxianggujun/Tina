#include "CoreApplication.hpp"

#include "ParserYamlConfig.hpp"
#include "gui/BgfxGuiSystem.hpp"
#include "window/GLFWWindow.hpp"

namespace Tina {
    CoreApplication::CoreApplication(const Path &configFilePath) {
        loadConfig(configFilePath);
    }

    CoreApplication::~CoreApplication() {
        CoreApplication::shutdown();
    }

    void CoreApplication::shutdown() {
        if (m_guiSystem) {
            m_guiSystem->shutdown();
            m_guiSystem.reset();
        }

        if (m_window) {
            m_window->destroy();
            m_window.reset();
        }
    }

    void CoreApplication::loadConfig(const Path &configFilePath) {
        m_config = createScopePtr<Config>();
        m_config->loadFromFile(configFilePath.getFullPath());

        // 获取配置项
        bool isDebugEnabled = m_config->get<bool>("debug.enabled");
        int windowWidth = m_config->get<int>("window.width");
        std::string appName = m_config->get<std::string>("application.name");

        // 设置配置项
        m_config->set("network.timeout", 5000);
        
    }

    ScopePtr<IWindow> CoreApplication::createWindow(const IWindow::WindowConfig &config) {
        ScopePtr<IWindow> window = createScopePtr<GLFWWindow>(config);
        window->create(config);
        return window;
    }

    ScopePtr<IGuiSystem> CoreApplication::createGuiSystem() {
        return createScopePtr<BgfxGuiSystem>();
    }

    ScopePtr<EventHandler> CoreApplication::createEventHandler() {
        return createScopePtr<EventHandler>();
    }
}
