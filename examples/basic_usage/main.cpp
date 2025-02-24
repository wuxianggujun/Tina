#include <iostream>
#include <bgfx/bgfx.h>

#include "tina/log/Log.hpp"
#include "tina/delegate/Delegate.hpp"
#include "tina/window/WindowManager.hpp"
#include "tina/window/Window.hpp"
#include "tina/event/Event.hpp"
#include "tina/event/EventManager.hpp"
#include "tina/resource/ResourceManager.hpp"
#include "tina/resource/ShaderResource.hpp"
#include "tina/resource/ShaderLoader.hpp"
#include "tina/resource/TextureResource.hpp"
#include "tina/resource/TextureLoader.hpp"

using namespace Tina;

// 事件处理类
class EventHandler {
public:
    void onWindowCreated(const EventPtr& event) {
        TINA_ENGINE_INFO("Window created: {}x{}", 
            event->windowCreate.width, 
            event->windowCreate.height);
    }

    void onWindowResized(const EventPtr& event) {
        TINA_ENGINE_INFO("Window resized: {}x{}", 
            event->windowResize.width, 
            event->windowResize.height);
        bgfx::reset(event->windowResize.width, event->windowResize.height, BGFX_RESET_VSYNC);
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(event->windowResize.width), uint16_t(event->windowResize.height));
    }

    void onKeyPressed(const EventPtr& event) {
        if (event->key.action == GLFW_PRESS) {
            TINA_ENGINE_INFO("Key pressed: {}", static_cast<int>(event->key.key));
        }
    }

    void onMouseMoved(const EventPtr& event) {
        TINA_ENGINE_DEBUG("Mouse moved: ({}, {})", 
            event->mousePos.x, event->mousePos.y);
    }
};

class ExampleApp {
public:
    ExampleApp() {
        m_windowManager = WindowManager::getInstance();
        m_eventManager = EventManager::getInstance();
        m_resourceManager = ResourceManager::getInstance();
    }

    bool initialize() {
        // 初始化日志系统
        Log::init();
        
        // 创建窗口管理器
        if (!m_windowManager->initialize()) {
            TINA_ENGINE_ERROR("Failed to initialize WindowManager");
            return false;
        }

        // 注册资源加载器
        m_resourceManager->registerLoader(std::make_unique<ShaderLoader>());
        m_resourceManager->registerLoader(std::make_unique<TextureLoader>());

        // 创建事件处理器
        EventHandler eventHandler;

        // 获取事件管理器
        auto* eventManager = EventManager::getInstance();

        // 注册事件处理函数
        eventManager->addListener(
            [&eventHandler](const EventPtr& event) { eventHandler.onWindowCreated(event); },
            Event::WindowCreate
        );
        
        eventManager->addListener(
            [&eventHandler](const EventPtr& event) { eventHandler.onWindowResized(event); },
            Event::WindowResize
        );
        
        eventManager->addListener(
            [&eventHandler](const EventPtr& event) { eventHandler.onKeyPressed(event); },
            Event::Key
        );
        
        eventManager->addListener(
            [&eventHandler](const EventPtr& event) { eventHandler.onMouseMoved(event); },
            Event::MouseMove
        );

        // 配置窗口
        Window::WindowConfig windowConfig;
        windowConfig.title = "Tina Engine Example";
        windowConfig.width = 1280;
        windowConfig.height = 720;
        windowConfig.vsync = true;
        windowConfig.fullscreen = false;

        // 创建窗口
        m_windowHandle = m_windowManager->createWindow(windowConfig);
        if (!isValid(m_windowHandle)) {
            TINA_ENGINE_ERROR("Failed to create window");
            return false;
        }

        // 获取窗口指针
        m_window = m_windowManager->getWindow(m_windowHandle);
        if (!m_window) {
            TINA_ENGINE_ERROR("Failed to get window pointer");
            return false;
        }

        TINA_ENGINE_INFO("Window created successfully");
        
        // 设置视口
        bgfx::setViewRect(0, 0, 0, windowConfig.width, windowConfig.height);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

        // 加载着色器
        m_shader = m_resourceManager->loadSync<ShaderResource>("2d", "resources/shaders/");
        if (!m_shader || !m_shader->isLoaded()) {
            TINA_ENGINE_ERROR("Failed to load shader");
            return false;
        }

        m_texture = m_resourceManager->loadSync<TextureResource>("test.png","resources/textures/");
        if (!m_texture || !m_texture->isLoaded()) {
            TINA_ENGINE_ERROR("Failed to load texture");
            return false;
        }
        
        // 创建顶点布局
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
            .end();

        // 创建顶点缓冲
        float vertices[] = {
            // pos      uv      color
            -0.5f, -0.5f,  0.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f, // 左下
             0.5f, -0.5f,  1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f, // 右下
             0.5f,  0.5f,  1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f, // 右上
            -0.5f,  0.5f,  0.0f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f  // 左上
        };

        m_vbh = bgfx::createVertexBuffer(
            bgfx::copy(vertices, sizeof(vertices)),
            layout
        );

        // 创建索引缓冲
        uint16_t indices[] = {
            0, 1, 2, // 第一个三角形
            2, 3, 0  // 第二个三角形
        };

        m_ibh = bgfx::createIndexBuffer(
            bgfx::copy(indices, sizeof(indices))
        );

        // 创建uniform变量
        m_useTexture = bgfx::createUniform("u_useTexture", bgfx::UniformType::Vec4);
        m_textureSampler = bgfx::createUniform("u_texColor", bgfx::UniformType::Sampler);
        return true;
    }

    void run() {
        while (!m_window->shouldClose()) {
            // 处理事件
            m_windowManager->processMessage();

            // 设置渲染状态
            bgfx::touch(0);

            // 设置uniform变量
            float useTexture[4] = { 1.0f, 0.0f, 0.0f, 0.0f }; // 启用纹理
            bgfx::setUniform(m_useTexture, useTexture);

            // 设置纹理和采样器
            if (m_texture && m_texture->isLoaded()) {
                // 设置纹理单元0的纹理和采样器状态
                bgfx::setTexture(0, m_textureSampler, m_texture->getHandle(), 
                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | 
                    BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
            }

            // 设置着色器程序
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | 
                BGFX_STATE_BLEND_ALPHA);

            // 提交绘制命令
            bgfx::setVertexBuffer(0, m_vbh);
            bgfx::setIndexBuffer(m_ibh);
            bgfx::submit(0, m_shader->getProgram());

            // 提交帧
            bgfx::frame();
        }
    }

    void shutdown() {
        // 释放shader资源
        if (m_shader) {
            TINA_ENGINE_DEBUG("Releasing shader reference in ExampleApp::shutdown");
            m_shader.reset();
        }

        // 释放纹理资源
        if (m_texture) {
            TINA_ENGINE_DEBUG("Releasing texture reference in ExampleApp::shutdown");
            m_texture.reset();
        }

        // 等待所有资源释放完成
        bgfx::frame();

        // 关闭资源管理器
        m_resourceManager->shutdown();

        // 销毁顶点和索引缓冲
        if (bgfx::isValid(m_vbh)) {
            bgfx::destroy(m_vbh);
        }
        if (bgfx::isValid(m_ibh)) {
            bgfx::destroy(m_ibh);
        }
        if (bgfx::isValid(m_useTexture)) {
            bgfx::destroy(m_useTexture);
        }
        if (bgfx::isValid(m_textureSampler)) {
            bgfx::destroy(m_textureSampler);
        }

        // 关闭bgfx
        bgfx::shutdown();

        // 销毁窗口
        if (isValid(m_windowHandle)) {
            m_windowManager->destroyWindow(m_windowHandle);
        }

        // 关闭日志系统
        Log::shutdown();
    }

private:
    WindowManager* m_windowManager{nullptr};
    EventManager* m_eventManager{nullptr};
    ResourceManager* m_resourceManager{nullptr};
    WindowHandle m_windowHandle;
    Window* m_window{nullptr};

    RefPtr<ShaderResource> m_shader;  // 保持shader资源的引用
    RefPtr<TextureResource> m_texture;  // 保持纹理资源的引用
    bgfx::VertexBufferHandle m_vbh{BGFX_INVALID_HANDLE};
    bgfx::IndexBufferHandle m_ibh{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle m_useTexture{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle m_textureSampler{BGFX_INVALID_HANDLE};
};

int main() {
    ExampleApp app;
    
    if (!app.initialize()) {
        TINA_ENGINE_ERROR("Failed to initialize application");
        return -1;
    }

    app.run();
    app.shutdown();

    return 0;
}
