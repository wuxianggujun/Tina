//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"
#include "graphics/Renderer2D.hpp"
#include "graphics/Color.hpp"
#include "window/GLFWWindow.hpp"
#include "core/Config.hpp"
#include <bgfx/bgfx.h>
#include <fmt/format.h>

#include "bx/math.h"

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
        // m_guiSystem = std::make_unique<GuiSystem>();

        // 创建2D渲染器
        m_renderer2D = std::make_unique<Renderer2D>();
        m_renderer2D->initialize();
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
        // if (m_guiSystem)
        // {
        //     // m_guiSystem->update(deltaTime);
        // }
    }

    void GameApplication::render()
    {
        if (m_window)
        {
            // 设置视图清屏状态
            bgfx::setViewClear(0
                , BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
                , 0x303030ff  // 深灰色背景
                , 1.0f
                , 0
            );

            // 设置视图矩形
            uint16_t width = uint16_t(m_window->getResolution().x);
            uint16_t height = uint16_t(m_window->getResolution().y);
            bgfx::setViewRect(0, 0, 0, width, height);

            // 设置视图变换矩阵
            float view[16];
            float proj[16];
            bx::mtxIdentity(view);
            bx::mtxOrtho(
                proj,
                0.0f,
                float(width),
                float(height),
                0.0f,
                0.0f,
                1000.0f,
                0.0f,
                bgfx::getCaps()->homogeneousDepth
            );
            bgfx::setViewTransform(0, view, proj);

            // 渲染2D内容
            if (m_renderer2D)
            {
                m_renderer2D->render();
            }

            // 提交帧
            bgfx::frame();
        }
    }

    void GameApplication::shutdown()
    {
        if (m_renderer2D)
        {
            m_renderer2D.reset();
        }

        // if (m_guiSystem)
        // {
        //     m_guiSystem.reset();
        // }

        if (m_window)
        {
            m_window.reset();
        }
    }
} // Tina
