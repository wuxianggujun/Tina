# ADR 0003：具体 backend 由 bootstrap factory 注入

- 状态：Accepted
- 日期：2026-07-16
- 接受日期：2026-07-17

## 背景

若 `tina_runtime` 内部直接创建 GLFW/bgfx/miniaudio，它就必须依赖具体实现，Null Runtime 也
无法做到不链接真实 backend。把 factory 放进全局注册表又会退化成 Service Locator。

## 决定

`EngineHost::Create` 接收经过验证的 `EngineConfig` 和显式的一次性 composition factory bundle。
M7-B1 已将内部组合 SPI 冻结为 `EngineCompositionFactories`，其中 Platform/Render 必须用
tagged union 二选一：

```text
EngineCompositionFactories
  common: MonotonicClockFactory + TaskSystemFactory
  platformRender:
    IndependentPlatformRenderFactories
      PlatformBackendFactory + RenderDeviceFactory
      // Headless+Null，或 M7-A GLFW+Null
    | WindowSurfacePlatformRenderFactories
      WindowSurfacePlatformBackendFactory + WindowSurfaceRenderDeviceFactory
      // M7-B1 GLFW WindowSurface handoff；M7-B2 私有 bgfx device
```

两个分支不能同时存在，也不能留空。`IWindowSurfacePlatformBackend` 同时实现普通
`IPlatformBackend` 与内部 primary-surface provider 契约，并显式提供
`acquirePrimaryWindowSurfaceLease()`、`primaryWindowSurfaceSnapshot()` 与
`publishPrimaryWindow()`。因此 EngineHost 不使用 RTTI、全局 registry 或 native handle 去寻找
provider。EngineHost 的固定事务是：验证全部 factory → Clock → Platform → Task；Independent 分支
随后直接创建 RenderDevice，WindowSurface 分支则从已创建 Platform 获取 move-only lease 和初始
surface snapshot，再把 lease 移动给 `WindowSurfaceRenderDeviceFactory`。Render 成功后才调用
`publishPrimaryWindow()`；每个成功产品立即由 EngineHost 接管并登记逆序回滚。

Desktop/headless bootstrap 只构造这组 move-only factory，不在 EngineHost 外提前创建、持有或销毁
Platform/Render 实例。配置是纯值，factory bundle 是一次性组合输入，都不注册全局状态。

窗口化 production 组合还必须遵守 [ADR 0020](0020-window-surface-handoff.md)：Platform 先创建主窗口，
内部 `WindowSurfaceRenderDeviceFactory` 再接收 move-only `NativeWindowSurfaceLease`。M7-A GLFW+Null
使用 Independent 分支且不获取伪 lease；Null/Headless 同样不创建伪 lease。M7-B1 已实现 lease、
snapshot、延迟发布与 Runtime handoff；M7-B2 的真实 bgfx device 仍未实现。任一真实 backend 失败
都不得通过全局状态或静默降级绕过。

## 结果

- Runtime 只依赖模块接口；
- Null slice 不链接 GLFW/bgfx/miniaudio；
- 失败点可以逐 factory 注入测试；
- Bootstrap 只选择 tagged 分支和具体 factory；EngineHost 仍是创建/回滚/销毁的唯一编排者。

## 被拒绝方案

- Runtime 直接 `new` 具体 backend：依赖反转失败；
- 全局 factory registry/plugin locator：隐藏依赖和生命周期。
