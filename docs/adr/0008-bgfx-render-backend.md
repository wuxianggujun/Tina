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

可见性进一步分为三层：普通 Game SDK 不暴露 RenderDevice/GPU handle/Pass/View/native
surface；Tina Engine Module SPI 使用 Tina-owned handle/descriptor；只有 backend private 层看到
bgfx。禁止以 `void* nativeDevice`、`getNativeHandle`、ViewId 或 callback escape hatch 绕过边界。
游戏 target 只链接 `Tina::GameSDK`、`Tina::DesktopBootstrap` 和按需的纯 Tina 扩展模块
（首个为 `Tina::Physics2D`），bootstrap 私有组合 render_bgfx。

`RenderScene` 只包含已解析的 World 2D/3D render packet，UI 独立产生 `UIDisplayList`，最终汇入
轻量 `RenderFrame` view。Runtime-private owning `RenderFramePacket` 持有 FrameArena、
FrameResourceTable、Asset lease、Atlas/surface pin 和 SubmissionTicket 到 backend completion；
Render SPI 只提供窄 `FramePinSink`，不依赖 concrete Asset/UI/Platform pin。Pass Scheduler 消费
RenderFrame；bgfx adapter 只实现 RenderDevice descriptor/resource/submit SPI，不理解 TileMap、
Widget、AssetHandle 或 RenderScene。ShaderAsset 的 Tina ABI 与私有 backend payload 分离；C++ Game API 不出现
shaderc profile、BGFX flag 或 Uniform/Program handle。

Surface initial clear/load 由 RenderFrame attachment ops 唯一定义，Scheduler 绑定到首个 enabled
content pass；无 content 时发 clear-only operation，SurfaceSuspended 时不 clear/present。纯 UI、
2D-only 和3D-only不能各自发明第二套 clear owner。

## 结果

- 上层可以用 Null backend 验证顺序和资源生命周期；
- bgfx device loss、frame retirement 和 surface suspend 由单一 adapter 处理；
- 首期只实现 Opaque3D、Sprite2D、UI、Present；
- 新真实 backend 必须先证明产品需求并新建 ADR。
- public-header compile、forbidden-token、CMake dependency closure、无 bgfx Null preset 和外部
  SDK consumer 形成自动化硬门禁，不能只依赖人工约定。
- 在途 packet 的 Asset unload、Atlas retire、Surface shutdown 和 Pass failure 必须有保活/归零测试。

## 被拒绝方案

- 同期设计 Vulkan/D3D12/OpenGL 多 backend：扩大接口且没有第二实现验证；
- 上层直接传递 bgfx handle：资源所有权和测试边界泄漏。
