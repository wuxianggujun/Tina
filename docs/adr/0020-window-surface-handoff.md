# ADR 0020：主窗口与渲染 Surface 的内部交接

- 状态：Accepted
- 日期：2026-07-17
- 实施状态：当前仅 M7-A Headless Platform/Input 契约已落地；本 ADR 的 GLFW window/surface lease、
  DPI、Windows IMM32 与 bgfx 交接均是完整 M7 后续目标，不是当前可运行 backend

## 背景

桌面产品需要先由 GLFW 建立主窗口，再把对应 native window/display 信息交给 bgfx 初始化。
如果 Render backend 自己创建窗口，Platform 的输入、DPI、IME、关闭事件和窗口生命周期会出现
第二个 owner；如果直接传递 `GLFWwindow*`、`HWND`、X11/Wayland handle 或 `void*`，native 类型和
悬空指针又会扩散到 Runtime、Render SPI 甚至游戏代码。

窗口最小化、resize、恢复和关闭还会让 Runtime 帧数与真实 GPU submission 数分离。只用一个
`surfaceSuspended` 布尔值控制整个 RenderDevice，或用 engine frame 推断 GPU retirement，都会在
资源回收和未来多窗口扩展中产生错误语义。

## 决定

### 唯一创建者与 Owner

- `tina_platform_glfw` 在主线程初始化 GLFW，并创建首个且唯一的 primary window；bgfx factory
  不调用 `glfwCreateWindow`、`glfwDestroyWindow`，也不拥有 GLFW 生命周期；
- Platform backend 拥有 window registry、primary `WindowId`、GLFW window、输入、DPI、IMM32
  composition 和 close request。`EngineHost` 拥有 Platform backend 与 RenderDevice，并保证
  RenderDevice 先于 Platform backend 销毁；
- GLFW adapter 必须保持已落地的 M7-A 输入门禁：只发布 `PrimaryPointerId`，一个 Platform frame 的
  `GamepadSnapshot` 必须同 registry owner 且 slot 唯一；Runtime Action Mapper 对跨帧 retained
  active/suppressed source 与最终 held snapshot 的校验不能由 adapter 绕过；
- `NativeWindowSurfaceLease` 不拥有窗口，而是一个 move-only、不可复制、析构 `noexcept` 的
  生命周期 pin。只要 lease 存活，Platform 就不能销毁或复用对应 window slot；
- bgfx factory 成功后，具体 `tina_render_bgfx` backend 接管该 lease，直到 bgfx surface/device
  完成 shutdown。每帧 `RenderFramePacket` 的 Surface pin 是另外一个 surfaceRevision/in-flight pin，
  不能替代这条 window lifetime lease。

### 内部 Integration SPI

交接只存在于 Desktop bootstrap、Runtime composition 和两个 adapter 可见的内部 integration
header。下列签名表达冻结语义，不承诺当前切片已经实现最终 ABI：

```cpp
namespace Tina::Integration {

class NativeWindowSurfaceLease; // move-only；没有 native/void* getter

class IPrimaryWindowSurfaceProvider {
public:
    virtual ~IPrimaryWindowSurfaceProvider() noexcept = default;

    [[nodiscard]] virtual Core::Result<NativeWindowSurfaceLease>
    acquirePrimarySurfaceLease() noexcept = 0;
};

class IWindowedPlatformBackend
    : public Platform::IPlatformBackend,
      public IPrimaryWindowSurfaceProvider {
public:
    ~IWindowedPlatformBackend() noexcept override = default;
};

using WindowedPlatformBackendFactory = std::move_only_function<
    Core::Result<std::unique_ptr<IWindowedPlatformBackend>>(
        const Platform::PlatformBackendCreateParams&)>;

using PlatformAwareRenderFactory = std::move_only_function<
    Core::Result<std::unique_ptr<Render::IRenderDevice>>(
        const Render::RenderDeviceCreateParams&,
        NativeWindowSurfaceLease&&)>;

} // namespace Tina::Integration
```

`EngineCompositionFactories::platformRender` 是
`IndependentPlatformRenderFactories` 与 `WindowSurfacePlatformRenderFactories` 的 tagged union。
前者持有普通 Platform/Render factories，供 Headless+Null 或 M7-A GLFW+Null 使用；后者持有上面的
`WindowedPlatformBackendFactory` 与 `PlatformAwareRenderFactory`。两个分支互斥，EngineHost 访问
WindowSurface 分支时已经静态知道 provider 接口，不做 `dynamic_cast`。

`tina_platform_glfw` 是 lease 的唯一构造者。lease 内部保存 `WindowSurfaceId`、window generation、
native binding revision、释放所需 owner cookie，以及固定大小、带平台 kind 的 Tina-owned opaque
POD payload；它不公开 payload span、原始地址或任意 backend callback。只有
`tina_render_bgfx` 的私有 decoder 能把 payload 映射为临时 `bgfx::PlatformData`。bgfx factory
必须在调用期完成映射，不能在 lease 之外缓存 native handle。

Independent/Null 组合继续显式选择 Headless 或 GLFW + NullRenderDevice，不构造伪 lease，也
不能在 lease 获取或 bgfx 初始化失败后静默降级到 Null。生产 `Desktop::CreateEngine` 由唯一
bootstrap 只选择 tagged composition factory，不在 EngineHost 外创建 owner；EngineHost 创建 Platform
后调用 provider 并把 lease 移入 `PlatformAwareRenderFactory`。integration header 不安装到 Game SDK，
不进入 Tina module public umbrella，相关 target 只使用 PRIVATE 依赖。

Game SDK 只看到 `WindowId`、logical/framebuffer/content-scale metrics；Render module public API
只看到 backend-neutral `RenderSurfaceState`。Game SDK、Phase Context、Scene/UI/Asset、普通 Tina
module public header均看不到 `GLFWwindow*`、Win32/X11/Wayland handle、opaque payload、
`bgfx::PlatformData` 或任何 bgfx 类型。

### 创建事务与失败回滚

窗口化组合按以下事务执行，每成功一步立即登记逆操作：

```text
validate config
  -> initialize GLFW Platform backend
  -> create/register primary GLFW window
  -> acquire NativeWindowSurfaceLease
  -> move lease into PlatformAwareRenderFactory
  -> decode payload and initialize bgfx surface/device
  -> publish fully initialized RenderDevice
```

主窗口创建失败时回滚 GLFW；lease 获取失败时注销并销毁主窗口；bgfx factory 在部分初始化后
失败时，先在 factory 内撤销 bgfx 资源，再销毁已移动 lease。随后 `EngineHost::Create` 按
Render（若已发布）→ Platform window → GLFW 的顺序回滚。factory 未完全成功前不能发布可调用的
RenderDevice，失败也不能留下仍接受提交的 surface。

rollback/shutdown 必须幂等且 `noexcept`，保留最初的结构化错误并把回滚异常降为诊断。任何
阶段都不能通过销毁仍被 lease 或 submission pin 引用的窗口来“继续回滚”；若硬 deadline 后仍
无法 drain，进入 fatal-stop/fast-fail，而不是制造 use-after-free。

### Surface Snapshot、Revision 与 Resize

Platform 在 Poll 后先原子提交 `WindowFrameSnapshot{WindowMetricsSnapshot, WindowInputSnapshot}`，
再由同一主线程、同一帧边界从 committed Metrics 派生不可变
`WindowSurfaceSnapshot { surfaceId, framebufferExtent, contentScale, sourceMetricsRevision,
surfaceRevision, suspended }`。Surface adapter 禁止再次查询 GLFW 或独立采样 extent/scale：

- `sourceMetricsRevision` 必须等于本次派生所用 `WindowMetricsSnapshot::revision`；Metrics 与 Surface
  snapshot 作为一个 frame-boundary transaction 发布，不能观察到新 Metrics + 旧 Surface 的组合；
- `surfaceRevision` 是每个 `WindowSurfaceId` 内单调递增的64位值。一次 Poll 中的多次 resize/scale callback
  先合并为最终快照，再只提交一个新 surfaceRevision；相同快照不重复递增；
- `surfaceRevision` 在 framebuffer extent、content scale、suspend/resume 或 native binding generation
  改变时递增。surfaceRevision 回绕属于 fatal contract violation，不能静默复用旧值；
- Runtime 每帧只采样一次快照，把它转换为 `RenderSurfaceState`，并让 packet pin 住对应 surface id +
  surfaceRevision。旧 surfaceRevision 的 packet 可以完成，但新 packet 不得混用旧 viewport/attachment；
- Active resize 在下一次 surface submission 前由 backend 在主线程应用；多次未应用 surfaceRevision
  可合并到最新值。成功后记录 `appliedRevision`，失败返回结构化
  `SurfaceReconfigureFailed`，停止新提交并进入关闭/drain，不自动切换 Null backend；
- 普通 extent/scale resize 不重新创建 GLFW window，也不换 native lease。只有 native binding
  generation 确实变化时需要重新交接 lifetime lease；M7-B 首期明确不支持 live native rebind，检测到
  变化立即返回结构化 `NativeWindowBindingChangedUnsupported`，停止 ingress 并进入 Failed → Draining，
  不销毁仍被旧 lease pin 住的窗口。未来只有新增内部 rebind capability 与独立 ADR 后才允许重获 lease。

### Suspend、Resume、Close 与 Drain 状态机

每个 surface 独立遵循以下状态，而不是修改整个 RenderDevice 的全局可用性：

```text
Creating -> Active <-> Suspended -> Closing -> Draining -> Closed
     |          |          |            |
     +----------+----------+----------> Failed -> Draining
```

- framebuffer extent 为0×0或平台明确最小化时进入 `Suspended`。Focus 丢失本身不等于
  suspended；
- Suspended 保留 window、native lease、RenderDevice 和已有 GPU 资源。Runtime 继续处理
  Platform polling/lifecycle dispatch、Simulation、Asset completion 与必要的 backend retirement，但不创建 surface
  attachment、不 clear、不提交 surface frame、不 Present；平台等待/限频避免 busy loop；
- 恢复为非零 extent 时提交新 surfaceRevision，backend 先应用最新 extent/attachment，再接受首个
  Active submission。恢复失败进入 `Failed`，不能假装仍 Suspended 无限重试；
- GLFW close callback 只写入 sticky、不可取消的 close latch。Platform `pollFrame()` 返回 tagged
  `PlatformPollResult::ExitRequested`，不创建 `PlatformFrameView`；Runtime 在任何新帧
  PlatformEventDispatcher/Input/Fixed/Render phase 开始前进入 `Closing`，关闭新 packet ingress，取消
  Pointer Capture/IME composition，并停止新 submission；该 outcome 既不进入当前同步生命周期
  `PlatformEventDispatcher`，也不进入未来通用 Runtime Event Queue，callback 本身不得销毁窗口、UI、
  RenderDevice 或 Scene；
- 已在关闭提交点之前被 backend 接受的 packet 进入 `Draining`，等待所有 submission ticket、
  deferred destroy 和 packet Surface pin 归零。resize/scale 事件在 Closing/Draining 不再触发
  backend reset；
- drain 完成后先关闭 bgfx surface/device，再销毁 RenderDevice 持有的
  `NativeWindowSurfaceLease`，最后由 Platform 销毁 GLFW window并终止 GLFW。只有到这一步状态
  才是 `Closed`。

游戏内 `requestExitAfterFrame()` 的“完成当前帧后退出”在 surface snapshot 为 Active 时允许当前
packet 完成提交；若 snapshot 为 Suspended，则仍完成 Extraction/UI/Render-skip/Deferred Cleanup，
但不创建 surface submission 或伪 ticket。OS CloseRequested 在 Poll 阶段发生；该分支的
`PlatformFrameView` 未被创建，Runtime 也尚未开始新帧，因此不组装“最后一帧”packet。一旦 Closing
已进入，两条路径都不得再创建新 packet。

`surfaceSuspended` 只属于 `WindowSurfaceSnapshot/RenderSurfaceState`。它不表示 device lost，也不阻止
与该 surface 无关的资源 retirement、诊断或未来其他 surface 工作；`IRenderDevice` 不提供
`setSuspended(bool)` 这类全局开关。

### Engine Frame 与 Submission 编号

- `engineFrameIndex` 由 Runtime 每次主循环递增，包括0个 fixed step、surface suspended 和没有
  GPU 工作的 Continue 帧；它只用于游戏/CPU frame timing 和确定性阶段关联。Close-only
  `PlatformPollResult::ExitRequested` 不创建 frame view，也不分配或递增该编号；
- `submissionIndex` 由 RenderDevice 在一个非 Suspended `RenderFrame` 被 backend 实际接受时
  分配，按 device 单调递增，并随 `SubmissionTicket` 记录 `WindowSurfaceId` 与 surfaceRevision；clear-only
  surface frame 也是一次真实 submission；
- suspended 路径返回显式 `SkippedSurfaceSuspended`/无提交结果，不调用 surface submit/present，
  不产生伪 `SubmissionTicket`，也不递增 `submissionIndex`。当帧尚未进入 in-flight 的 packet
  直接事务回收其 frame-local pin；先前 in-flight packet 仍按真实 completion 回收；
- upload/retirement maintenance 使用自己的 ticket/counter，不伪装成 surface submission。
  `engineFrameIndex`、`submissionIndex` 和 Present 计数永远不能假设相等，GPU 资源 retirement
  只能依据真实 submission completion/fence。

## 结果

- 当前 M7-A Headless 已提供可验证的 Platform frame、Primary Pointer、Gamepad snapshot、生命周期
  dispatcher 与 close outcome 基线；下面的 GLFW/DPI/IMM32/surface 结果要在完整 M7 adapter 落地后验收；
- GLFW 保持窗口、输入、DPI、IME 与关闭语义的唯一 owner，bgfx 只负责渲染；
- opaque lease 在不泄漏 native/bgfx 类型的前提下，强制窗口晚于 RenderDevice 销毁；
- resize、最小化和关闭使用可测试的 surfaceRevision/state machine，不依赖 callback 偶然顺序；
- CPU 主循环、真实 GPU submission 与资源 retirement 的编号不再混用；
- 首期仍只有一个 primary surface；若增加多窗口，必须先扩展资源预算和调度验收，而不能复制
  全局 suspended/close 状态。

## 验收

- 首先复用 M7-A Headless 门禁，证明 GLFW adapter 仍只接受 `PrimaryPointerId`、Gamepad snapshot
  同 owner/slot 唯一、retained active/suppressed source 与最终 snapshot 一致，且 CloseRequested 不进入
  `PlatformEventDispatcher` 或未来通用 Runtime Event Queue；
- 注入 window 创建、lease 获取、bgfx init 和 surface reconfigure 每个失败点，验证完整逆序回滚、
  无窗口/lease/submission 残留且不降级 Null；
- 验证 RenderDevice shutdown、lease release、GLFW window destroy、GLFW terminate 的严格顺序，
  并让错误顺序在测试中立即失败；
- 连续 resize callback 只提交最新 surfaceRevision；旧 packet 可完成但不能污染新 attachment/viewport；
- 最小化300个 engine frame 不 clear/present、不增加 submissionIndex、不 busy loop；恢复后只应用
  最新 revision并正常提交；
- close 时拒绝新 packet，drain 已接受 ticket 后才释放 lease/窗口；drain timeout 不释放活跃内存；
- public-header、外部 Game SDK consumer、forbidden-token 与 dependency-closure 门禁证明普通模块
  不需要 GLFW/bgfx include path，且不存在 native/opaque payload escape hatch。

## 被拒绝方案

- 让 bgfx adapter 创建/销毁 GLFW window：形成第二个窗口 owner并拆散输入、IME 与关闭生命周期；
- 在公共 factory 中传递 `GLFWwindow*`、`HWND`、X11/Wayland handle 或 `void*`：类型和寿命泄漏；
- 用可复制 descriptor 代替 move-only lease：无法阻止窗口在 backend 使用期间被销毁；
- 最小化时关闭整个 RenderDevice或每次 resize 重建 GLFW window：破坏无关资源并制造抖动；
- 用 `engineFrameIndex` 充当 submission/fence 编号，或为 suspended 帧伪造完成：导致提前退役；
- close callback 立即销毁窗口：在途 packet、bgfx surface 与 IME/Input callback 可能悬空。
