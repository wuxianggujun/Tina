//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"
#include "graphics/Color.hpp"
#include "window/GLFWWindow.hpp"
#include "core/Config.hpp"
#include "core/RenderSystem.hpp"
#include "core/components/RenderComponents.hpp"
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
        GameApplication::shutdown();
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
                fmt::print("Failed to load config: {}\n", e.what());
            }
        }

        // 创建窗口
        m_window = std::make_unique<GLFWWindow>();
        m_window->create(windowConfig);
        m_window->addResizeListener(this);

        // 初始化渲染系统
        auto& renderSystem = RenderSystem::getInstance();
        renderSystem.initialize();

        // 创建正交相机
        float width = static_cast<float>(windowConfig.resolution.width);
        float height = static_cast<float>(windowConfig.resolution.height);
        m_camera = std::make_unique<OrthographicCamera>(
            0.0f,    // left
            width,   // right
            height,  // bottom
            0.0f,    // top
            -1.0f,   // near
            1.0f     // far
        );

        // 设置相机位置
        m_camera->setPosition(Vector3f(0.0f, 0.0f, 0.0f));
        m_camera->setTarget(Vector3f(0.0f, 0.0f, -1.0f));

        // 设置渲染系统的相机
        renderSystem.setCamera(m_camera.get());
        
        // 设置视口
        renderSystem.setViewport(0, 0, windowConfig.resolution.width, windowConfig.resolution.height);

        // 创建示例实体
        createExampleEntities();
    }

    void GameApplication::createExampleEntities() {
        // 创建红色矩形
        {
            auto entity = m_registry.create();
            auto& transform = m_registry.emplace<TransformComponent>(entity);
            transform.position = Vector2f(100.0f, 100.0f);
            
            auto& quad = m_registry.emplace<QuadRendererComponent>(entity);
            quad.color = Color::Red;
            quad.size = Vector2f(100.0f, 100.0f);
            quad.depth = 0.0f;
        }

        // 创建绿色矩形
        {
            auto entity = m_registry.create();
            auto& transform = m_registry.emplace<TransformComponent>(entity);
            transform.position = Vector2f(250.0f, 100.0f);
            
            auto& quad = m_registry.emplace<QuadRendererComponent>(entity);
            quad.color = Color::Green;
            quad.size = Vector2f(100.0f, 100.0f);
            quad.depth = 0.1f;
        }

        // 创建蓝色矩形
        {
            auto entity = m_registry.create();
            auto& transform = m_registry.emplace<TransformComponent>(entity);
            transform.position = Vector2f(400.0f, 100.0f);
            
            auto& quad = m_registry.emplace<QuadRendererComponent>(entity);
            quad.color = Color::Blue;
            quad.size = Vector2f(100.0f, 100.0f);
            quad.depth = 0.2f;
        }

        // 创建白色矩形
        {
            auto entity = m_registry.create();
            auto& transform = m_registry.emplace<TransformComponent>(entity);
            transform.position = Vector2f(550.0f, 100.0f);
            
            auto& quad = m_registry.emplace<QuadRendererComponent>(entity);
            quad.color = Color::White;
            quad.size = Vector2f(100.0f, 100.0f);
            quad.depth = 0.3f;
        }
    }

    void GameApplication::onWindowResize(int width, int height)
    {
        // 更新相机投影
        if (m_camera)
        {
            m_camera->setProjection(
                0.0f, // left
                static_cast<float>(width), // right
                static_cast<float>(height), // bottom
                0.0f, // top
                -1.0f, // near
                1.0f // far
            );
        }
        
        // 更新渲染系统的视口
        RenderSystem::getInstance().setViewport(0, 0, width, height);
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
        // 子类实现具体的更新逻辑
    }

    void GameApplication::render()
    {
        auto& renderSystem = RenderSystem::getInstance();
        
        // 开始渲染帧
        renderSystem.beginFrame();

        // 设置清屏颜色
        renderSystem.setClearColor(Color(0.2f, 0.2f, 0.2f, 1.0f));

        // 使用ECS渲染所有实体
        renderSystem.render(m_registry);

        // 结束渲染帧
        renderSystem.endFrame();
    }

    void GameApplication::shutdown()
    {
        // 清除所有实体
        m_registry.clear();
        
        // 关闭渲染系统
        RenderSystem::getInstance().shutdown();

        if (m_window)
        {
            m_window->removeResizeListener(this);
            m_window.reset();
        }
    }

    void GameApplication::run()
    {
        initialize();
        mainLoop();
    }
} // Tina
