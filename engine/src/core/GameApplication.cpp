//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"

namespace Tina {
    GameApplication::GameApplication() {
        Vector2i resolution = Vector2i(1280, 720);
        window = createScopePtr<GLFWWindow>();
        guiSystem = createScopePtr<GuiSystem>();
        eventHandler = createScopePtr<EventHandler>();
        window->create(Window::WindowConfig{"Tina", resolution, false, false, false});
        window->setEventHandler(std::move(eventHandler));
        renderer = createScopePtr<Renderer>(resolution, 0);

        guiSystem->Initialize(resolution.width, resolution.height);
        lastFrameTime = static_cast<float>(glfwGetTime());
    }

    GameApplication::~GameApplication() {
        shutdown();
    }

    void GameApplication::initialize() {
        createTestGui();
    }

    void GameApplication::run() {
        initialize();
        mainLoop();
    }

    void GameApplication::mainLoop() {
        while (!window->shouldClose()) {
            float currentTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            update(deltaTime);
            render();

            window->pollEvents();
        }
        renderer->shutdown();
        window->destroy();
    }

    void GameApplication::shutdown() {
        if (guiSystem) {
            guiSystem->Shutdown();
        }
        if (renderer) {
            renderer->shutdown();
        }
        if (window) {
            window->destroy();
        }
    }

    void GameApplication::update(float deltaTime) {
        guiSystem->Update(registry,deltaTime);
    }

    void GameApplication::render() {
        // 设置视口
        bgfx::setViewClear(0,
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            0x303030ff, // 背景色
            1.0f,  // 深度清除值
            0      // 模板清除值
        );
        
        // 设置视口大小
        bgfx::setViewRect(0, 0, 0, 
            static_cast<uint16_t>(1280), 
            static_cast<uint16_t>( 720)
        );
        
        // 确保每帧都触发
        bgfx::touch(0);
        
        // 渲染3D场景
        renderer->render();
        
        // 渲染GUI
        guiSystem->Render(registry);
        
        // 提交帧
        renderer->frame();
    }

    void GameApplication::createTestGui() {
        // 创建测试GUI元素
        auto buttonEntity = registry.create();
        auto& button = registry.emplace<GuiComponent>(buttonEntity);
        button.position = glm::vec2(100.0f, 100.0f);
        button.size = glm::vec2(200.0f, 50.0f);
        button.color = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);
        button.text = "Click Me!";
        
        auto panelEntity = registry.create();
        auto& panel = registry.emplace<GuiComponent>(panelEntity);
        panel.position = glm::vec2(400.0f, 100.0f);
        panel.size = glm::vec2(300.0f, 200.0f);
        panel.color = glm::vec4(0.3f, 0.3f, 0.3f, 0.8f);
    }
} // Tina
