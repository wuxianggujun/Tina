//
// Scene - 场景基类
// - 职责：抽象场景概念，提供生命周期钩子和主循环接口
// - 设计：所有游戏场景（GameScene/MenuScene/PauseScene等）的基类
// - 生命周期：onEnter（进入） → update/render（运行） → onPause（暂停） → onResume（恢复） → onExit（退出）
//

#pragma once

#include "../core/Memory.hpp"
#include "../core/Container.hpp"
#include "../ui/UIConstants.hpp"  // ✅ VIEW_UI等常量定义
#include "SceneRenderer.hpp"  // EASTL的unique_ptr需要完整定义
#include "Camera2D.hpp"       // Scene默认有2D相机
#include "SubscriptionToken.hpp"  // ✅ 事件订阅token
#include "EngineEvents.hpp"   // ✅ WindowResizedEvent
#include <bgfx/bgfx.h>

// 前向声明
namespace Tina::UI {
    class UIRenderer;
    class UINode;  // 前向声明UINode
    class UILayoutManager;  // 前向声明UILayoutManager
}

namespace Tina::Renderer {
    class RenderQueue;
}

namespace Tina::Engine {

// 前向声明
class Application;
class InputSystem;

// 场景基类
// 派生类必须实现 update() 和 render() 纯虚函数
class Scene {
public:
    // 构造函数和析构函数（在.cpp中定义以支持UniquePtr）
    Scene();
    virtual ~Scene();

    // ==================== 生命周期回调 ====================

    // 场景首次进入时调用（一次性初始化）
    // 用途：加载资源、初始化游戏对象、订阅事件
    virtual void onEnter() {}

    // 场景退出时调用（清理资源）
    // 用途：卸载资源、销毁游戏对象、断开事件连接
    virtual void onExit() {}

    // 场景被新场景覆盖时调用（停止更新但保留状态）
    // 用途：暂停游戏逻辑、保存状态、停止音效
    virtual void onPause() {}

    // 场景从暂停恢复时调用（恢复更新）
    // 用途：恢复游戏逻辑、刷新状态、恢复音效
    virtual void onResume() {}

    // 窗口大小改变时调用（由框架自动调用）
    // 用途：更新UI布局、重新计算缩放、调整相机视口
    // 参数：width - 新的像素宽度，height - 新的像素高度
    virtual void onWindowSizeChanged(int width, int height) {}

    // ==================== 主循环接口 ====================

    // 框架更新入口（由Application/SceneManager调用）
    // 自动更新UI根节点，然后调用子类的update()
    void updateFrame(float dt);

    // 更新逻辑（子类实现）
    // 参数：dt - 上一帧到当前帧的时间间隔（秒）
    virtual void update(float dt) = 0;

    // 框架渲染入口（由Application调用）
    // 自动处理视图设置和touch，然后调用子类的render()
    void renderFrame();

    // 处理输入事件（框架会先处理窗口事件，然后调用子类）
    // 参数：event - 操作系统事件（键盘、鼠标、窗口等）
    // void handleEventFrame(const Event& event); // TODO: 迁移到新的事件系统

    // 子类可覆盖的事件处理
    // virtual void handleEvent(const Event& event) {} // TODO: 迁移到新的事件系统

    // ==================== 状态查询 ====================

    // 查询场景是否活跃（活跃场景才会收到update/render/handleEvent调用）
    bool isActive() const { return m_active; }

protected:
    // ==================== 视图配置 ====================

    // 视图设置结构体
    struct ViewSetup {
        uint16_t id;                           // 视图ID
        enum Type {
            World3D,                            // 3D世界视图（使用相机矩阵）
            UI2D,                               // 2D UI视图（正交投影）
            Background2D                        // 2D背景视图（正交投影）
        } type;
        bool needsClear = false;               // 是否需要清屏
        uint32_t clearColor = 0x303030ff;      // 清屏颜色
        uint8_t clearFlags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;  // 清屏标志
    };

    // 子类覆盖此方法声明需要的视图
    // 默认配置：只有一个UI视图
    virtual Container::Vector<ViewSetup> getViewSetup() {
        return {{ UI::VIEW_UI, ViewSetup::UI2D, true }};  // 默认清屏，避免resize残留
    }

    // 渲染场景内容（子类实现）
    // 注意：不需要处理视图设置和touch，框架会自动处理
    virtual void render() = 0;

    // ==================== 辅助方法 ====================

    // 访问Application实例（用于访问events()/scenes()等全局服务）
    Application* app() const { return m_app; }

    // 访问输入系统（便捷方法）
    InputSystem* input() const;

    // === UI根节点管理（框架自动处理窗口resize） ===
    // Scene子类在创建顶层UI节点后调用此方法注册
    // 窗口resize时，框架会自动调用所有根节点的onWindowSizeChanged()
    void addUIRoot(UI::UINode* root);

    // 移除UI根节点（通常在onExit时调用，或根节点销毁前调用）
    void removeUIRoot(UI::UINode* root);

    // 更新窗口尺寸（由Application调用）
    virtual void updateWindowSize(int width, int height);

protected:
    // 实际应用窗口尺寸更新（子类可以覆盖）
    virtual void applyWindowResize(int width, int height);

public:

    // 获取窗口尺寸
    int getPixelWidth() const { return m_pixelWidth; }
    int getPixelHeight() const { return m_pixelHeight; }

    // 获取场景的相机（Scene默认有2D相机）
    Camera2D* camera() { return m_camera.get(); }
    const Camera2D* camera() const { return m_camera.get(); }

    // === UI便捷访问（阶段1：自动化封装） ===
    
    // 获取UIRenderer（懒加载，自动初始化）
    // 用途：Scene子类通过ui()访问UIRenderer，无需手动初始化
    // 示例：ui().drawRect(...); ui().drawText(...);
    UI::UIRenderer& ui();
    
    // 获取SceneRenderer（懒加载，自动初始化）
    // 用途：绘制背景、遮罩等，无需了解着色器和顶点布局
    // 示例：scene().drawGradientBackground(...);
    SceneRenderer& scene();
    
    // 获取UI视图ID（子类可覆盖）
    // 默认：VIEW_UI（UI层）
    virtual uint16_t uiViewId() const { return UI::VIEW_UI; }
    
    // === 便捷方法：视图设置 ===
    
    // 设置UI正交视图（像素坐标，左上角为原点）
    // 用途：避免每帧重复计算投影矩阵
    void setupUIView(uint16_t viewId, int width, int height);

    // === 渲染队列访问（阶段2：统一渲染命令） ===

    // 获取RenderQueue（懒加载，自动初始化）
    // 用途：统一的渲染命令队列，支持排序和批处理
    // 示例：queue().submit(RenderCommand::MakeRect(...));
    Renderer::RenderQueue& queue();

private:
    // ==================== 框架方法 ====================

    // 准备视图（框架调用，子类不需要关心）
    void prepareViews();

    // 完成视图（框架调用，子类不需要关心）
    void finalizeViews();

    // 设置World3D视图
    void setupWorldView(uint16_t viewId);
    
    // UI树管理辅助方法
    void registerUITreeToLayoutManager(UI::UINode* node);
    void unregisterUITreeFromLayoutManager(UI::UINode* node);
    
    // ✅ 事件监听管理（由SceneManager调用）
    void setupEventHandlers();
    void cleanupEventHandlers();

private:
    Application* m_app = nullptr;  // Application实例指针（不持有所有权）
    bool m_active = true;          // 场景活跃标志（由SceneManager管理）

    // UI渲染器（懒加载，首次调用ui()时创建）
    Memory::UniquePtr<UI::UIRenderer> m_uiRenderer;

    // Scene渲染器（懒加载，首次调用scene()时创建）
    Memory::UniquePtr<SceneRenderer> m_sceneRenderer;

    // 渲染队列（懒加载，首次调用queue()时创建）
    Memory::UniquePtr<Renderer::RenderQueue> m_renderQueue;

    // 视图管理
    Container::Vector<ViewSetup> m_viewSetup;  // 缓存的视图配置
    bool m_viewDirty = true;                   // 视图是否需要更新
    int m_pixelWidth = 1280;                   // 当前窗口宽度
    int m_pixelHeight = 720;                   // 当前窗口高度
    
    // ✅ 事件驱动：订阅窗口resize事件
    SubscriptionToken m_windowResizeToken;

    // 场景相机（Scene默认有2D相机）
    Memory::UniquePtr<Camera2D> m_camera;

    // UI根节点列表（框架自动管理resize通知）
    Container::Vector<UI::UINode*> m_uiRoots;
    
    // UI布局管理器（每个Scene拥有自己的实例）
    Memory::UniquePtr<UI::UILayoutManager> m_uiLayoutManager;

    // SceneManager可以访问私有成员（设置m_app和m_active）
    friend class SceneManager;
    // Application可以访问私有成员（调用updateWindowSize等）
    friend class Application;
};

} // namespace Tina::Engine
