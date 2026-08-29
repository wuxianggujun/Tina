# ADR 0034：native surface 失效与重建（live rebind）

- 状态：Accepted
- 日期：2026-08-29
- 决策者：Tina maintainers
- 取代：[ADR 0020](0020-window-surface-handoff.md) 中「不支持 live native rebind」的那一条；
  实现 [ADR 0032](0032-mobile-platform-contract-boundaries.md) 的 C3/D6

## 背景

Android 在应用切后台时销毁 `ANativeWindow`（`APP_CMD_TERM_WINDOW`），回前台时给一个**新的**
（`APP_CMD_INIT_WINDOW`）。当前引擎会把这个循环判为致命：

- `WindowSurface` lease 钉住 native binding（`include/tina/integration/WindowSurface.hpp:31-33`）；
- binding 变化返回 `NativeWindowBindingChangedUnsupported`（`include/tina/render/RenderErrors.hpp:13`）；
- ADR 0020:154-156 明确写「M7-B 首期明确不支持 live native rebind……未来只有新增内部 rebind
  capability 与独立 ADR 后才允许重获 lease」。

也就是说：**每次切后台都会终止整个 run。** 这是 Android 上不可接受的。

### 实测：bgfx 早已支持,ADR 0020 的限制是我们自己的,不是底层的

写这份 ADR 前实际读了 bgfx 源码,结论与 ADR 0020 的前提不同:

`bgfx.cpp:448-458` 的 `setPlatformData()` 头文件注释写着 "Must be called before `bgfx::init`",
但实现里的断言只禁止改 display type 与 context:

```cpp
BGFX_FATAL(g_platformData.ndt == _data.ndt && g_platformData.context == _data.context,
    "Only backbuffer pointer and native window handle can be changed after initialization!");
```

**native window handle 是允许在 init 之后更换的。** 配套重建路径也已存在:

- `renderer_vk.cpp:7622-7626`：`recreateSurface = ... || m_nwh != _nwh`；
- `renderer_vk.cpp:3118-3121`：`reset` 时把 `g_platformData.nwh` 拷进 backbuffer 并重建 swapchain。

bgfx 自己的 Android 示例正是靠这条路径：`entry_android.cpp:238` 对 `APP_CMD_TERM_WINDOW`
**什么都不做**，`:216` 对 `APP_CMD_INIT_WINDOW` 只发一个 size 事件。

因此 ADR 0020 的那条限制记录的是**2026-07 我们的实现边界**，却被写成了能力限制。这与本仓库反复出现的
形态相同：文档把「我们没做」写成「不支持」，后续读者据此认为需要更大的改动。

### 实测：GPU 资源不需要全部重建

ADR 0032 的 D6 说「允许 RenderDevice 在同一 run 内重建 GPU 资源」，这**高估了范围**。
surface 与 swapchain 属于 backbuffer；texture/mesh/program（`BgfxRenderDevice.cpp` 中 75 处 bgfx
handle）属于 **device**，不随 surface 销毁失效。Vulkan 的 `SwapChainVK::update()` 只重建 surface 与
swapchain，不触碰 device 资源。

所以 resident `AssetLease`、`GpuTextureId`/`GpuMeshId`、两个 binding registry、retirement ledger
**全部保持有效**，无需参与 rebind。这让 C3 从「牵动全部资源 registry」缩小为「新增一类事件 + 一次
device reset」。

## 决定

### 1. rebind 是**事件**，不是 availability 状态

`Suspended` 表达「暂时不可画，但资源仍然有效」（最小化、0x0）；native binding 变化表达「backbuffer
所依附的 native window 换了」。二者不能合并：把后者压成前者会让引擎在 surface 已经不是同一个的情况下
继续认为 backbuffer 有效。

因此新增 `RenderSurfaceState::nativeBindingRevision`：**单调递增**，仅在 native binding 真正更换时
递增。它与既有 `surfaceRevision` 正交——后者表达几何/可用性变化。

### 2. 变化的检测与拒绝都在 `RenderSurfaceStateTracker`

`RenderSurfaceStateTracker::validateAndCommit()` 已经是唯一的 per-frame surface 契约守卫
（`src/render/RenderSurfaceStateTracker.cpp:73`），rebind 检测放在同一处：

- `nativeBindingRevision` 递增 → 允许，且要求 `surfaceRevision` 同时递增（binding 变化必然带来新的
  backbuffer 事实）；
- `nativeBindingRevision` 后退 → 拒绝，与既有 revision 单调性一致；
- 不递增 → 现状不变，零行为差异。

### 3. device 用新 binding 做一次 reset，**不重建 device**

bgfx 后端在观察到 `nativeBindingRevision` 递增时：`bgfx::setPlatformData(新 nwh)` +
`bgfx::reset(...)`。不调用 `bgfx::shutdown()`，因此 program/texture/mesh/shadow atlas 全部存活。

`IRenderDevice` 因此**不需要**新的纯虚函数：rebind 通过既有的 per-frame `RenderSurfaceState` 传达，
后端自行决定如何应对。这保持了「surface 状态是每帧数据」的既有形态，也让 Null device 与测试替身零改动。

### 4. in-flight frame 的处置：拒绝跨 rebind 提交

rebind 只在 `submitFrame()` 的 surface 校验点生效，而 `RenderFramePacket` 的 CPU 借用期只覆盖
submit→present（ADR 0016）。因此不存在「跨 rebind 的 in-flight pin」：要么这一帧在旧 binding 上完成
并 present，要么它还没开始。**不新增 drain 协议。**

### 5. `NativeWindowBindingChangedUnsupported` 保留给真正不支持的后端

它不再是 Tina 的普遍契约，而是「这个后端无法 rebind」的诚实回答。GLFW 桌面路径可继续返回它——桌面上
native window 不会在 run 中间更换，一旦发生就是异常而非常态。

### 6. 桌面必须能验证，且必须与 Android 走同一条码路

否则 rebind 分支只会被没人运行的工具链执行——正是已删除的 `cmake/ShaderUtils.cmake` 与 C5 的 ESSL
分支的成因。因此 `RenderSurfaceStateTracker` 的 rebind 语义由 Windows 单测覆盖，且 Null device 的
per-frame 校验走同一 tracker。

## 结果

- Android 切后台/回前台不再终止 run；
- 不新增 `IRenderDevice` 纯虚，不新增 drain 协议，不重建 device 资源；
- ADR 0020 那条限制被明确取代，不再让读者以为需要更大改动；
- 成本：`RenderSurfaceState` 增加一个字段，所有构造点需要显式意图（Tina 不保留兼容默认值）；
- 门禁：tracker 的 rebind 接受/拒绝/单调性单测；product smoke 指纹不变（桌面不触发 rebind）。

## 被拒绝方案

- **把 surface 销毁压成 `Suspended`**：语义错误。`Suspended` 承诺资源仍有效，而 native window 已经不是
  同一个；引擎会在错误前提下继续持有 backbuffer。
- **新增 `IRenderDevice::rebindNativeSurface()` 纯虚**：surface 状态本就是每帧数据，再加一条命令式入口
  会产生两个真相来源，并强迫每个测试替身实现一个它不需要的方法。
- **rebind 时重建全部 GPU 资源**：实测不必要（device 资源不随 surface 失效），且会把一次 swapchain
  重建放大成全量 asset 重上传。
- **保留 ADR 0020 的禁止,只在 Android 后端内部绕过**：会让 `NativeWindowBindingChangedUnsupported`
  变成必须被规避的东西,而不是诚实的能力回答——与 D3 里被否决的「后端内部适配」是同一种错误。
