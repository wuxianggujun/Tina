# 新会话提示词：Scene3DRuntime

把下面整段贴给新会话。它假设对方对本仓库一无所知。

---

## 任务

在 Tina（C++23 游戏 Runtime，仓库根 `C:\Users\wuxianggujun\CodeSpace\CMakeProjects\Tina`，
分支 `codex/tina-vnext-runtime`）里建立 **3D authored 场景的运行时所有者**，即 2D 侧
`Scene2DRuntime` 的 3D 对应物。目前它不存在。

**先写 ADR，等我裁决后再写代码。** 不要一上手就建 target。

## 为什么需要它（已验证的现状）

- `Scene2DRuntime`（`include/tina/gameplay2d/Scene2DRuntime.hpp`，ADR 0031 Proposed 但已实现）
  拥有 2D authored 节点的实例化、AssetLease 生命周期和固定每帧顺序：
  `updateDemand → (调用方 pump AssetSystem) → commitReady → fixedUpdate → fixedUpdatePhysics → extract`。
  它存在的理由写在该头文件注释里：这些顺序错误全部是**静默**的（提前 extract 只是少画，
  提前释放 audio lease 是 use-after-free）。
- **3D 侧没有任何等价物。** 全仓 `Scene3DRuntime` 零命中，也没有 `gameplay3d` 模块；
  `ResourceBinding3D` / `World3DSnapshot` 同样零命中——2D 的 authored resource 绑定组件
  （`include/tina/scene/ResourceBinding2D.hpp`）在 3D 没有对应物。
- 后果是每个 3D 游戏自己手写编排：`samples/3d_product/platforms/desktop/DesktopMain.cpp` **3606 行**，
  `samples/3d_voxel/platforms/desktop/DesktopMain.cpp` 2309 行。ADR 0031 当年正是拿 2D sample 的 6660 行作为论据。
- 3D 骨骼 pose 的所有权**故意在引擎之外**：`ExtractRenderScene.hpp:16` 的
  `SkinnedPose3DProvider` 是调用方拥有的回调，不是 ECS 组件；`:45` 把它挂在 extraction 参数上。
  没有 provider 或空 span 会返回 `UnresolvedSkinnedPose` 并**让整帧失败**（fail-closed）。
- `Scene::Animator3D`（`include/tina/scene/Animator3D.hpp`）是单骨架单 clip 的 CPU 求值器；
  `Tina::Animation3D`（ADR 0037 Accepted）在它旁边提供 pose 图。两者都没有「谁在每帧驱动它们」的所有者。
- `PrefabPayload` 只有 `meshId`/`materialId`（`:102-103`、`:127-128`），**没有 clip 字段**，
  所以 authored 3D 内容目前无法表达「这个节点播哪个动画」。这可能是本任务最大的设计缺口。

## 硬约束（违反即返工）

1. **不保留兼容旧设计的代码。** 我明确要求：不要为「以后可能有人用旧写法」留转发、别名、
   `_unused` 变量、`// removed` 注释或 feature flag。要删就删干净。参照 ADR 0041 的先例
   （编辑器退出 SDK 包，breaking，无兼容转发）。
2. **依赖方向只能指向 Scene，不能反向。** 参考 `src/gameplay2d/CMakeLists.txt:19-24` 的注释与
   ADR 0031 的依赖表：`tina_scene` 只链 Core/Render/AssetFormat/AssetTypes；
   `tina_runtime` 既不链 Scene 也不链 Asset，**所以接线只能在 game/app 层**。
   新模块（若你提议 `Tina::Gameplay3D`）必须自证无环，方式是读 `CMakeLists.txt` 而不是猜。
3. **固定容量、generation handle、`Result`/`Status`。** 超限是 `CapacityExceeded` 而不是重新分配。
   运行时状态放 side table（按 `EntityId` 索引），组件里不存 `AssetLease`/运行时句柄——
   否则 `tina_scene` 会被迫链接 Asset（ADR 0031 D3）。
4. **不可移动。** `Scene2DRuntime` 显式删掉了拷贝和移动，因为它把内部引用交给了别的对象。
   3D 版若同样交出引用，也要把移动变成编译错误而不是写在文档里。
5. **公开头零第三方类型。** 不出现 bgfx/Box2D/cgltf 等。
6. **不要碰工作区里已经 dirty 的文件。** 常有 2+ 会话并行；先跑
   `git status --short | grep -v "^R"`，改动前确认目标文件不在别人手里。
7. **不要动 `Scene2DRuntime` 的行为**去「统一 2D/3D」。3D 的物理后端（Jolt）根本不存在
   （`docs/physics.md:5-8`：Jolt 未进 manifest/源码/target），所以 3D 版**不能**照抄
   `fixedUpdatePhysics()`。

## 必须自己查清再下结论的问题

不要假设，去读代码然后在 ADR 里写明证据路径与行号：

- 3D 的 authored 内容现在到底能表达什么？看 `PrefabPayload`、Editor 的 World3D document、
  `docs/game-3d.md`。**需不需要先有 `ResourceBinding3D` 这类组件？** 如果需要，那是本任务的前置切片，
  而不是顺手加。
- `SkinnedPose3DProvider` 的 caller-owned 回调形态要不要保留？我倾向**保留**（它是刻意设计，
  ADR 0037 与该头注释都在支撑），那么新 runtime 的角色是「实现一个 provider」而不是「取代 seam」。
  若你认为该换，必须论证，并说明 fail-closed 语义怎么保。
- 3D 有没有等价于 TileMap 的**流式**资源？没有的话，`updateDemand`/`commitReady` 这两步在 3D
  可能根本不需要——**不要为了对称照搬 2D 的六步**。
- 动画驱动归谁：`Animator3D`（Scene 层）与 `AnimationGraph3D`（Animation3D 模块）都存在，
  新 runtime 应该驱动哪个、还是两个都支持？
- 谁来喂 `MeshRenderer3D` / `SkinnedMeshRenderer3D` 的 GPU 资源解析？先读
  `include/tina/scene/ExtractRenderScene.hpp` 和 `AssetFrameResourceResolver`。

## 交付物

**第一步只交 ADR。** 编号取当前最大 + 1（现在最大是 0045，故大概是 0046；先 `ls docs/adr/` 确认）。
格式照 `docs/adr/0031-scene-2d-runtime-ownership.md`：背景（带证据路径行号）→ 待确认决策表
（D1..Dn，每行「决策点 / 推荐 / 主要备选与取舍」）→ 决定 → 结果（含成本与限制）→ 被拒绝方案。
状态 **Proposed**。中文。

同时挂进索引：`docs/adr/README.md` 表格、`docs/design-freeze.md` 的 Proposed 表、
`docs/roadmap.md`、`docs/backlog.md`（新 ID，建议 `SCENE3D-001`）。参照上一轮 ADR 0045 的做法。

**Proposed 阶段不占位任何 API**：不建头文件、不加 CMake target、不占 `ErrorDomain`/`MemoryTag`。
这是本仓规矩，见 ADR 0027 顶部的更正与 `METRICS-001` 的 Blocked 状态。

我裁决 D 表之后，你才进入实现切片；那时的验收要求：header-isolation TU、GoogleTest
executable 实跑（本仓不用 CTest）、以及**把某个 3D sample 的手写编排真正替换掉**——
新增一个 owner 却没人用，就是又一个「有 payload 无消费面」（`FX-ASSET-001` 的教训）。

## 本仓的工作方式

- 验证 = 真实执行 + 退出码。不要用「我推理过」代替跑命令。
- 构建命令见 `docs/building.md`；测试直接跑 GoogleTest exe，别用 `--clean-first`，别删 `out/build`。
- 文档改完跑 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/docs/CheckDocs.ps1`，
  要求 `errors=0`；输出里没有 `markdown_files=` 那行说明脚本崩了，不是通过。
  （已知既有 warning 两条，在 `docs/physics.md` 与 `docs/testing.md`，与本任务无关。）
- 文档冲突优先级：当前源码/CMake/运行结果 > Accepted ADR + design-freeze > 主题文档 > 历史证据。
  `docs/backlog.md` 的状态经常落后于代码，**grep 过再相信**。
- 全部回复与文档用中文；代码、API、报错信息保持原文。
