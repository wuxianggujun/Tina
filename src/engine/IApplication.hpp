//
// IApplication - 应用程序接口
// - 职责：定义用户应用的生命周期钩子
// - 设计：纯接口，由用户实现自定义逻辑
//

#pragma once

namespace Tina::Engine {

// 前向声明
class Application;

/// 应用程序接口
/// 用户通过实现此接口来定义应用行为
class IApplication {
public:
    virtual ~IApplication() = default;

    // ==================== 必须实现的方法 ====================

    /// 应用初始化
    /// 在窗口、bgfx、所有系统初始化完成后调用
    /// 用途：加载资源、初始化场景、设置初始状态
    virtual void onSetup(Application& core) = 0;

    /// 应用清理
    /// 在场景清理后，bgfx关闭前调用
    /// 用途：保存数据、清理自定义系统
    virtual void onCleanup(Application& core) = 0;

    // ==================== 可选实现的方法 ====================

    /// 每帧更新（在场景更新前）
    /// 用途：更新全局系统（网络、AI等）
    virtual void onUpdate(Application& core, float dt) {}

    /// 每帧渲染（在场景渲染后）
    /// 用途：绘制全局UI（调试信息、HUD等）
    virtual void onRender(Application& core) {}

    /// 事件处理（在系统事件处理后）
    /// 用途：处理全局输入、自定义事件
    virtual void onEvent(Application& core) {}
};

} // namespace Tina::Engine
