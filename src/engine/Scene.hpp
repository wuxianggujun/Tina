//
// Scene - 场景基类
// - 职责：抽象场景概念，提供生命周期钩子和主循环接口
// - 设计：所有游戏场景（GameScene/MenuScene/PauseScene等）的基类
// - 生命周期：onEnter（进入） → update/render（运行） → onPause（暂停） → onResume（恢复） → onExit（退出）
//

#pragma once

#include "../os/OS.hpp"
#include "../core/Memory.hpp"
#include "SceneRenderer.hpp"  // EASTL的unique_ptr需要完整定义

// 前向声明
namespace Tina::UI {
    class UIRenderer;
}

namespace Tina::Engine {

// 前向声明
class Application;

// 场景基类
// 派生类必须实现 update() 和 render() 纯虚函数
class Scene {
public:
    // 虚析构函数（多态基类必须，在.cpp中定义以支持UniquePtr）
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

    // ==================== 主循环接口 ====================

    // 更新逻辑（每帧调用）
    // 参数：dt - 上一帧到当前帧的时间间隔（秒）
    virtual void update(float dt) = 0;

    // 渲染场景（每帧调用）
    // 注意：仅提交渲染指令，不要直接设置视图矩阵
    virtual void render() = 0;

    // 处理输入事件（可选实现）
    // 参数：event - 操作系统事件（键盘、鼠标、窗口等）
    virtual void handleEvent(const Tina::os::Event& event) {}

    // ==================== 状态查询 ====================

    // 查询场景是否活跃（活跃场景才会收到update/render/handleEvent调用）
    bool isActive() const { return m_active; }

protected:
    // 访问Application实例（用于访问events()/scenes()等全局服务）
    Application* app() const { return m_app; }
    
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
    // 默认：view 3（UI层）
    virtual uint16_t uiViewId() const { return 3; }

private:
    Application* m_app = nullptr;  // Application实例指针（不持有所有权）
    bool m_active = true;          // 场景活跃标志（由SceneManager管理）
    
    // UI渲染器（懒加载，首次调用ui()时创建）
    Memory::UniquePtr<UI::UIRenderer> m_uiRenderer;
    
    // Scene渲染器（懒加载，首次调用scene()时创建）
    Memory::UniquePtr<SceneRenderer> m_sceneRenderer;

    // SceneManager可以访问私有成员（设置m_app和m_active）
    friend class SceneManager;
};

} // namespace Tina::Engine
