//
// IApplication - 应用程序扩展接口
// - 职责：定义用户应用的生命周期钩子
// - 设计：纯虚接口，可选实现，用于扩展Application框架
// - 用法：继承此接口并传给Application构造函数，或传nullptr使用默认行为
//

#pragma once

namespace Tina::Engine {

// 前向声明
class Application;

/// 应用程序扩展接口
/// 用户可以通过实现此接口来定义自定义应用行为
/// 
/// **使用示例：**
/// ```cpp
/// // 方式1：不扩展（简单使用）
/// Application app(nullptr, config);
/// app.scenes().push(MakeUnique<MenuScene>());
/// app.run();
/// 
/// // 方式2：扩展自定义逻辑
/// class MyGame : public IApplication {
///     void onSetup(Application& app) override {
///         app.scenes().push(MakeUnique<MenuScene>());
///     }
///     void onCleanup(Application& app) override {}
/// };
/// MyGame game;
/// Application app(&game, config);
/// app.run();
/// ```
class IApplication {
public:
    virtual ~IApplication() = default;

    // ==================== 必须实现的方法 ====================

    /// 应用初始化钩子
    /// **调用时机：** 在Application::init()完成后，主循环启动前
    /// **调用顺序：** 窗口创建 → bgfx初始化 → 所有系统初始化 → onSetup()
    /// **用途：** 推入初始场景、加载全局资源、初始化自定义系统
    /// @param core Application实例的引用，用于访问框架服务
    virtual void onSetup(Application& core) = 0;

    /// 应用清理钩子
    /// **调用时机：** 在主循环结束后，Application::shutdown()执行前
    /// **调用顺序：** 主循环结束 → 场景栈清理 → onCleanup() → bgfx关闭
    /// **用途：** 保存游戏数据、卸载全局资源、清理自定义系统
    /// @param core Application实例的引用，用于访问框架服务
    virtual void onCleanup(Application& core) = 0;

    // ==================== 可选实现的方法 ====================

    /// 每帧更新钩子（在场景更新之前调用）
    /// **调用时机：** 事件处理后，场景更新前
    /// **调用频率：** 每帧一次
    /// **用途：** 更新全局系统（网络、AI、计时器等）
    /// @param core Application实例的引用
    /// @param dt 上一帧到当前帧的时间间隔（秒）
    virtual void onUpdate(Application& core, float dt) {}

    /// 每帧渲染钩子（在场景渲染之后调用）
    /// **调用时机：** 场景渲染后，bgfx::frame()前
    /// **调用频率：** 每帧一次
    /// **用途：** 绘制全局UI（调试信息、FPS计数器、通知等）
    /// @param core Application实例的引用
    virtual void onRender(Application& core) {}

    /// 事件处理钩子（在系统事件处理之后调用）
    /// **调用时机：** Application::processEvents()后，场景更新前
    /// **调用频率：** 每帧一次
    /// **用途：** 处理全局快捷键、截图、录像等
    /// @param core Application实例的引用
    virtual void onEvent(Application& core) {}
};

} // namespace Tina::Engine
