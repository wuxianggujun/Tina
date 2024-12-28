//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"

namespace Tina {
    
    GameApplication::GameApplication(ScopePtr<IWindow> &&window, ScopePtr<IRenderer> &&renderer,
        ScopePtr<IGuiSystem> &&guiSystem, ScopePtr<EventHandler> &&eventHandler):
        m_window(std::move(window)), m_renderer(std::move(renderer)), m_guiSystem(std::move(guiSystem)),
        m_eventHandler(std::move(eventHandler)),m_resourceManager(createScopePtr<ResourceManager>()) {
        Vector2i resolution = Vector2i(1280, 720);
        IWindow::WindowConfig config("Tina", resolution, false, false, false);
        m_window->create(config);
        m_window->setEventHandler(std::move(m_eventHandler));
        m_renderer->init(resolution);

        m_guiSystem->initialize(resolution.width, resolution.height);
        lastFrameTime = static_cast<float>(glfwGetTime());
    }

    GameApplication::~GameApplication() {
        shutdown();
    }

    void GameApplication::initialize() {
        // createTestGui();
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
        if (m_renderer) {
            m_renderer->shutdown();
        }
        if (m_window) {
            m_window->destroy();
        }
    }

    void GameApplication::update(float deltaTime) {
        m_guiSystem->update(m_registry,deltaTime);
    }

    void GameApplication::render() {
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
            1280, 
            720
        );
        
        // 确保每帧都触发
        bgfx::touch(0);
        
        // 渲染3D场景
        m_renderer->render();

        // 渲染GUI
        m_guiSystem->render(m_registry);

        // 提交帧
        m_renderer->frame();
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
