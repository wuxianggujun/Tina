//
// Application - 应用程序框架核心
// - 职责：管理窗口、bgfx、场景栈、输入、事件、资源等核心系统
// - 设计：框架核心类，通过IApplication接口支持用户扩展
// - 用法：创建Application实例，推入Scene，调用run()启动主循环
//

#pragma once

#include "../core/Memory.hpp"
#include "../renderer/ShaderManager.hpp"
#include <functional>
#include <mutex>

// 前向声明
namespace Tina { namespace UI { class TextRenderer; } }
namespace Tina { namespace Renderer { class Primitive2D; class SpriteRenderer; class ShaderManager; } }

namespace Tina::Engine {

// 前向声明
class Window;
class InputSystem;
class SceneManager;
class EventSystem;
struct FileSystem;
class ResourceManagerHub;
class TextureManager;
class FontManager;
class AudioManager;
class AudioEngine;
class IApplication;

/// 应用程序框架核心
/// 
/// **职责：**
/// - 管理窗口生命周期（创建、销毁、resize）
/// - 管理bgfx渲染器（初始化、reset、shutdown）
/// - 管理核心系统（场景、输入、事件、资源、音频）
/// - 运行主循环（事件处理、更新、渲染）
/// - 提供全局服务访问接口
/// 
/// **使用示例：**
/// ```cpp
/// Application::Config config;
/// Application app(nullptr, config);
/// app.scenes().push(MakeUnique<MenuScene>());
/// app.run();
/// ```
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

    /// 构造函数
    /// @param app 用户应用实例（可选，用于生命周期回调）
    /// @param config 应用配置
    explicit Application(IApplication* app, const Config& config);
    ~Application();

    // 禁止拷贝和移动
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;
    
    // 获取全局单例实例
    static Application* instance() { return s_instance; }

    // ==================== 主循环控制 ====================

    /// 启动主循环（阻塞直到quit()被调用或所有场景退出）
    void run();

    /// 退出主循环
    void quit() { m_running = false; }

    /// 查询主循环是否在运行
    bool isRunning() const { return m_running; }

    // ==================== 访问核心系统 ====================

    EventSystem& events() const { return *m_eventSystem; }
    EventSystem* getEventSystem() const { return m_eventSystem.get(); }
    
    SceneManager& scenes() const { return *m_sceneManager; }
    InputSystem& input() const { return *m_inputSystem; }
    Window& window() const { return *m_window; }
    FileSystem& fileSystem() const { return *m_fileSystem; }
    ResourceManagerHub& resources() const { return *m_resourceHub; }
    Tina::Renderer::ShaderManager& shaders() const { return *m_shaderMgr; }
    AudioManager& audio() const { return *m_audioMgr; }

    // 全局渲染器
    Tina::UI::TextRenderer& textRenderer() const;
    Tina::Renderer::Primitive2D& primitives2D() const;
    Tina::Renderer::SpriteRenderer& sprites2D() const;

    // 音频控制
    void setAudioMasterVolume(float v);
    float getAudioMasterVolume() const { return m_audioMasterVolume; }
    void setMusicVolume(float v);
    float getMusicVolume() const { return m_audioMusicVolume; }
    void setSfxVolume(float v);
    float getSfxVolume() const { return m_audioSfxVolume; }

    // ==================== 帧率和时间信息 ====================

    float deltaTime() const { return m_deltaTime; }
    float fps() const { return m_fps; }

    // ==================== 窗口信息 ====================

    int windowWidth() const { return m_pixelWidth; }
    int windowHeight() const { return m_pixelHeight; }
    void getPixelSize(int& outW, int& outH) const {
        outW = m_pixelWidth;
        outH = m_pixelHeight;
    }

    // ==================== 任务队列 ====================

    void post(std::function<void()> fn);
    void postDelayed(uint32_t delayMs, std::function<void()> fn);

private:
    void init();
    void shutdown();
    void processEvents();
    void update(float dt);
    void render();
    void prewarmCommonAssets();
    void flushTasks();
    void flushTimedTasks();
    void discardPendingTasks();

private:
    IApplication* m_app = nullptr;                     // 用户应用实例（可选）
    Config m_config;
    bool m_running = false;
    float m_deltaTime = 0.0f;
    float m_fps = 60.0f;
    uint64_t m_lastFrameTime = 0;

    int m_pixelWidth = 1280;
    int m_pixelHeight = 720;

    Memory::UniquePtr<Window> m_window;
    Memory::UniquePtr<InputSystem> m_inputSystem;
    Memory::UniquePtr<EventSystem> m_eventSystem;
    Memory::UniquePtr<SceneManager> m_sceneManager;
    Memory::UniquePtr<FileSystem> m_fileSystem;
    Memory::UniquePtr<ResourceManagerHub> m_resourceHub;
    Memory::UniquePtr<Tina::Renderer::ShaderManager> m_shaderMgr;
    Memory::UniquePtr<TextureManager> m_textureMgr;
    Memory::UniquePtr<FontManager> m_fontMgr;
    Memory::UniquePtr<AudioManager> m_audioMgr;
    Memory::UniquePtr<AudioEngine> m_audioEngine;
    bool m_bgfxInitialized = false;
    
    Memory::UniquePtr<Tina::UI::TextRenderer> m_textRenderer;
    Memory::UniquePtr<Tina::Renderer::Primitive2D> m_prim2D;
    Memory::UniquePtr<Tina::Renderer::SpriteRenderer> m_sprite2D;

    float m_audioMasterVolume = 1.0f;
    float m_audioMusicVolume = 1.0f;
    float m_audioSfxVolume = 1.0f;

    std::mutex m_taskMutex;
    Tina::Container::Vector<std::function<void()>> m_tasks;
    struct TimedTask { uint64_t dueMs = 0; std::function<void()> fn; };
    Tina::Container::Vector<TimedTask> m_timedTasks;
    
    static Application* s_instance;
};

} // namespace Tina::Engine
