# Tina 源码审查与修复交接（2026-09-05）

## 1. 文档用途

本文是当前工作树的源码审查交接入口：记录问题证据、修复边界和接手步骤。任务状态的权威明细仍是 [Backlog](backlog.md)，模块结构见 [Architecture](architecture.md)。不要把静态审查等同于已通过运行验证。

事实优先级保持不变：

1. 当前源码、CMake target、测试与实际运行结果；
2. `docs/design-freeze.md` 与 Accepted ADR；
3. 主题文档；
4. 历史证据和旧审查记录。

本项目不保留旧设计兼容层。修改 API 或 wire schema 时直接迁移到当前设计，旧 API/旧 schema fail closed；需要改变 wire schema 时同步 bump `SchemaVersion`，不增加版本嗅探、别名、wrapper 或 `deprecated` 双轨。

## 2. 当前工作树状态

- 最近继续整理：2026-09-06；保留原文件名作为稳定交接链接。
- 分支：`codex/tina-vnext-runtime`
- HEAD：`94a163d8`
- 工作树包含大量既有未提交修改，覆盖 Runtime、Asset、UI、Editor、文档和测试；不得 reset、checkout、clean-first 或删除无关修改。
- 本轮已修改 Runtime、Scene、Physics2D、Audio、Core 与 Render 源码，更新相关回归用例并同步文档。最小目标单并发构建通过，具体产物与修复过的编译阻塞见第 10 节。没有运行 GoogleTest、sample 或产品 exe。
- 限制并发为 1，不启动子代理。另一位 AI 正在推进 AI/寻路，本轮不修改 Navigation2D、寻路实现或其任务状态。
- 代码/文档保持 UTF-8；源代码新增注释使用 ASCII，中文说明保留在 UTF-8 Markdown 中。

接手第一步：

```text
git status --short
git diff --check
```

## 3. 已有实现与已修待验证（不要重复实现）

### 3.1 并发、上传和文本回调

| 能力 | 当前事实 | 主要证据 |
| --- | --- | --- |
| `TaskGroup` | pending 更新与 wait predicate 已使用同一 mutex 协议；有计数边界防护；停止时继续 drain 已接受任务。包装 callable 的分配异常仍有独立缺陷，见 P1-6 | `src/task/TaskGroup.cpp` |
| GPU upload | ready/pending 预留、`UploadLedgerFull` 回滚、stale ticket 清理、retry、`PendingUpload::ticketRetired` 已实现 | `src/asset/AssetGpuUpload.cpp`, `include/tina/asset/AssetGpuUpload.hpp` |
| TextEdit callback | changed/submit 回调使用固定容量 snapshot；重入时外层事件仍指向本次快照；容量不足不发布半份事件 | `src/ui/UIContextText.cpp`, `include/tina/ui/UITextEdit.hpp` |

旧交接文档中的 P0-1/P0-2/P0-3 对应修复已在工作树源码中看到，已移出待实现列表。本轮没有复跑它们的门禁，不据此宣称 Task/GPU/UI 全模块无风险。

### 3.2 资源、纹理和透明基础能力

当前独立 PNG/JPEG 到 GPU 的基础链路已经存在：

```text
PNG/JPEG
  -> MediaCook/stb_image（仅 Cooker 私有 TU）
  -> RGBA8 + 可选完整 mip chain
  -> Texture2D payload schema v2
  -> Catalog/AssetSystem/AssetStore
  -> typed GPU upload
  -> Sprite2D/Mesh3D binding registry
  -> Null 或 bgfx RenderDevice
```

关键事实：

- `MediaCook` 以 `stbi_load_from_memory(..., 4)` 强制解码为 RGBA，alpha 不会在 decoder 层被丢弃。
- `TextureMipChain` 在线性颜色空间按 alpha 加权生成 mip，输出仍是 straight RGBA；全透明块归零。它降低透明区颜色污染，不证明源图 bilinear 采样或无 gutter 图集完全没有黑边。
- Sprite2D fragment 采样纹理 alpha，输出 premultiplied RGB/A；bgfx 使用 `(ONE, INV_SRC_ALPHA)`。
- UI ImageQuad 走独立 RGBA shader，采样后转为 premultiplied alpha。
- 3D Material 的 alpha pass intent 只允许显式 `Opaque` 或 `Blend`；`Blend` 使用 straight-alpha、深度测试但不写深度。
- Mesh/Texture 的 GPU retirement 使用独立 readback marker，不把 CPU frame completion 当作 GPU fence。

因此，“引擎完全没有透明处理”不是当前事实。水图片不透明时，必须先确认它实际走的是 Sprite2D、TileMap、UI ImageQuad 还是 3D Material 路径，再检查源文件 alpha、Cooked payload、材质 alpha mode 和自定义 shader 合约。

### 3.3 本轮源码已修，尚未验证

本节“未验证”指尚无运行/失败注入/视觉证据；相关最小 target 已编译通过，不能把 Build 视为 Unit/Visual。

| Backlog ID | 原问题与修复 | 剩余验收 |
| --- | --- | --- |
| `RUNTIME-STARTUP-TEARDOWN-001` | startup candidate 在 modules 关闭后才析构；`EngineHost.cpp::startUnchecked` 失败出口现按 scope cancel/join → candidate reset → scope reset → module teardown 排序 | 扩充 `RuntimeLifecycleTests.cpp`：onEnter 失败、worker 仍访问 State、析构访问 Render 的顺序；不能提前销毁 worker 引用的 State |
| `SCENE-LOAD-ROLLBACK-001` | 文件加载实例化后，index 或 gameplay bytes 失败会残留实体；`World2DSnapshot.cpp::loadWorld2DSceneFromFile` 现保留局部 bindings，index 返回失败或 bytes `bad_alloc` 时反向 destroy，再更新 transforms，成功后才发布 bindings | `World2DSnapshotTests.cpp` 补失败注入，确认原有实体保留、新实体清空；当前实现不承诺任意异常均转换成 Status，也不承诺恢复 handle generation/内部 revision |
| `PHYSICS-AABB-OVERFLOW-001` | finite 端点在 float 差值/求和时溢出；`PhysicsWorld2D.cpp::overlapAabb` 改为 double 派生中心/半尺寸并检查 float 可表示范围 | `PhysicsWorld2DTests.cpp` 补极端输入；仅修 Tina 派生中间值，Box2D 内部巨大坐标安全范围尚未确定 |
| `AUDIO-TERMINAL-COMPLETION-001` | PCM 自然结束绕过已有 terminal parking，ring 满时丢 `Stopped` 并回收 one-shot；`AudioEngine.cpp::harvestNaturalEndForSlot` 现复用 `queueClipTerminal`，成功入队后再回收 | 已更新 `AudioEngineTest.OneShotNaturalEndParksUntilStoppedCompletionCanBeQueued`，覆盖 ring 满、拒绝提前释放、延迟 Stopped、无重复、generation 槽复用；已编译，尚未运行 |
| Core 构建阻塞 | `JsonDocument.cpp::JsonValue::elements()` 的 `emplace_back(value)` 让 STL allocator 尝试调用私有构造；改成在类成员作用域内 `push_back(JsonValue{value})`，不开放构造权限 | 首轮 MSVC C2672，修改后 `tina_core.lib` 已生成；JSON 行为测试尚未运行。同步改正 `JsonParseOptions` 注释，不再误称 depth/node 限制保护 parser 峰值 |

上述项保留为“实现完成、验收未完成”，不再列入下节的待修代码列表。Backlog 的 InProgress 不表示还要重写同一修复。

## 4. 水资源透明度专项诊断

### 4.1 现有能力边界

普通 2D 水贴图的像素透明链路为：源 RGBA → Cooked `Texture2D` → texture alpha × tint alpha → Sprite2D premultiplied 输出 → blend 合成。无 alpha 源图（例如 JPEG 解码 alpha=255）也可通过 tint 做整体半透明；源图不带 alpha 不等于资源错误。TileMap 的 `TileChunkSpriteEmitParams.alpha` 影响整体透明度，不替代纹理自身 alpha。

普通 3D 水面应满足：Material 显式设置 `alphaMode=Blend`，Scene extraction 与 Material binding 的 alpha intent 一致，进入 `Transparent3D` back-to-front pass。Runtime 不会根据 base-color alpha 自动猜测透明 pass。

### 4.2 最可能的故障点

| 现象 | 优先检查 | 结论/处理 |
| --- | --- | --- |
| 2D Sprite 整张不透明 | 源/Cooked alpha 分布、最终 tint alpha、实际绑定纹理 | 逐像素透明要求图中存在变化的 alpha；整体半透明可设 tint。若源有 alpha 而 Cooked 丢失，再修导入 |
| 2D 边缘发黑或像不透明 | atlas 子矩形 mip、源图边缘颜色、gutter、premultiplied/straight 是否混用 | Catalog recipe 可按 referrer 禁用子矩形纹理 mip；独立 MediaCook 不能推断后续如何切图。Sprite2D/UI 自定义输出遵守 premultiplied 合约 |
| TileMap 水不透明 | TileMap 是否只设置了整体 alpha；Tileset dependency 是否解析到正确 Texture2D | 修资源引用或整体 tint；不要在 TileMap 另造 shader |
| 3D 水不透明 | Material 是否显式 `Blend`；Material binding 与 Scene item alpha intent 是否一致 | 不兼容旧推断逻辑，直接修当前 Material/cooker 数据 |
| 自定义 shader 水不透明 | varying/uniform 声明、实际采样、最终输出 alpha、所用 pass | Sprite2D/UI 输出 premultiplied，Mesh3D Blend 输出 straight RGB/A；cook/submit 能验证结构与绑定，不能证明任意 shader 的 alpha 数学正确，须用像素 witness |

建议复现时记录：资源绝对路径、AssetId、使用组件/场景、Cooked Texture2D header（format、colorSpace、levelCount）、最终 binding key、alphaMode，以及一张源图和运行截图。当前代码已经能证明基础 alpha 通路，缺少的是针对“水资源”的产品级复现样例和数据断言。

用户尚未提供具体水资源或场景，因此不能把上述检查点写成该资源的已确认根因。源码另发现 sRGB 重复解码路径，见 P2-7；它影响 RGB，不直接改变 alpha，仍需独立像素验证。

### 4.3 水效果仍然缺少的功能

这些是能力缺口，不是已实现基础 alpha 的 bug：

- 没有专用 Water Material/Shader（波动、法线滚动、深度淡化、折射、屏幕颜色采样）。
- 没有 `Mask`/alpha-test；只有 `Opaque` 与 `Blend`。
- Transparent3D 不作为 shadow caster；没有 order-independent transparency。
- 没有水面排序/深度淡化的产品 authoring 入口，也没有水资源的 2D/3D sample witness。

推荐顺序：先用现有 Sprite2D 或 `Blend` Material 做最小水资源 sample，补 source/Cooked/binding/像素 alpha 断言；再单独设计 Water shader 与 offscreen/depth 输入，不能把高级水效果混进通用 alpha 基础路径。

### 4.4 方块数量与性能诉求（2026-09-06）

用户反馈“顶点/方块数量被写死”，但未给具体场景或报错。当前工作树已有 U32 mesh 源码通路，不能重复声称仍受旧 U16 顶点上限限制。
进一步发现 bgfx 瞬态池未从 Host 暴露预算，单独增加 CPU scene 容量仍可能在默认 6/2 MiB 池失败；本轮已增加
`renderTransientVertexBufferBytes` / `renderTransientIndexBufferBytes`，保留默认内存占用和启动定容，不加旧设计兼容层。
两种 Host factory 都透传，Null/bgfx 校验非法预算，bgfx 防 allocation header 加法溢出；容量失败包含需要/可用 bytes。
同时修正 drawCallCapacity 的公共校验漏上界（65,536 原本能通过 EngineConfig，却无法被 native backend 接受）。
构建还暴露 `GpuMeshUploadTests.cpp` / `NullRenderDeviceTests.cpp` 的三角索引 fixture 未从 U16 迁走，已迁到 U32，
保留 U16 skin influences；新增 `NullRenderDeviceMeshTest.StaticAndSkinnedUploadsAcceptVertexIndicesAbove65535`，
同时检查大索引被接受、等于 vertexCount 的非法索引被拒绝。Render 回归已编译，未运行；未对所有 Asset/sample 测试逐个构建。

当前仅能确认这些源码边界，不能断言用户实际场景已撞到某一池，也不能承诺 FPS 提升。完整数量表、配置示例、
chunk/instancing/greedy meshing 的后续方向见 [Render 容量说明](rendering.md#方块数量与容量预算)，任务为 `RENDER-TRANSIENT-BUDGET-001`。

### 4.5 已修的每帧效率问题（2026-09-06 继续推进）

以下两项源码已修，后续只补验收，不要重新实现：

| ID | 原因与影响 | 当前修复与验收 |
| --- | --- | --- |
| `RENDER-RESOURCE-LOOKUP-001` | `RenderFramePacket.hpp::intern` 线性扫描已登记 descriptor；R 个不同 mesh/material/texture 初次登记累计 R(R-1)/2 次比较，提高容量会放大 CPU 开销 | 预分配最多半满的 hash 索引，保留完整 key/kind 比较、dense ref 顺序、Pin 与满容量 duplicate 语义，清理只遍历本帧占用槽；新增碰撞/8,193 混合资源/帧复用/probe-count 回归。索引最坏行为不宣称 O(1)，额外内存见 Render 文档 |
| `RENDER-OPAQUE-BATCH-SORT-001` | `RenderScene.cpp::commit` 的不透明排序漏 shader/uniform，`sameMeshBatch` 却要求它们相等；交错资源造成碎批次、额外 draw 和虚假的 batch capacity 不足 | 排序在 depth 前加入 shader/shaderUniforms；4,000 item/4 pipeline fixture 限 batch capacity=4，检查正反插入与组内稳定顺序；透明/Sprite/skinned 路径不改 |

尚无运行和 FPS 证据。普通方块若全都复用同一 mesh/material/shader，第二项可能没有额外收益；大量不同 chunk/材质
更容易暴露第一项。必须使用用户真实场景或固定 fixture 记录 workload，不能给出凭空加速倍率。

## 5. 仍存在的问题与修复方案

### P1-1 外部驱动 Host 缺少显式 stop

- 触发：调用 `EngineHost::start()` + 外部 `tick()`，随后直接销毁 Running Host。
- 影响：可能跳过 `IGameState::onExit()`、`IGameApplication::onShutdown()`、`StateTaskScope::cancelAndJoin()`，销毁顺序不再受控。
- 修复：新增 `EngineHost::stop(IGameApplication&) noexcept`；只允许 owner thread、外部驱动模式、Running 状态调用；复用 `stopCommittedGame()`。析构遇到 Running 状态应 fail closed，不能静默跳过生命周期回调。
- 验证：新增 start/tick/stop、重复 stop、stop 后析构、错误线程调用测试；确认 state/application 回调各执行一次且 task scope 已 join。
- 状态：待修。

### P1-3 AssetLease 与 AssetStore move/lifetime 契约不安全

- 触发：`AssetStore`/`AssetSystem` move 后仍使用旧 `AssetLease`，或 worker/render 线程析构 lease。
- 影响：`AssetLease` 保存裸 `AssetStore*`，可能 UAF；AssetSystem move 还可能重建空 GPU coordinator，丢失 pending ticket/queue。
- 修复：采用单轨设计：优先禁止存在 active lease、GPU pin、pending upload、retirement callback 时 move；或者引入 lifetime control block + 固定容量 deferred-release queue。统一 owner-thread query/release，错线程 fail closed。不能加兼容 wrapper。
- 验证：move 前后 lease、错线程 release、Store 析构后晚到 release、pending upload move 的单测；确认不访问旧地址且 coordinator 状态不丢失。
- 状态：待设计后实现。

### P1-4 StateTaskScope/Host 无期限等待

- 触发：worker 永久阻塞，状态切换、Pop/Replace、异常退出或 Host shutdown 调用 `cancelAndJoin()`。
- 影响：`TaskGroup::waitIdle()` 无限等待，Host 永久卡死，已有 shutdown deadline 形同虚设。
- 修复：增加带 deadline 的 join；超时返回结构化 timeout 并保持 scope/worker 存活，禁止 detach 或强杀。
- 验证：阻塞 worker + 短 deadline 测试；确认 API 返回 timeout、资源仍可安全收尾，正常 worker 仍 exactly-once join。
- 状态：待修。

### P1-6 TaskGroup 包装任务分配异常泄漏 pending

- 证据：`TaskGroup.cpp::add` 在递增 pending 后构造捕获整个 `TaskCallable` 的外层 callable；`MoveOnlyFunction.hpp` 对超出 inline buffer 的捕获执行 heap allocation。
- 触发：包装 callable 的分配抛出 `std::bad_alloc`，尚未进入 `scheduleCpu`。
- 影响：已有返回失败回滚分支不会执行，pending 永远非零，wait/析构可能永久阻塞。
- 修复：在发布 pending 前构造包装 callable，并把内部构造异常转换成 OutOfMemory；再明确 `scheduleCpu` 的接受点和异常契约，避免对已经接受的工作重复递减。不得只把返回失败分支视为全部异常安全。
- 验证：包装分配失败后 pending=0、waitIdleFor 成功；正常接受/返回拒绝路径计数各一次。不要用生产全局 allocator 开关作为测试接口。
- 状态：待修，Backlog `TASK-GROUP-ALLOCATION-001`。

### P2-1 JSON 限制晚于 nlohmann DOM 解析

- 触发：输入在 `maxInputBytes` 内，但深度/节点数极大。
- 影响：`nlohmann::ordered_json::parse()` 先构造完整 DOM，`maxDepth/maxNodes` 只保护后续 Tina DOM；峰值内存可能远超配置。
- 修复：引入 SAX/事件式解析，或在 parser 层实施深度/节点预算；不做旧格式兼容。至少在文档和 API 中明确当前限制不包含 parser 峰值。
- 验证：深层/宽节点 corpus 记录 parser 峰值与拒绝点，确认超限在受控预算内 fail closed。
- 状态：待设计。

### P2-3 Audio shutdown 无限自旋

- 触发：realtime reader/callback 卡死，shutdown 等待关闭位清零。
- 影响：析构线程永久阻塞。
- 修复：增加可观测 deadline；超时保持 callback 仍需的数据存活并返回明确状态，禁止强杀线程或提前释放共享数据。
- 验证：注入不退出 callback，确认 deadline 返回、进程不 UAF；正常 callback 仍完成关闭。
- 状态：待修。

### P2-5 AssetRetirement ledger 长期增长

- 触发：长时间流式加载/场景切换，Released record 只清字段不回收槽位。
- 影响：记录数无限增长，`find()` 线性扫描，长期查询退化。
- 修复：固定容量 released slot 复用，或在无外部借用时批量 compact；先明确 `records()` 引用失效契约再改。
- 验证：百万次 release/reuse workload，确认记录数有界、查询复杂度稳定、外部引用契约明确。
- 状态：待设计。

### P2-6 World2D capture 丢失 Marker2D kind

- 触发：无 payload 的实体统一 capture 为 `Node2D`。
- 影响：Editor `Marker2D` 保存再打开后降级为普通 Node2D，无法 round-trip。
- 修复：在 World runtime metadata 保留 authored node kind，并在 capture 时恢复 Marker2D。优先复用现有 wire kind；只有实际改变 wire 布局才 bump schema 并拒绝旧文件，不为增加内存 metadata 无故改格式。
- 验证：Marker2D capture/load byte round-trip 与未知 kind fail closed 测试。
- 状态：待产品决策。

### P2-7 内置 Mesh3D 对 sRGB 纹理重复解码

- 证据：`src/render/bgfx/BgfxTexture2DUpload.cpp::toBgfxTexture2DFlags` 对 Srgb 设置 `BGFX_TEXTURE_SRGB`；`src/render/bgfx/shaders/fs_tina_opaque3d_mr.sc::main` 又对 `texture2D(s_texColor, ...)` 的 RGB 调用 `srgbToLinear`。
- 触发：通过正式 Texture2D upload 绑定 sRGB base-color 后，使用内置 static/skinned Mesh3D fragment。硬件采样已解码为 linear，shader 再解码一次。
- 影响：base-color 明显偏暗，影响 PBR/透明面颜色；这条计算的 alpha 仍为 `v_color0.a * texel.a`，不能解释为“引擎没有透明”。
- 修复：统一采样返回 linear 的契约，以 texture colorSpace/上传 flag 为转换 owner，移除内置 Mesh3D 的额外解码；同步检查自定义 shader 示例、默认纹理、最终 framebuffer 编码和 Sprite/UI 路径，不能简单删除上传 flag 来掩盖局部错误。
- 验证：sRGB 128 对照等价 linear 约 0.216 的纹理，受控照明下结果一致；opaque/blend/static/skinned 都取像素，alpha 不变。当前只有源码证据，未修改 shader、未做 GPU 视觉验证。
- 状态：待修，Backlog `RENDER-SRGB-DECODE-001`。

## 6. 功能缺口清单

以下是审查时仍有缺口的能力；另一会话可能继续修改，开工前先核对源码和 Backlog。

| 缺口 | 落地方案 | 验收 |
| --- | --- | --- |
| Runtime chunk streaming/world partition | 在产品 owner 之上定义 request/cancel/generation、CPU/GPU budget、evict/retry；复用 Asset loading/retirement，不再造资源寿命系统 | 跨 chunk 移动、取消旧请求、预算满、GPU 尚未退休时不复用资源 |
| Metrics Registry | 先定固定容量、指标身份和 owner-thread publish，接 Asset/upload/retirement 指标 | 容量满显式失败、snapshot 一致、热路径无分配 |
| 3D floating origin / Physics3D | 分开设计坐标重基准事务与私有 physics adapter，不把 Physics2D 接口强行扩成两套语义 | 大坐标下相机/物理/渲染同帧一致；公开头不漏 backend 类型 |
| Texture source sampler authoring | 扩展 recipe/SDK 输入并一路传递到现有 `Texture2DSamplerDesc`，明确 mip/颜色空间策略 | source → cooked → upload 的 wrap/filter/mip/anisotropy/颜色空间一致 |
| Texture 压缩/transcode | 在 Cooker 接生产编码器，复用 BC/ASTC reader；按平台能力显式选择格式 | block/level 字节数、alpha 保留、GPU 支持与不支持两条路径 |
| Water/Mask/透明排序能力 | 先做现有 alpha witness；Water 再定义 screen-color/depth 的 pass 输入；Mask 与 OIT 单独评估 | 水面/背景像素合成正确，深度边缘与透明重叠有独立视觉证据 |
| bgfx 后处理 | 现有 Null offscreen/HDR/bloom/tone mapping 契约不等于 GPU 实现；为 bgfx 增加 attachment owner、pass 依赖和 resize/retirement | 非空 chain 不再返回 `RenderTextureUnsupported`，真实 GPU 像素与 resize 证据 |
| Font payload/cooker、包签名 | 分别定义字体数据消费契约与验签信任边界；不把 path 字体 fixture 写成通用 font cooker | 字体 typed round-trip；损坏/伪造包拒绝且无半份发布 |
| 平台/辅助功能验收 | 在对应环境执行 zenity/kdialog、跨 DPI/GPU 和 Narrator/Inspect 金标，不凭 Null 结果关单 | 保存真实平台、动作、结果与截图证据；Editor 只在大功能闭环后统一 gate |

每个缺口都应先写 wire/API/所有权边界，再实现 producer/consumer 和最小产品 witness；不要先添加只能被 Null 或单测消费的“半功能”。

## 7. 权威文档同步要求

- `docs/README.md`：保留本文入口，说明本文件包含审查状态、透明度诊断和下一 AI 步骤。
- `docs/architecture.md`：补充 StateTaskScope、Asset owner-thread/Lease move 约束、PNG/JPEG→RGBA8→GPU 数据流和外部 Host stop 风险。
- `docs/rendering.md`：明确 Sprite2D/UI/Transparent3D 的 alpha 基础已支持，单列 Water 高级效果缺口。
- `docs/resources.md`：删除 StaticMesh/SkinnedMesh 的 U16 残留，说明纹理 alpha、mip 派生和 source sampler authoring 缺口。
- `docs/public-api.md`：删除 U16 mesh 与旧 schema 描述，公开当前 U32 index、显式 alpha mode 和 fail-closed 规则。
- `docs/backlog.md`：TaskGroup mutex、GPU upload、TextEdit snapshot、mip 和透明基础的已有实现不再重复列待做；TaskGroup 分配异常、sRGB 重复解码与高级水能力分别记账。

## 8. 下一 AI 接手顺序

1. 读取本文，执行 `git status --short` 与 `git diff --check`。
2. 最新优先级是绘制容量与效率。先读第 4.4/4.5 节，预算透传、hash 去重与 shader 分组已有源码；核实用户场景实际撞到哪层限制，再用 probe-count、batch-count 和 CPU/GPU frame-time 分开取证，不重复实现这三项。
3. `EngineHost::stop()` 仍待修；candidate teardown 顺序已修复，补生命周期失败测试验证它。
4. World2D load rollback 与 Physics AABB double 派生已有实现；补失败注入与极端输入测试。
5. 明确 AssetStore/AssetLease move 与 owner-thread 契约，再实现单轨修复；处理 StateTaskScope deadline 和 Audio shutdown。Audio 自然结束 terminal 修复已在第 3.3 节。
6. 对水资源先做最小 Sprite2D/Transparent3D witness；基础 alpha 与 sRGB 重复解码分别取证，随后单独推进 Water shader 高级效果。
7. TaskGroup 的 mutex 修复已存在，但包装分配异常仍需处理，见 P1-6。不要重复重写丢唤醒修复。
8. 复用已有 build tree，单并发构建最小受影响 target；只有用户明确要求测试才直接运行相关 GoogleTest，smoke/gate 另按授权执行。没有运行证据时保持“待验证”。

## 9. 完成判据

- 权威文档不再把已修复的 P0-1/P0-2/P0-3、mip 生成、基础 alpha、U16 mesh 写成待做或现状。
- 待实现项与第 3.3 节待验证项严格分开；外部 Host stop、Asset lease/move、StateTaskScope deadline、TaskGroup 分配异常不能被已有局部修复掩盖。
- 水资源问题能区分“源图/导入/绑定/材质配置错误”和“引擎尚缺 Water 高级效果”，不再笼统归因于底层 shader。
- 旧 API/旧 schema 不保留兼容分支；所有 schema 变更同步 bump 并 fail closed。

## 10. 本轮构建与资源记录（2026-09-06）

全部复用常驻 `out/build/windows-msvc-vnext-bgfx-product-2d`，Debug，`--parallel 1`、`/nr:false`、
`/p:CL_MPCount=1`、`/p:BuildInParallel=false`。首轮 build 因既有 CMakeLists 时间戳自动重新 generate，
没有 clean-first，也没有新建 build tree。编译期间 shaderc 仅作为 build 工具，不是产品运行。

| 阶段 | 结果 |
| --- | --- |
| 首次最小 build | exit 1：`JsonValue::elements()` 的私有构造经 allocator 调用导致 C2672；修成成员内构造后重试 |
| Audio/Runtime/Scene/Physics2D build | exit 0：`tina_audio_tests`、`tina_runtime`、`tina_scene`、`tina_physics2d` |
| 容量改动首次 build | exit 1：Render 测试三角索引仍为 U16，与当前 U32 upload 接口不匹配；迁移两份 fixture 并补大索引用例 |
| 容量改动重试 | exit 0：`tina_tests`、`tina_render_bgfx_tests`，同时包含当前 Runtime/Render/bgfx 与相关 public-header isolation 编译 |
| 运行验证 | `testRuns=0 sampleRuns=0`，无 Visual/Benchmark 结果；未重新构建或启动 TinaEditor/sample 产品 |

产物均在上述 tree 内，时间为 +08:00：

| 相对路径 | bytes | 最后写入 |
| --- | --- | --- |
| `bin/Debug/tina_audio_tests.exe` | 1,455,616 | 00:08:47 |
| `bin/Debug/tina_tests.exe` | 20,706,816 | 00:26:12 |
| `bin/Debug/tina_render_bgfx_tests.exe` | 9,110,016 | 00:27:10 |
| `src/runtime/Debug/tina_runtime.lib` | 22,817,836 | 00:21:45 |
| `src/scene/Debug/tina_scene.lib` | 11,393,498 | 00:12:33 |
| `src/physics2d/Debug/tina_physics2d.lib` | 2,094,896 | 00:12:38 |

资源核验：常驻 buildTree 从 15,977,766,838 bytes 增至 15,989,309,116 bytes，保留用于增量构建；不删除用户缓存。
收尾进程查询 `cmake/MSBuild/cl/link/shaderc` 返回空。未创建临时 buildTree、helper、watchdog、窗口管理器、
container、volume、image 或 agent；本轮这些资源无待回收对象。cache 仅复用/更新常驻 tree，未创建一次性 cache。

下一次只有用户明确要求测试时才运行第 3.3 / 4.4 节对应 GoogleTest；产品大实例/水资源需要独立 GPU witness，
不能拿本节编译结果替代像素或性能结论。首次 Benchmark 固定 seed、相机、分辨率、VSync 与 build configuration，
记录实际触发的限制、CPU/GPU 时间与内存，再决定是否增加 streaming、greedy meshing 或持久 instance buffer。

### 10.1 资源查找与批次排序增量验证（01:06）

- 同一常驻 build tree / Debug / 单并发，未 configure，单次 build `tina_tests tina_render_scene_tests` exit 0。
- `tina_tests.exe` 最后写入 2026-09-06 01:06:38 +08:00，20,706,816 bytes；本节记录覆盖上表该产物的旧时间。
- `tina_render_scene_tests.exe` 最后写入 2026-09-06 01:06:53 +08:00，2,285,568 bytes。
- 新回归：`FrameResourceTest.LookupCollisionsKeepDescriptorIdentityAndReuseSlotsNextFrame`、
  `FrameResourceTest.LargeMixedWorkingSetDeduplicatesWithoutQuadraticScanning`、
  `RenderSceneBuilderTest.OpaqueBatchSortGroupsInterleavedShaderAndUniformBindings`，均已编译但未执行。
- `testRuns=0 sampleRuns=0`；没有 GPU/Visual/Benchmark 结论，也没有重跑前一阶段的 bgfx executable。
- 常驻 buildTree 从 15,989,309,116 bytes 增至 16,016,843,508 bytes，保留用于增量构建。收尾查询
  `cmake/MSBuild/cl/link/shaderc` 无残留；没有新建临时目录、helper、container、volume、image、一次性 cache 或 agent。
