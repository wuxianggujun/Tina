# ADR 0046：Phase Context 暴露 host-lifetime 的 `IRenderDevice` 借用

- 状态：Accepted
- 日期：2026-09-04
- 决策者：Tina maintainers

## 背景

GPU 资源的所有权在 Tina 里是不对称的。CPU 侧的 `Asset::AssetSystem` 由游戏 State 自己构造并持有，
所以 State 想读 cooked 字节随时能读。GPU 侧不行：`IRenderDevice` 由 `EngineModules::renderDevice`
以 `unique_ptr` 独占，游戏无法自建一个——它需要 window surface lease，而 lease 的唯一 owner 是
`EngineHost`（[ADR 0020](0020-window-surface-handoff.md)）。

于是每个需要上传纹理、创建 mesh binding 或读回像素的样例都自己造了一份同样的绕道：
`CreateEngineOptions::wrapWindowSurfaceRenderDevice` 里包一个 `DeviceCapture`，把裸指针记下来，
再从 `main()` 一路传给 `IGameApplication` 和 `IGameState`。[已验证] 迁移前有 9 个 sample 加上
`editor/` 的 `EditorRenderDeviceAccess` 用这个模式，其中 5 份是逐字复制的 identity capture，
既不改行为也不采样任何东西，只为把指针搬进 State。

这条绕道让 `docs/README.md`「不要取得 `IRenderDevice*`」在字面上成立而在实质上失效：产品代码
确实取得了指针，只是取得的路径比 phase context 更长、更没有寿命契约，也没有 top-state 门控。

`DisplaySettings`（[ADR 0021](0021-runtime-ui-startup-capability.md) 的 phase-scoped 能力面）不是
答案。它刻意只暴露 vsync，因为 vsync 是唯一可热改的设备设置（见 [rendering.md](../rendering.md)），
把资源 API 塞进去会让「玩家可改的选项」和「GPU 资源所有权」变成同一个句柄。

## 决定

`IRenderDevice` 的借用直接进入三个 phase context，各自的寿命与门控由该相位能给的保证决定：

| Context | 签名 | 门控 |
| --- | --- | --- |
| `GameStateEnterContext` | `IRenderDevice& renderDevice()` | 无。候选即将成为栈顶 |
| `GameStateExitContext` | `IRenderDevice& renderDevice()` | 无 |
| `FrameUpdateContext` | `IRenderDevice* renderDevice()` | top-state，非栈顶为 `nullptr` |

**这是 host-lifetime 借用，不是 phase-local 借用。** 允许 State 在 `onEnter` 记下地址，交给没有
phase context 的成员函数使用（析构函数、`releaseGpuResources()`、`updateUI()` 里的帧捕获）。
禁止的是：在 host 析构后使用，或放进比 host 活得久的对象的析构函数里。

三条保证支撑这个寿命，缺一不可：

1. `EngineHost::Create` 的两条 composition 分支都对空 device fail-closed
   （`src/runtime/EngineHost.cpp` 的 independent 与 window-surface 分支），所以引用永不为空。
2. `stopCommittedGame` 先跑完栈上所有 `onExit`，才调 `m_modules.shutdown()`，所以 `onExit` 期间
   device 仍然活着。
3. live native rebind 只做 `setPlatformData` + `reset`，不 shutdown、不重建 device 实例
   （[ADR 0034](0034-native-surface-rebind.md)），所以地址在整个 host 生命周期稳定。

Enter 不做 top 门控：候选 State 正在成为栈顶，`onEnter` 里创建 GPU 资源是它唯一的时机。
Frame 必须门控且必须可空：下层暂停菜单若拿到完整 device，就能在上层运行时创建或退役 GPU 资源。
它和 `displaySettings()`、`inputActionRebinding()` 共用同一个 `depthFromTop == 0` 判断。

不守卫 `shutdown()`、`submitFrame()`、`present()`。这三个是 host 的职责，游戏调用它们是错误用法，
但把它们从接口上摘掉需要一个 State-facing 的 `IRenderDevice` 子集接口，那是另一个决定。

`DisplaySettings` 保持只有 vsync。资源 API 走 `renderDevice()`，两者职责不再混叠。

## 结果

- 5 份 identity `DeviceCapture` 类连同它们的 wrap lambda 删除（`2d_custom_shader`、
  `2d_shader_lighting`、`2d_shader_materials`、`3d_ik_chain`、`3d_animation_graph`），
  `2d_terraria`/`3d_voxel` 的 `core/DeviceCapture.hpp` 整份删除，`2d_catalog` 与 `samples/web`
  的内联 capture 删除，`editor/` 的 `EditorRenderDeviceAccess` 删除。
- 只设过 wrap 的样例改回单参 `Desktop::CreateEngine(config)`；仍需要 `uiFontBytes` /
  `acceptFileDropEvents` 的（`3d_voxel`、`2d_terraria`、Editor）保留 `CreateEngineOptions`，
  只去掉 wrap。
- `2d_tilemap_bgfx` 与 `3d_product` 的 `DeviceCapture` **保留**。它们不是 identity：装饰器
  实现 `requestCaptureNextPresent()`，并让 `main()` 在 `run()` 返回后读 `hasLastCapture()` /
  `statistics().liveResources`。这些是像素证据 gate 的数据源，与本 ADR 无关。两个 State 里
  取 device 的部分改走 context，只留 telemetry 调用走 `capture_`。
- `wrapWindowSurfaceRenderDevice` 保留在 `CreateEngineOptions`：它的正当用途从此只有 telemetry
  装饰器，不再是「拿到指针」的通道。
- 代价：`IRenderDevice` 的全部资源 API 对 State 可见，包括不该由 State 调用的
  `shutdown()`/`submitFrame()`/`present()`。这从「样例作者要自己接线」的成本换成了
  「接口比需要的宽」的成本。后者可以由后续的窄接口切片收窄，前者只能靠每个作者自觉。
- 门禁：`tests/runtime/RuntimeLifecycleTests.cpp` 断言 Enter/Exit 拿到的地址等于工厂交出的
  device、且 Enter 处 vsync 可往返；`RenderDeviceBorrowIsHostLifetimeAndTopGatedOnFrame` 覆盖
  下层被埋住后 `FrameUpdateContext::renderDevice()` 变 `nullptr` 而 `updateFrame` 仍在跑；
  `FailedStateEnterRollsBackTheCandidateAndKeepsTheStackRunning` 覆盖失败候选在 `onEnter` 里
  碰过 device 之后仍不调 `onExit`。`header_isolation/GameApplicationHeader.cpp` 钉住三个签名
  （两个引用、一个指针）与 `noexcept`。

## 被拒绝方案

- **只开 `GameStateEnterContext`。** 无法删掉任何 `DeviceCapture`：每个样例都在
  `onEnter`/`onExit`/`updateFrame` 三处取 device，只开一处等于三处里留两处绕道。
- **把资源 API 加进 `DisplaySettings`。** 它是玩家可改选项的句柄，也是 phase-local 的；GPU 资源
  的寿命横跨相位，塞进去会让 `hasValue()` 同时表示「非栈顶」和「没有设备」。
- **收成 callback-scoped（只在相位内有效）。** Editor 的 `preparePreviewAssetBindings()` /
  `releasePreviewAssetBindingsDraining()`、`3d_voxel` 的 `releaseGpuResources()`、`3d_product`
  的 `releaseProductGpuResources()` 都从析构函数或非相位路径调用，收窄会把它们逼回自建 capture。
- **给 State 一个窄接口（只含资源 API 的子集）。** 值得做，但需要决定这个子集的边界、由谁
  实现、以及 `Sprite2DBindingRegistry` / `Mesh3DBindingRegistry` 这些已借 `IRenderDevice&`
  的类型如何过渡。本 ADR 不预判那个边界，先把绕道去掉。
- **Frame 也返回引用。** 下层 State 会拿到完整设备，暂停菜单可以退役上层正在采样的纹理，
  且这个失败在编译期与单帧测试里都不可见。
