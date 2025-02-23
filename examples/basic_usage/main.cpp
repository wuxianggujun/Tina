#include <iostream>

#include "tina/log/Log.hpp"
#include "tina/delegate/Delegate.hpp"
#include "tina/window/WindowManager.hpp"
#include "tina/window/Window.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

using namespace Tina;

// 事件处理类
class EventHandler {
public:
    void onWindowCreated(const WindowEventData& data) {
        TINA_ENGINE_INFO("Window created: {}x{}", data.width, data.height);
    }

    void onWindowResized(const WindowEventData& data) {
        TINA_ENGINE_INFO("Window resized: {}x{}", data.width, data.height);
        bgfx::reset(data.width, data.height, BGFX_RESET_VSYNC);
        bgfx::setViewRect(0, 0, 0, data.width, data.height);
    }

    void onKeyPressed(const KeyEventData& data) {
        if (data.action == GLFW_PRESS) {
            TINA_ENGINE_INFO("Key pressed: {}", data.key);
        }
    }

    void onMouseMoved(const MouseEventData& data) {
        TINA_ENGINE_DEBUG("Mouse moved: ({}, {})", data.x, data.y);
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

    // 注册事件处理函数
    windowManager.onWindowCreate.bind<EventHandler, &EventHandler::onWindowCreated>(&eventHandler);
    windowManager.onWindowResize.bind<EventHandler, &EventHandler::onWindowResized>(&eventHandler);
    windowManager.onKeyEvent.bind<EventHandler, &EventHandler::onKeyPressed>(&eventHandler);
    windowManager.onMouseMove.bind<EventHandler, &EventHandler::onMouseMoved>(&eventHandler);

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
