#include <iostream>

#include "tina/log/Log.hpp"
#include "tina/delegate/Delegate.hpp"
#include "tina/window/WindowManager.hpp"
#include "tina/window/Window.hpp"
#include "tina/event/Event.hpp"
#include "tina/event/EventManager.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

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

int main()
{
    // 初始化日志系统
    Tina::Log::init();
    
    // 创建窗口管理器
    WindowManager windowManager;
    if (!windowManager.initialize()) {
        TINA_ENGINE_ERROR("Failed to initialize WindowManager");
        return -1;
    }

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
    windowConfig.title = "Tina Engine Demo";
    windowConfig.width = 1280;
    windowConfig.height = 720;
    windowConfig.vsync = true;
    windowConfig.fullscreen = false;

    // 创建窗口
    const WindowHandle windowHandle = windowManager.createWindow(windowConfig);
    if (!isValid(windowHandle)) {
        TINA_ENGINE_ERROR("Failed to create window");
        return -1;
    }

    // 获取窗口指针
    Window* window = windowManager.getWindow(windowHandle);
    if (!window) {
        TINA_ENGINE_ERROR("Failed to get window pointer");
        return -1;
    }

    TINA_ENGINE_INFO("Window created successfully");
    
    // 主循环
    while (!window->shouldClose()) {
        // 处理事件
        windowManager.pollEvents();

        // 处理窗口消息
        windowManager.processMessage();

        // 渲染
        bgfx::touch(0);
        
        // 提交渲染
        bgfx::frame();

        // 处理输入
        if (Window::isKeyPressed(GLFW_KEY_ESCAPE)) {
            window->close();
        }
    }

    // 关闭bgfx
    bgfx::shutdown();

    // 清理资源
    windowManager.destroyWindow(windowHandle);
    // 关闭日志系统
    Tina::Log::shutdown();
    
    return 0;
}
