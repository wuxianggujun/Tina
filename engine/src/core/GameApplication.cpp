//
// Created by wuxianggujun on 2024/7/12.
//

#include "GameApplication.hpp"
#include "math/Vector.hpp"

namespace Tina {
    GameApplication::GameApplication(const Path &configFilePath): CoreApplication(configFilePath) {
        m_eventHandler = CoreApplication::createEventHandler();
        m_resourceManager = createScopePtr<ResourceManager>();
    }

    GameApplication::~GameApplication() = default;

    void GameApplication::initialize() {
        // 创建 WindowConfig
        IWindow::WindowConfig windowConfig;
        // 从配置文件中读取窗口配置
        // 假设你的 Config 类提供了获取窗口配置的方法
        windowConfig.title = "Tina Engine";          // 可以换成类似于 m_config->getWindowConfig().title
        windowConfig.resolution.width = 1280;   // m_config->getWindowConfig().resolution.width;
        windowConfig.resolution.height = 720;   // m_config->getWindowConfig().resolution.height;
        windowConfig.resizable = true;          // m_config->getWindowConfig().resizable;
        windowConfig.maximized = false;         // m_config->getWindowConfig().maximized;
        windowConfig.vsync = true;              // m_config->getWindowConfig().vsync;

        m_window = createWindow(windowConfig);
        m_window->setEventHandler(std::move(m_eventHandler));

        m_guiSystem = createGuiSystem();
        m_guiSystem->initialize(windowConfig.resolution.width, windowConfig.resolution.height);
    }

    void GameApplication::run() {
        initialize();
        mainLoop();
    }

    void GameApplication::mainLoop() {
        while (!m_window->shouldClose()) {
            const auto currentTime = static_cast<float>(glfwGetTime());
            const float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            update(deltaTime);
            render();

            m_window->pollEvents();
        }
    }
    

    void GameApplication::update(float deltaTime) {
        m_guiSystem->update(m_registry, deltaTime);
    }

    void GameApplication::render() {
        // 从 IWindow 获取分辨率
        const Vector2i resolution = m_window->getResolution();
        
        bgfx::begin();
        // 设置视口
        bgfx::setViewClear(0,
                           BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           0x303030ff, // 背景色
                           1.0f, // 深度清除值
                           0 // 模板清除值
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
        auto &button = m_registry.emplace<GuiComponent>(buttonEntity);
        button.position = glm::vec2(100.0f, 100.0f);
        button.size = glm::vec2(200.0f, 50.0f);
        button.color = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);
        button.text = "Click Me!";

        auto panelEntity = m_registry.create();
        auto &panel = m_registry.emplace<GuiComponent>(panelEntity);
        panel.position = glm::vec2(400.0f, 100.0f);
        panel.size = glm::vec2(300.0f, 200.0f);
        panel.color = glm::vec4(0.3f, 0.3f, 0.3f, 0.8f);
    }
} // Tina
