# GoogleTest 与验证

## 规则

- 测试框架固定为 GoogleTest；
- CMake 只生成 `tina_tests` 可执行文件，不注册额外测试调度；
- 构建完成后直接运行 `tina_tests`，返回码非0即失败；
- Visual Studio 多配置构建把测试运行时隔离到 `bin/<Config>`，禁止 Debug/Release GTest DLL 共用目录；
- 测试依赖由固定 vcpkg baseline 提供；
- 测试日志不得包含路径外的敏感环境变量或凭据。

## 已验证基线

以下结果对应 2026-07-16 的 `dev` 验证基线（含 Button action 生命周期修复）。测试数量只在本文件维护；后续新增但尚未完成全平台验证的测试不计入已验证基线。

| 平台 | 工具链 | 配置 | GoogleTest | 状态 |
| --- | --- | --- | --- | --- |
| Windows 11 | VS 2026 18.4.3 / MSVC 19.50.35717 | Debug | 50/50 | 通过 |
| Windows 11 | VS 2026 18.4.3 / MSVC 19.50.35717 | Release | 50/50 | 通过 |
| Ubuntu 22.04 | GCC 11.4 | 单配置门禁 | 50/50 | 通过 |
| Linux | Clang + ASan/UBSan | 尚未建立可复现 preset | 未验证 | 待完成 |

Release 的四条 300 帧运行路径均已正常返回 0，且未出现 fatal、`BGFX LEAK` 或 `MEMORY LEAK`。这证明主循环和退出资源链路通过，不等同于截图级画面验收或实体手柄兼容性验收。

## 当前自动化覆盖与 vNext 门禁

- Core 当前：Result、ScopeExit、EnumFlags、Assert、Clock、FrameTimer、FixedStepTicker、基础类型和 Legacy Compatibility；
- Core vNext：Result context、FakeClock、Metric current/peak/reset、Trace 开关、MemoryTag、
  UTF-8/Unicode 路径、原子写失败恢复、generation ID、Ensure/CrashContext；
- Core 专用结构：StaticVector 满容量与无 heap fallback、InlineFunction 大小/移动/自销毁、
  FrameArena 对齐/reset/OOM/高水位、GenerationPool stale handle；
- Task：有界队列、QueueFull/停止后拒绝、TaskGroup 取消与 barrier、owner/generation 迟到任务、
  异常不逃出线程、IO/CPU executor 隔离和确定性合并；
- Runtime 时间：固定步长、插值、禁用 Simulation、最大追赶步和异常步消费；
- 3D Camera：60° 垂直 FOV 必须按 bx 要求以 degrees 进入投影矩阵，防止误转 radians 后 Cube 近距离铺满屏幕；
- Event：优先级队列、RAII Token、dispatcher 先销毁、立即取消订阅，以及 IME composition 与已提交文本分离；
- Resource：共享 FileSystem 唯一 completion pump、主线程预算、取消和过期 generation 隔离。
- Windows 栈预算：EventSystem 实例不得重新引入超过默认线程栈预算的大块 inline queue；
- UI：hit-test 不隐式布局、重叠节点唯一命中、Capture/Target/Bubble 顺序、动态子节点上下文继承、stale NodeId 失效、上下文先析构、节点移除/自移除生命周期、Pointer Capture 外部释放、Tab/Shift+Tab 焦点遍历、焦点 KeyDown 路由/默认取消/重复键抑制/路由中删除目标、KeyUp 完整路由/停止传播后的局部清理/路由中删除目标、方向键 beam 优先与隐藏/禁用节点过滤、Modal Focus Scope 限制/嵌套恢复/自动失效、设备无关语义导航的 scope/Accept/Cancel 生命周期、未处理按键向祖先回退、每窗口 Theme/DPI 隔离、200% DPI 逻辑坐标命中、裁剪边界、ScrollView 滚轮/钳制和十万行虚拟范围；Button action 还覆盖实例级重入隔离、异常后恢复、不同 action 嵌套、回调销毁自身，以及 Capture 阶段删除 routed click 目标后的 generation 失效。

## 待补自动化门禁

- Legacy Application 现有失败点继续回归；vNext EngineHost 对每个 injected factory/初始化阶段
  覆盖逆序回滚、`IGameApplication` 无帧回调、initial State enter/layout 失败不调用 `onExit`、
  run 只调用一次、`onShutdown` 恰好一次和重复 shutdown；
- `IGameState` top-only、structural 与 policy-change 合计每 State 每帧最多一个 command，验证
  replace 后再请求 policy-change 返回 `AlreadyQueued`；覆盖 queue/completion capacity、sequence、
  completion slot 的 Reserved/Delivered/Diagnostics 回收，以及 `initialPolicy` 单次采样；
- push/replace enter 失败保持旧栈且不调 candidate onExit；失败注入必须在 enter 中真实启动读取
  staged owner 的 Task，验证 completion 在 commit 前不可发布，回滚先 cancel + barrier/join、再释放
  Worker 可访问的 owner，最终 Task/owner/completion 计数归零；
- State Transition Commit 后新 State 同帧只 layout 一次、下一帧输入生效；pop/replace 按“关闭
  ingress → cancel → barrier/join → onExit → RAII 析构”清理 roots/focus/capture/TaskGroup，onExit
  恰好一次，Worker 不能观察已释放的 State 成员；
- Platform：Headless 不链接 GLFW、Window/Gamepad stale generation、失焦合成 release、
  fixed 0/1/4步的 Action edge 只消费一次、DPI 和 IMM32 窗口销毁顺序；
- 2D world picking 在 Action Mapping 使用 last-presented Camera/Surface revision 转换一次；0步后
  Camera 移动/resize 也不得改变已锁存 WorldPointerSample；viewport 外明确 no-hit；
- Camera2D 覆盖 NaN/Inf/非正投影值、`x + width`/`y + height` 越界、零 Surface suspension 和
  Catalog canonical PPM mismatch；PixelPerfect 覆盖强制 CameraAndSprites snap/nearest sampler、
  Camera 相对旋转、Size override 与最终 texel basis/origin 校验，不合格 Camera 不生成 view、
  不合格 Sprite 被去重诊断并跳过；
- Scene 延迟 push/pop/replace，以及 fixed phase mutation barrier、延迟实体销毁和
  interpolation snapshot；
- UI WindowRecord 唯一 ownership、RootOwner rollback、UINodeId cross-window owner 校验/回绕 retire、committed
  paint-hit snapshot、细粒度 dirty 和布局中新增 dirty 不丢；
- UIInputScopeSnapshot 对多个 eligible State roots 只做一次全局 hit-test；阻断/恢复时 Pointer Cancel、
  Focus history、Modal root scope 与 generation 失效顺序固定；
- Transform/scroll/clip 只重建 composite snapshot，不重建 local PaintCache；Visible/Hidden/Collapsed
  dirty 传播完整，相同 effective clip 确定性 intern 为同一 ClipId；
- 无变化 UI 必须 Style/Layout/PaintCache rebuild=0且 Tina heap allocation delta=0；每窗口 layout
  <=1、每 Pointer transition hit-test<=1，dirty leaf 不重排无关 subtree；
- UI 多指针/多按键、触摸输入、GLFW 手柄轮询/回滞/长按重复的可注入测试、实体手柄矩阵、焦点回调中的延迟销毁、可访问语义和截图级激活视觉状态；
- Checkbox 的 Pointer/Keyboard/Gamepad 单次切换、disabled/preventDefault、回调自销毁；Slider
  的有限性校验、clamp/step 量化、min==max、capture 拖动、Home/End、每帧单次 change 与 DPI
  命中；设置 backend 失败时 model 回滚且不留下错误全屏/音量状态；
- UI Semantics 的 Role/Name/Range/Checked/Enabled/Focused、labelledBy stale UINodeId、装饰节点过滤
  和稳定树序；Theme/DPI revision 只使必要 style/layout dirty，敏感 TextEdit 正文不进诊断；
- Font Asset lease、UTF-8 非法序列替换、中文 fallback、Atlas page 满容量/退役、raster completion
  stale generation；text measure 与 raster 分离，glyph 发布只 Paint dirty，不改变既定 advance；
- InputFrame 同 Poll 的 Down→Up、多次 Wheel/Text/Composition sequence、Move 不跨边界合并、
  transition 满容量 resync，以及 UI consumption 后固定步0/1/4次的 Action edge 语义；
- Simulation/Frame Action domain 不重复投递：0步帧保留 Simulation edge、Frame edge 当帧一次，
  replay 只记录带目标 tick 的 Simulation Action；
- Game SDK umbrella header 在无 bgfx/GLFW/EnTT include path 下独立编译；public source/include、
  module direct/public dependency 通过第三方 forbidden-token/target 检查；外部 Game consumer
  只声明 Game SDK + desktop bootstrap 也能完成生产链接；可选 `Tina::Physics2D` consumer 在无
  Box2D include path 下单独编译和链接；
- RenderScene 与 UIDisplayList 分别只生成一次并汇入 RenderFramePacket；DisplayList 不含 Widget/bgfx，
  相邻兼容 batching 保持 paint checksum；
- 在途 RenderFramePacket 期间卸载 Asset、退役 Atlas、关闭 Surface 和注入 Pass 失败仍保持引用
  有效；completion 后 packet/lease/pin/resource count 归零；纯 UI/2D-only/3D-only/无内容/
  SurfaceSuspended 的 initial clear 次数固定，UI-only/2D-only depth allocation count 为0；
- Render Pass 顺序、禁用与失败停止、临时资源清理、typed handle generation、
  NullRenderDevice 资源计数和连续300帧；vNext-null 完全不 add_subdirectory/link/load bgfx；
- Asset CPU Decode/GPU Upload 双队列的 generation 取消，以及任务数、字节、时间预算和
  饥饿保护；弱 Handle/强 Lease、UploadTicket/retirement、依赖循环/失败链、Cooker 的损坏/
  不支持 glTF、生成后验证、事务 Manifest 和增量更新；
- Asset import 的缺失/重复 ID、移动保 ID、复制分配新 ID，以及同一锁定输入跨两次独立 cook
  生成 byte-for-byte 相同产物；设置/依赖/schema/target 任一变化都会令 cache key 失效；
- Cooker 拒绝绝对/UNC/远程/根外 `..` 与 symlink URI、超限 data URI、整数溢出和解压炸弹；
  Runtime 即使 ContentHash 匹配也拒绝越界 payload/dependency table；
- Audio：Voice generation、command/completion 满容量、callback 0分配/0阻塞、设备 Disabled、
  Stop/自然结束竞争、Asset lease ACK 和重复 shutdown；
- 2D layer/order/alpha、Camera resize/world picking、Tile chunk culling/dirty rebuild、Tile AABB/
  Box2D 分工和 UI overlay 不穿透；
- 3D Camera/Material/Texture/depth occlusion、bounds/culling/instance、resize、Cooked glTF/Prefab 和
  不支持特性诊断；
- 完整 Tina 游戏的 Linux GCC/Clang production backend 2D/UI/3D 运行，以及 Clang ASan/UBSan preset。

Windows 和 Linux 必须分别构建。项目直接运行 GoogleTest 可执行文件，不使用 CTest 调度；Clang ASan/UBSan 在仓库提供可复现配置并实际通过前不得标记为已验证。

vNext 的每个垂直切片先通过模块级 GoogleTest，再启动对应样例。Null Runtime 连续300帧；
Platform/UI、Scene/2D、Render/3D、Asset/Cooker 分别保留独立运行入口。测试程序返回0、日志
资源计数为0和实际画面正确是三个不同证据，验收记录必须分别给出。

性能数据由独立 Release `tina_bench` 直接运行并输出带 schema、workload version/checksum、
硬件、工具链、依赖 fingerprint 和提交信息的结果，不使用 CTest。普通 GoogleTest 不使用
易抖动的绝对微秒阈值；Tina-owned 零稳态分配、容量溢出、checksum 和资源归零等确定性契约
仍直接阻断。当前开发机只产生 provisional 结果；固定门禁机才允许绝对预算和相对回归门禁。

正式 p99 每 workload 至少5个独立进程、每进程 warm-up 600帧并采10,000帧，nearest-rank
计算；比较 run-level p99 中位数和 baseline MAD。Build/host/workload fingerprint 不同返回
BaselineIncompatible。基准输出不记录 hostname、用户名或绝对路径，正确性 checksum 不同的
run 不参与性能比较。

Tracy Profile 构建单独验证 Tina zone、frame、thread name、可选 lock/memory event 和正常
shutdown；空后端与 Tracy 后端必须产生相同业务结果。Bench/Profile 使用相同优化/CRT/assert/
LTO 语义，只改变插桩和符号；正式 `tina_bench` 默认关闭 Tracy，需要定位回退时才用相同
workload 启用。Tracy overhead 与常驻 Metrics off/on overhead 分开记录。

Windows Debug 直接运行 `out/build/windows-msvc/bin/Debug/tina_tests.exe`，Release 使用对应的 `bin/Release/tina_tests.exe`；`Tina.exe`、shaderc 和 app-local DLL 同样按配置隔离，禁止使用共享 `bin/Tina.exe` 判断配置。Linux 单配置构建直接运行 `out/build/<preset>/bin/tina_tests`。

只验证 Runtime 和测试源码的 Linux 编译/链接时，目标使用独立 compile-gate preset，避免
污染可运行 `linux-ninja` cache：

```bash
cmake --preset linux-compile-gate
cmake --build --preset linux-compile-gate-debug --target Tina tina_tests
./out/build/linux-compile-gate/bin/tina_tests --gtest_color=no
```

该 preset 尚未落地；临时手工关闭 shader 时也必须指定独立 `-B` 目录。
`TINA_BUILD_SHADERS=OFF` 输出不含 cooked shader，不能作为可运行包或发布包。Windows 运行
验收与正式 Linux 包必须保持默认 `ON`。

## 当前 Legacy 运行冒烟

构建命令和环境前提见 [构建与运行](building.md)。以下命令描述验收入口，不替代构建步骤。

菜单 2D + 中文 UI，正常提交300帧后退出：

```bash
./Tina --smoke-frames=300
```

直接进入完整 2D TileMap、ECS、Toolbar 和 CharacterPanel：

```bash
./Tina --smoke-game --smoke-frames=300
```

直接显示虚拟化世界列表、新建世界对话框、中文标签和已聚焦 TextEdit：

```bash
./Tina --smoke-ui --smoke-frames=300
```

运行右手透视相机、深度测试和静态索引 Cube：

```bash
./Tina --smoke-3d --smoke-frames=300
```

四个命令都必须返回0，并在日志中出现正常初始化、达到帧数、场景退出、资源管理器释放、bgfx 和窗口关闭记录。UI 路径还必须出现 `UI smoke scene ready`，且不得出现 `无法建立模态焦点范围`；3D 路径必须肉眼或截图确认透视 Cube 可见，并出现 `Smoke3DScene released vertex and index buffers`，且不得出现 `BGFX LEAK` 或 `MEMORY LEAK`。只检查 exit code 和 buffer 生命周期不足以证明画面正确。

bgfx Debug/D3D11 当前会在关闭 InfoQueue 时输出一次 `RefCount is 4 (expected 0)`；同一代码的 MSVC Release 300 帧验证无该提示、无 stderr、无 leak marker，因此将其记录为第三方 Debug layer 诊断噪声，不作为 Tina 资源泄漏结论。

## vNext 独立样例门禁

以下是目标 executable，不得用当前 `Tina --smoke-*` 的结果冒充：

| 样例 | 主要证明 | 资源策略 |
| --- | --- | --- |
| `tina_sample_null` | EngineHost、`IGameApplication`/`IGameState`、RenderFramePacket、300/10,000帧生命周期 | 无第三方 backend |
| `tina_sample_ui` | committed snapshot、dirty/Flex/PaintCache、中文、Modal、TextEdit、DisplayList | M7 内置 Cooked Font/Texture fixture |
| `tina_sample_2d_infrastructure` | Camera2D、Sprite layer/order、world picking、UI overlay | M8 内置 Cooked Sprite fixture |
| `tina_sample_3d_infrastructure` | Perspective、depth、canonical Mesh、Unlit pipeline | M9 procedural Cube |
| `tina_sample_2d` | Cooked TileMap/Tileset、chunk、角色/Tile AABB、Box2D dynamic body、正式 UI | M10/M11 Catalog/Manifest |
| `tina_sample_3d` | Cooked glTF -> Mesh/Material/Prefab、culling/instance | M10 Catalog/Manifest |

M7 已建立私有最小 production Surface/UI Pass；M9 只扩展3D。游戏 sample source、Game SDK
header 和 UI public header 不出现 bgfx。结构化验收使用 backend-neutral 字段：

```text
RenderDevice stopped
render.resources.current = 0
render.retirement.pending = 0
ui.resources.current = 0
```

具体 backend 的 InfoQueue/debug marker 只属于 adapter test/log，不成为 Game API 或通用样例
成功条件。每个可见样例仍分别保存返回码、资源计数、性能结果和实际截图。
