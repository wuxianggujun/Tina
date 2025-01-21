#include "CoreApplication.hpp"

#include "gui/BgfxGuiSystem.hpp"
#include "window/GLFWWindow.hpp"

namespace Tina
{
    CoreApplication::CoreApplication(const Path& configFilePath)
    {
        loadConfig(configFilePath);
    }

    CoreApplication::~CoreApplication()
    {
        CoreApplication::shutdown();
    }

    void CoreApplication::shutdown()
    {
        if (m_guiSystem)
        {
            m_guiSystem->shutdown();
            m_guiSystem.reset();
        }

        if (m_window)
        {
            m_window->destroy();
            m_window.reset();
        }
    }

    void CoreApplication::loadConfig(const Path& configFilePath)
    {
        try
        {
            m_config = createScopePtr<Config>();
            m_config->loadFromFile(configFilePath.getFullPath());

            std::string appName = m_config->get<std::string>("application.name");
            std::string runMode = m_config->get<std::string>("application.run-mode");

            // 设置配置项
            m_config->set("network.timeout", 5000);
        }
        catch (const std::runtime_error& e)
        {
            // 处理配置加载错误，例如记录日志或抛出异常
            std::cerr << "Error loading config: " << e.what() << std::endl;
            // 你可以选择退出程序或使用默认配置
            throw; // 重新抛出异常，让调用者处理
        }
    }

    ScopePtr<IWindow> CoreApplication::createWindow(const IWindow::WindowConfig& config)
    {
        ScopePtr<IWindow> window = createScopePtr<GLFWWindow>();
        window->create(config);
        return window;
    }

    ScopePtr<IGuiSystem> CoreApplication::createGuiSystem()
    {
        return createScopePtr<BgfxGuiSystem>();
    }

    ScopePtr<EventHandler> CoreApplication::createEventHandler()
    {
        return createScopePtr<EventHandler>();
    }
}
