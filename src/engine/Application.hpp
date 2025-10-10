//
// Application - 应用程序主类
// - 职责：管理应用程序生命周期、封装窗口和bgfx、提供全局服务访问
// - 设计：引擎架构的入口点，协调场景管理器和事件总线
//

#pragma once

#include "../core/Memory.hpp"
#include "../os/OS.hpp"
#include "../renderer/ShaderManager.hpp"
#include <functional>
#include <mutex>

// 在全局命名空间前向声明 SDL_mixer 的不透明结构体，避免命名空间污染
struct MIX_Mixer;

// 前向声明：UI 文本渲染器（避免在头文件中包含重量级依赖）
namespace Tina { namespace UI { class TextRenderer; } }

namespace Tina::Engine {

// 前向声明
class SceneManager;
class OSEventBus; // OS 事件总线（窗口/输入）
class TypedEventBus; // 强类型事件总线（玩法/编辑器/插件）
struct FileSystem;
class ResourceManagerHub;
class TextureManager;
class FontManager;
class AudioManager;


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

    // 获取 OS 事件总线（窗口/输入等稳定事件）
    OSEventBus& osEvents() const { return *m_osEventBus; }

    // 获取强类型事件总线（游戏/编辑器/插件的自定义事件）
    TypedEventBus& events() const { return *m_typedEvents; }

    // 获取场景管理器（用于场景切换）
    SceneManager& scenes() const { return *m_sceneManager; }

    // 获取文件系统（用于异步IO）
    FileSystem& fileSystem() const { return *m_fileSystem; }

    // 获取资源管理中心（用于加载资源）
    ResourceManagerHub& resources() const { return *m_resourceHub; }

    // 获取全局着色器管理器（全局唯一，贯穿应用生命周期）
    Tina::Renderer::ShaderManager& shaders() const { return *m_shaderMgr; }

    // 获取音频管理器（用于加载和播放音频）
    AudioManager& audio() const { return *m_audioMgr; }

    // ==================== 文本渲染器 ====================
    // 全局 TextRenderer，避免每个 Scene 重复创建与加载字体
    Tina::UI::TextRenderer& textRenderer() const;

    // 全局音量控制（0.0 ~ 1.0）
    void setAudioMasterVolume(float v);
    float getAudioMasterVolume() const { return m_audioMasterVolume; }

    // 分组音量（music / sfx），范围 0.0 ~ 1.0
    void setMusicVolume(float v);
    float getMusicVolume() const { return m_audioMusicVolume; }
    void setSfxVolume(float v);
    float getSfxVolume() const { return m_audioSfxVolume; }

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
    void prewarmCommonAssets();     // 预热常用资源（字体/图标），减少首帧等待

public:
    // 任务队列（线程安全）：将任务投递到主线程安全点执行
    // 用途：
    // - 在任意线程/回调中请求在主线程操作（如场景切换、UI修改、bgfx调用）
    // - 与 SceneManager 的 request* 协同使用，统一在帧末或事件分发后执行
    void post(std::function<void()> fn);
    void postDelayed(uint32_t delayMs, std::function<void()> fn);

private:
    Config m_config;                               // 应用程序配置
    bool m_running = false;                        // 主循环运行标志
    float m_deltaTime = 0.0f;                      // 帧时间间隔（秒）
    float m_fps = 60.0f;                           // 当前帧率
    uint64_t m_lastFrameTime = 0;                  // 上一帧时间戳（用于计算deltaTime）

    int m_pixelWidth = 1280;                       // 窗口像素宽度
    int m_pixelHeight = 720;                       // 窗口像素高度

    Tina::os::WindowHandle m_window = nullptr;     // 窗口句柄
    Memory::UniquePtr<OSEventBus> m_osEventBus;            // OS 事件总线
    Memory::UniquePtr<TypedEventBus> m_typedEvents;        // 强类型事件总线
    Memory::UniquePtr<SceneManager> m_sceneManager;        // 场景管理器（独占所有权）
    Memory::UniquePtr<FileSystem> m_fileSystem;            // 文件系统（异步IO）
    Memory::UniquePtr<ResourceManagerHub> m_resourceHub;   // 资源管理中心
    Memory::UniquePtr<Tina::Renderer::ShaderManager> m_shaderMgr; // 全局着色器管理器
    Memory::UniquePtr<TextureManager> m_textureMgr;        // 纹理资源管理器
    Memory::UniquePtr<FontManager> m_fontMgr;              // 字体资源管理器
    Memory::UniquePtr<AudioManager> m_audioMgr;            // 音频资源管理器

    // SDL_mixer 3.x 混音器（输出到默认音频设备）
    // 全局文本渲染器（共享）
    Memory::UniquePtr<Tina::UI::TextRenderer> m_textRenderer;

    MIX_Mixer* m_mixer = nullptr;

    // 全局音量
    float m_audioMasterVolume = 1.0f;
    float m_audioMusicVolume = 1.0f;
    float m_audioSfxVolume = 1.0f;

    // 任务队列（主线程执行）
    std::mutex m_taskMutex;
    Tina::Container::Vector<std::function<void()>> m_tasks;
    void flushTasks();
    struct TimedTask { uint64_t dueMs = 0; std::function<void()> fn; };
    Tina::Container::Vector<TimedTask> m_timedTasks;
    void flushTimedTasks();
};

} // namespace Tina::Engine
