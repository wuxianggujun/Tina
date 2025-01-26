//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"
#include "graphics/Renderer2D.hpp"
#include "graphics/Color.hpp"
#include "window/GLFWWindow.hpp"
#include "core/Config.hpp"
#include <fmt/format.h>

namespace Tina
{
    GameApplication::GameApplication() : m_lastFrameTime(0.0f), m_configPath("")
    {
    }

    GameApplication::GameApplication(const Path& configPath)
        : m_lastFrameTime(0.0f)
        , m_configPath(configPath)
    {
    }

    GameApplication::~GameApplication()
    {
        shutdown();
    }

    void GameApplication::initialize()
    {
        // 创建窗口配置
        IWindow::WindowConfig windowConfig;
        windowConfig.title = "Tina Engine";
        windowConfig.resolution.width = 1280;
        windowConfig.resolution.height = 720;
        windowConfig.resizable = true;
        windowConfig.maximized = false;
        windowConfig.vsync = true;

        // 如果有配置文件，从配置文件读取配置
        if (m_configPath.exists())
        {
            try 
            {
                Config config;
                config.loadFromFile(m_configPath.toString());

                if (config.contains("window.title"))
                    windowConfig.title = config.get<std::string>("window.title");
                if (config.contains("window.width"))
                    windowConfig.resolution.width = config.get<int>("window.width");
                if (config.contains("window.height"))
                    windowConfig.resolution.height = config.get<int>("window.height");
                if (config.contains("window.resizable"))
                    windowConfig.resizable = config.get<bool>("window.resizable");
                if (config.contains("window.maximized"))
                    windowConfig.maximized = config.get<bool>("window.maximized");
                if (config.contains("window.vsync-enabled"))
                    windowConfig.vsync = config.get<bool>("window.vsync-enabled");
            }
            catch (const std::exception& e)
            {
                // 处理异常
            }
        }

        // 创建窗口
        m_window = std::make_unique<GLFWWindow>();
        m_window->create(windowConfig);

        // 创建GUI系统
        m_guiSystem = std::make_unique<GuiSystem>();

        // 创建2D渲染器
        m_renderer2D = std::make_unique<Renderer2D>();
    }

    void GameApplication::run()
    {
        initialize();
        mainLoop();
    }

    void GameApplication::mainLoop()
    {
        while (m_window && !m_window->shouldClose())
        {
            float currentTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentTime - m_lastFrameTime;
            m_lastFrameTime = currentTime;

            update(deltaTime);
            render();

            m_window->pollEvents();
        }
    }

    void GameApplication::update(float deltaTime)
    {
        if (m_guiSystem)
        {
            // m_guiSystem->update(deltaTime);
        }
    }

    void GameApplication::render()
    {
        if (m_window)
        {
            if (m_renderer2D)
            {
                m_renderer2D->begin();
                // 在这里添加2D渲染代码
                m_renderer2D->end();
            }

            if (m_guiSystem)
            {
                // m_guiSystem->render();
            }
        }
    }

    void GameApplication::shutdown()
    {
        if (m_renderer2D)
        {
            m_renderer2D.reset();
        }

        if (m_guiSystem)
        {
            m_guiSystem.reset();
        }

        if (m_window)
        {
            m_window.reset();
        }
    }
} // Tina
