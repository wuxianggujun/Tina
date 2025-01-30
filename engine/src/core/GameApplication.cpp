//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"
#include "graphics/Color.hpp"
#include "window/GLFWWindow.hpp"
#include "core/Config.hpp"
#include "core/RenderSystem.hpp"
#include "components/RenderComponents.hpp"
#include <bgfx/bgfx.h>
#include <fmt/format.h>
#include "bx/math.h"
#include "graphics/Sprite.hpp"

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
            0.0f, // left
            width, // right
            0.0f, // bottom
            height, // top
            -1.0f, // near
            1.0f // far
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

        Texture iconTexture(Texture::loadFromFile("../resources/textures/player.png"));
        // 创建玩家精灵
        auto player = m_scene.createSprite(
            iconTexture,
            Vector2f(100.0f, 100.0f), // 位置
            Vector2f(64.0f, 64.0f) // 大小
        );

        // 获取并修改组件
        auto& transform = m_scene.getComponent<TransformComponent>(player);
        transform.rotation = 0.0f;

        auto& spriteComp = m_scene.getComponent<SpriteComponent>(player);
        spriteComp.depth = 1.0f;
        // 设置旋转中心为精灵的中心点
        Vector2f spriteSize(64.0f, 64.0f);
        spriteComp.sprite.setOrigin(spriteSize * 0.5f);  // 使用精灵大小的一半作为旋转中心
    }

    void GameApplication::createExampleEntities()
    {
        // 创建红色矩形
        m_scene.createQuad(
            Vector2f(100.0f, 100.0f),
            Vector2f(100.0f, 100.0f),
            Color::Red
        );

        // 创建绿色矩形
        m_scene.createQuad(
            Vector2f(250.0f, 100.0f),
            Vector2f(100.0f, 100.0f),
            Color::Green
        );

        // 创建蓝色矩形
        m_scene.createQuad(
            Vector2f(400.0f, 100.0f),
            Vector2f(100.0f, 100.0f),
            Color::Blue
        );

        // 创建白色矩形
        m_scene.createQuad(
            Vector2f(550.0f, 100.0f),
            Vector2f(100.0f, 100.0f),
            Color::White
        );
    }

    void GameApplication::onWindowResize(int width, int height)
    {
        // 更新相机投影
        if (m_camera)
        {
            m_camera->setProjection(
                0.0f, // left
                static_cast<float>(width), // right
                0.0f, // bottom
                static_cast<float>(height), // top
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
        const auto view = m_scene.getRegistry().view<TransformComponent, SpriteComponent>();
        for (const auto entity : view)
        {
            auto& transform = view.get<TransformComponent>(entity);
            auto& sprite = view.get<SpriteComponent>(entity);

            // 使用较小的旋转速度
            const float rotationSpeed = 90.0f; // 每秒90度
            transform.rotation = std::fmod(transform.rotation + rotationSpeed * deltaTime, 360.0f);
            if (transform.rotation < 0)
            {
                transform.rotation += 360.0f;
            }

            fmt::print("Entity {} - Position: ({}, {}), Rotation: {}, Depth: {}\n",
                static_cast<uint32_t>(entity),
                transform.position.x,
                transform.position.y,
                transform.rotation,
                sprite.depth);
        }
    }

    void GameApplication::render()
    {
        auto& renderSystem = RenderSystem::getInstance();

        // 开始渲染帧
        renderSystem.beginFrame();

        // 设置清屏颜色
        renderSystem.setClearColor(Color(0.2f, 0.2f, 0.2f, 1.0f));

        // 使用ECS渲染所有实体
        renderSystem.render(m_scene.getRegistry());

        // 结束渲染帧
        renderSystem.endFrame();
    }

    void GameApplication::shutdown()
    {
        // 场景会在析构时自动清理所有实体
        m_scene.clear();

        m_camera.reset();

        // 关闭渲染系统
        RenderSystem::getInstance().shutdown();

        if (m_window)
        {
            m_window->removeResizeListener(this);
            m_window->destroy();
        }
        bgfx::shutdown();
    }

    void GameApplication::run()
    {
        initialize();
        mainLoop();
    }
} // Tina
