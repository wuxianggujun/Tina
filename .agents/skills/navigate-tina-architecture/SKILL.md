---
name: navigate-tina-architecture
description: 梳理 Tina C++23 游戏 Runtime 的当前模块依赖、公开 API/SPI、所有权、ADR、Roadmap 与 Legacy 退役边界。Use when planning cross-module changes、增加或拆分 CMake target、修改公开头/依赖方向、判断实现与文档状态、设计迁移切片，或核验第三方泄漏和已删除产品残留。
---

# Tina 架构导航

## 建立权威事实

在仓库根目录先执行：

```powershell
git status --short
cmake --list-presets
```

保留用户未提交修改，并按顺序阅读：

1. `README_CN.md`、`docs/README.md`、`docs/architecture.md`、`docs/design.md`。
2. `docs/design-freeze.md` 与 `docs/adr/README.md`。
3. `docs/backlog.md` / `docs/roadmap.md`，再读任务对应主题文档、公开头、CMake target 与测试。
4. 必要时用 `git log -- <paths>`、`git diff -- <paths>` 和实际结果判断文档漂移。

源码/CMake/实际运行结果回答“已经实现什么”；Accepted ADR/设计冻结回答“允许怎样实现”；Backlog
回答“当前做什么”。Skill 只保存导航规则，不作为完成度数据库。

## 当前模块图

```text
Core
├─ Platform ── private PlatformGlfw
├─ Task
├─ Render ──── private RenderBgfx
├─ UI (+ Platform) ── private UIFreetype / UIUia
├─ Audio ───── private AudioMiniaudio
├─ AssetFormat
├─ AssetTypes (+ Render)
├─ Scene (+ Render + AssetFormat + AssetTypes)
├─ Asset (+ AssetTypes + AssetFormat + Task + Render)
├─ Physics2D (optional private Box2D)
├─ WindowSurfaceIntegration (+ Platform)
├─ UIRenderIntegration (+ UI + Render)
└─ Runtime (+ Platform + Task + Render + UI + Audio + integrations)
   └─ DesktopBootstrap ── private GLFW/bgfx/Task/optional adapters
```

以各模块 `CMakeLists.txt` 为最终事实。`Scene`/`Asset`/`Physics2D` 当前主要由产品 State/Resources owner
组合，不要仅因 target 存在就塞入 `EngineHost`。

## 三层 API

| 层 | 使用者 | 规则 |
| --- | --- | --- |
| Game API | 普通游戏 | `Desktop::CreateEngine` + Tina 模块 facade；不拿 backend/native owner |
| Module API/SPI | Tina 模块与高级集成 | `include/tina/<module>` 与窄 integration/runtime SPI |
| Backend Private | GLFW/bgfx/FreeType/UIA/miniaudio/Box2D/cgltf/stb | 第三方类型、宏、native payload 与链接保持最窄 PRIVATE |

修改 target/public header 时检查：

- PUBLIC 依赖是否只表达调用者真实需要的 Tina 类型。
- adapter 关闭时基础 public header/header-isolation 是否仍独立编译。
- Null 图是否排除 GLFW/bgfx/shader 与无关 feature。
- 静态库 PRIVATE 依赖是否会通过安装/export 转为 consumer 的 link requirement。
- 是否需要外部 installed consumer，而不只是在 monorepo 中依赖传递 include。

快速扫描：

```powershell
rg -n "bgfx::|BGFX_|GLFW|GLFWwindow|entt::|EASTL|HWND|wl_|xcb_|X11" include\tina
rg -n "target_link_libraries|target_include_directories|add_library|add_executable" `
  --glob CMakeLists.txt -g "!thirdparty/**" -g "!dependencies/**" -g "!out/**" -g "!build/**" .
```

`include/tina/integration` 可以承载窄 Module SPI，但不得暴露真实 native 类型或任意 `void*` escape hatch。

## 规划跨模块切片

1. 写清当前事实、目标状态、唯一 owner 与最小垂直切片。
2. 标出 borrowed view 失效点、commit/publish 点、失败回滚、shutdown/join/retirement 顺序。
3. 优先复用 `Core::Result/Status`、generation handle、FrameArena、PlatformFrameBuilder、
   RenderFramePacket/FramePin、Asset Handle/Lease 与现有 factories。
4. 先建立新边界和 replacement tests，再迁移调用点；临时 bridge 必须有真实消费者和删除条件。
5. 新公开模块同时处理 header、实现 target、`Tina::<Module>` alias、header-isolation、测试与文档。
6. Proposed 决策先由用户确认并更新 ADR/设计冻结；反转 Accepted 决策必须新增 ADR supersede 旧记录。
7. 同步当前事实文档、Backlog/Roadmap 与相关 Skill；不要把阶段流水复制到所有主题文档。

## 保护关键契约

- `EngineHost` 是唯一非全局组合根；`IGameApplication` lifecycle-only，帧行为属于 State stack。
- 输入顺序保持 Platform -> UI consume/claim -> Action；State policy 不回改已完成的 UI route。
- Runtime packet、Asset retirement 与 GPU backend completion 分账，不能用一种 ticket 证明全部寿命。
- Scene/Prefab/TileMap/FX 保存 weak AssetHandle；Render item 使用 packet-local FrameResourceRef。
- UI route/action 类型留在 `Tina::UI`；Runtime 通过窄 capability/integration 消费，不复制第二套 ABI。
- 每个固定容量 owner 都有明确 capacity、失败原子性和 teardown 门禁。

## Legacy 与历史文档

Legacy `Tina.exe`、旧横版 2D、旧 UI 产品图、CoreLegacy/EASTL 产品路径已删除；
`TINA_BUILD_LEGACY=ON` 永久 FATAL。不要恢复 `src/game`、`src/engine` 或第二套 UI。

Accepted ADR 可保留历史名称和当时事实；当前主题文档、Skill 与 CMake 不得继续把历史状态写成现状。
删除残留前同时检查 source/header/target/feature/resource/test consumer，零文本命中不是充分条件。

## 验证

使用 `$build-and-test-tina` 选择最小 target，直接运行 GoogleTest executable。跨模块/public API 变更扩大到
header-isolation、外部 consumer、第三方 token scan 和对应产品 gate。最后检查 `git status --short`、
`git diff --check` 与完整 diff；`git diff` 不会显示 untracked 文件。
