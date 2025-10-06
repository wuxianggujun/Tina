//
// Application - 应用程序主类
// - 职责：管理应用程序生命周期、封装窗口和bgfx、提供全局服务访问
// - 设计：引擎架构的入口点，协调场景管理器和事件总线
//

#pragma once

#include "../core/Memory.hpp"
#include "../os/OS.hpp"

namespace Tina::Engine {

// 前向声明
class SceneManager;
class EventBus;

// 应用程序主类
class Application {
public:
    // 应用程序配置
    struct Config {
        int windowWidth = 1280;            // 窗口宽度
        int windowHeight = 720;            // 窗口高度
        const char* windowTitle = "Tina";  // 窗口标题
        bool vsync = true;                 // 垂直同步
        uint32_t msaa = 8;                 // 多重采样抗锯齿
    };

    explicit Application(const Config& config);
    ~Application();

    // 禁止拷贝和移动
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // ==================== 主循环控制 ====================

    // 启动主循环（阻塞直到quit()被调用或所有场景退出）
    void run();

    // 退出主循环
    void quit() { m_running = false; }

    // 查询主循环是否在运行
    bool isRunning() const { return m_running; }

    // ==================== 访问核心系统 ====================

    // 获取窗口句柄
    Tina::os::WindowHandle window() const { return m_window; }

    // 获取事件总线（用于订阅事件）
    EventBus& events() const { return *m_eventBus; }

    // 获取场景管理器（用于场景切换）
    SceneManager& scenes() const { return *m_sceneManager; }

    // ==================== 帧率和时间信息 ====================

    // 获取上一帧的时间间隔（秒）
    float deltaTime() const { return m_deltaTime; }

    // 获取当前帧率（FPS）
    float fps() const { return m_fps; }

    // ==================== 窗口信息 ====================

    // 获取窗口宽度（像素）
    int windowWidth() const { return m_pixelWidth; }

    // 获取窗口高度（像素）
    int windowHeight() const { return m_pixelHeight; }

    // 获取窗口像素尺寸（用于渲染器和UI）
    void getPixelSize(int& outW, int& outH) const {
        outW = m_pixelWidth;
        outH = m_pixelHeight;
    }

private:
    void init();                    // 初始化（窗口、bgfx、子系统）
    void shutdown();                // 清理（场景、bgfx、窗口）
    void processEvents();           // 处理操作系统事件
    void update(float dt);          // 更新逻辑
    void render();                  // 渲染场景

private:
    Config m_config;                               // 应用程序配置
    bool m_running = false;                        // 主循环运行标志
    float m_deltaTime = 0.0f;                      // 帧时间间隔（秒）
    float m_fps = 60.0f;                           // 当前帧率
    uint64_t m_lastFrameTime = 0;                  // 上一帧时间戳（用于计算deltaTime）

    int m_pixelWidth = 1280;                       // 窗口像素宽度
    int m_pixelHeight = 720;                       // 窗口像素高度

    Tina::os::WindowHandle m_window = nullptr;     // 窗口句柄
    Memory::UniquePtr<EventBus> m_eventBus;        // 事件总线（独占所有权）
    Memory::UniquePtr<SceneManager> m_sceneManager;// 场景管理器（独占所有权）
};

} // namespace Tina::Engine
