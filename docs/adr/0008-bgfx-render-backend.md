# ADR 0008：bgfx 是首个且唯一真实渲染后端

- 状态：Accepted
- 日期：2026-07-16

## 背景

Tina 需要 Windows/Linux 的 2D、UI 和基础 3D，而当前没有维护多套真实 Render backend 的
人力与验收需求。让 bgfx 类型扩散到 Scene/UI/Asset 会把上层生命周期绑定到实现细节。

## 决定

公共 `tina_render` 只暴露 Tina typed handle、descriptor、不可变 `RenderScene` 与小型 Pass
Scheduler；`tina_render_bgfx` 是唯一真实 backend，bgfx/bx/bimg 类型只存在于该实现与离线
shader 工具。`NullRenderDevice` 是测试 backend，不算第二个真实渲染器。Shader 由 bgfx
`shaderc` 离线编译为 Cooked Asset，Runtime 不现场编译。

## 结果

- 上层可以用 Null backend 验证顺序和资源生命周期；
- bgfx device loss、frame retirement 和 surface suspend 由单一 adapter 处理；
- 首期只实现 Opaque3D、Sprite2D、UI、Present；
- 新真实 backend 必须先证明产品需求并新建 ADR。

## 被拒绝方案

- 同期设计 Vulkan/D3D12/OpenGL 多 backend：扩大接口且没有第二实现验证；
- 上层直接传递 bgfx handle：资源所有权和测试边界泄漏。
