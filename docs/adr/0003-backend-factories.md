# ADR 0003：具体 backend 由 bootstrap factory 注入

- 状态：Proposed
- 日期：2026-07-16

## 背景

若 `tina_runtime` 内部直接创建 GLFW/bgfx/miniaudio，它就必须依赖具体实现，Null Runtime 也
无法做到不链接真实 backend。把 factory 放进全局注册表又会退化成 Service Locator。

## 决定

`EngineHost::Create` 接收经过验证的 `EngineConfig` 和显式 `EngineFactories`。Executable/sample
选择 production 或 headless/null factories；EngineHost 在每个 factory 成功后立即接管实例
并登记逆序回滚。配置是纯值，factory bundle 是一次性组合输入，都不注册全局状态。

## 结果

- Runtime 只依赖模块接口；
- Null slice 不链接 GLFW/bgfx/miniaudio；
- 失败点可以逐 factory 注入测试；
- Bootstrap 需要显式维护 backend 组合，但组合只在一个位置发生。

## 被拒绝方案

- Runtime 直接 `new` 具体 backend：依赖反转失败；
- 全局 factory registry/plugin locator：隐藏依赖和生命周期。
