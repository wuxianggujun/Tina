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
        m_renderer2D = std::make_unique<Renderer2D>(0);  // 使用视图0
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

            // 提交帧
            bgfx::frame();

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
            fmt::print("Window size: {}x{}\n", width, height);

            // 确保视图0被清除
            bgfx::touch(0);

            // 渲染2D内容
            if (m_renderer2D)
            {
                m_renderer2D->begin();
                
                // 使用RGBA格式的颜色
                uint32_t red   = 0xffff0000;  // Alpha=ff, Red=ff, Green=00, Blue=00
                uint32_t green = 0xff00ff00;  // Alpha=ff, Red=00, Green=ff, Blue=00
                uint32_t blue  = 0xff0000ff;  // Alpha=ff, Red=00, Green=00, Blue=ff
                uint32_t white = 0xffffffff;  // Alpha=ff, Red=ff, Green=ff, Blue=ff

                // 绘制一些矩形
                m_renderer2D->drawRect({100, 100}, {100, 100}, Color::Red);
                m_renderer2D->drawRect({200, 100}, {100, 100}, Color(1.0f, 0.0f, 0.0f, 0.5f));  // 半透明红色
                m_renderer2D->drawRect({250, 100}, {100, 100}, Color::Green);
                m_renderer2D->drawRect({400, 100}, {100, 100}, Color::Blue);
                m_renderer2D->drawRect({550, 100}, {100, 100}, Color::White);

                m_renderer2D->end();
            }
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
