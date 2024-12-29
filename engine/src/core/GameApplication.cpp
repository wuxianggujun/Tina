//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"

namespace Tina {
    
    GameApplication::GameApplication(ScopePtr<IWindow> &&window,
        ScopePtr<IGuiSystem> &&guiSystem, ScopePtr<EventHandler> &&eventHandler):
        m_window(std::move(window)), m_guiSystem(std::move(guiSystem)),
        m_eventHandler(std::move(eventHandler)),m_resourceManager(createScopePtr<ResourceManager>()) {
        Vector2i resolution = m_window->getResolution();
        m_window->setEventHandler(std::move(m_eventHandler));
        m_guiSystem->initialize(resolution.width, resolution.height);
        lastFrameTime = static_cast<float>(glfwGetTime());
    }

    GameApplication::~GameApplication() {
        shutdown();
    }

    void GameApplication::initialize() {
        // createTestGui();
        // 创建 WindowConfig
        Tina::IWindow::WindowConfig config;
        config.title = "Tina Engine";
        config.resolution = {1280, 720};
        config.resizable = true;
        config.maximized = false;
        config.vsync = true;

        m_window->create(config);
    }

    void GameApplication::run() {
        initialize();
        mainLoop();
    }

    void GameApplication::mainLoop() {
        while (!m_window->shouldClose()) {
            auto currentTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            update(deltaTime);
            render();

            m_window->pollEvents();
        }
    }

    void GameApplication::shutdown() {
        if (m_guiSystem) {
            m_guiSystem->shutdown();
        }
        if (m_window) {
            m_window->destroy();
        }
    }

    void GameApplication::update(float deltaTime) {
        m_guiSystem->update(m_registry,deltaTime);
    }

    void GameApplication::render() {
        // 从 IWindow 获取分辨率
        Vector2i resolution = m_window->getResolution();
        bgfx::begin();
        // 设置视口
        bgfx::setViewClear(0,
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x303030ff, // 背景色
            1.0f,  // 深度清除值
            0      // 模板清除值
        );
        
        // 设置视口大小
        bgfx::setViewRect(0, 0, 0, 
            resolution.width, 
            resolution.height
        );
        
        // 确保每帧都触发
        bgfx::touch(0);

        if (m_window) {
            m_window->render();
        }
        // 渲染GUI
        m_guiSystem->render(m_registry);

        if (m_window) {
            m_window->frame(); // 添加一个 frame 接口在 IWindow 中
        }
    }

    void GameApplication::createTestGui() {
        // 创建测试GUI元素
        auto buttonEntity = m_registry.create();
        auto& button = m_registry.emplace<GuiComponent>(buttonEntity);
        button.position = glm::vec2(100.0f, 100.0f);
        button.size = glm::vec2(200.0f, 50.0f);
        button.color = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);
        button.text = "Click Me!";

        auto panelEntity = m_registry.create();
        auto& panel = m_registry.emplace<GuiComponent>(panelEntity);
        panel.position = glm::vec2(400.0f, 100.0f);
        panel.size = glm::vec2(300.0f, 200.0f);
        panel.color = glm::vec4(0.3f, 0.3f, 0.3f, 0.8f);
    }
} // Tina
