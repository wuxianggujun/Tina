---
name: develop-tina-vnext-runtime
description: 实现或排查 Tina C++23 Runtime 的 Core、Platform/Input、Task、Scene、Asset、Render、Audio、WindowSurface、GLFW/bgfx adapter 与 Desktop bootstrap。Use when changing include/tina 下的非 UI Runtime 模块、对应 src 目录、生命周期/帧相位、State stack、输入映射、资源与 submission ownership 或 backend 组合；Retained UI 内部改动改用 develop-tina-ui。
---

# 开发 Tina Runtime

## 先建立当前事实

不要把本 Skill 的能力摘要当作完成度来源。先在仓库根目录执行 `git status --short`，保留用户改动，
再按任务阅读：

1. `docs/architecture.md`、`docs/public-api.md`、`docs/runtime.md`、`docs/design-freeze.md`。
2. 对应主题文档、Accepted ADR、公开头、模块 `CMakeLists.txt`、实现和测试。
3. `docs/backlog.md` 只判断当前任务状态；源码/CMake/实际运行结果判断已经实现什么。

当前产品基线包括 State stack/commands/policy、bounded TaskSystem、Scene/Asset/Audio、Null/bgfx Render、
packet-local frame resource、Texture/Mesh retirement、GLFW WindowSurface 与 Desktop bootstrap。不要恢复旧的
“单 State、DisabledTaskSystem-only、digital-only input、clear-only bgfx”假设。

## 选择模块

| 范围 | 路径 | 先读 |
| --- | --- | --- |
| Core/错误/时间/内存 | `include/tina/core`、`src/core` | `docs/core.md`、ADR 0004/0007/0019 |
| Platform/Input | `include/tina/platform`、`src/platform` | `docs/platform-input.md`、ADR 0005/0015/0020 |
| Runtime/Task/Desktop | `include/tina/{runtime,task,desktop}`、对应 `src` | `docs/runtime.md`、`docs/task-system.md`、ADR 0014/0017/0021 |
| Scene/Asset | `include/tina/{scene,asset,asset_format,asset_types}`、对应 `src` | `docs/scene-ecs.md`、`docs/resources.md`、ADR 0009/0013/0016 |
| Render/backend | `include/tina/render`、`src/render` | `docs/rendering.md`、ADR 0008/0020 |
| Audio/Physics | `include/tina/{audio,physics2d}`、对应 `src` | `docs/audio.md`、`docs/physics.md`、ADR 0010/0012 |
| Retained UI | `include/tina/ui`、`src/ui` | 改用 `$develop-tina-ui` |

## 保护架构边界

- `EngineHost` 是唯一非全局组合根；不新增 Singleton、Service Locator 或第二套主循环。
- `IGameApplication` 只负责生命周期；逐帧行为属于 `IGameState`，结构命令只在唯一 commit 点生效。
- 普通游戏使用 `Desktop::CreateEngine`；显式 factory 只用于高级集成、adapter sample 与测试。
- 公共边界只使用 Tina-owned 类型和 `Core::Result/Status`。GLFW、bgfx、Box2D、miniaudio、FreeType、
  cgltf、stb_image 与 native handle 留在最窄 PRIVATE adapter/Cooker。
- phase/view/span/string_view 都记录精确失效点；AssetHandle 是弱身份，Lease/FramePin/retirement record
  各自证明不同寿命，不能互相替代。
- generation handle 同时验证 owner/index/generation；禁止手工构造、跨 registry 混用或持久化 runtime ID。
- 固定容量队列、snapshot、packet 和 registry 必须显式失败，不隐式扩容或切换系统 heap。

## 保持当前帧序

```text
Platform poll/validate
-> lifecycle dispatch
-> UI route/consume/claim
-> Action mapping
-> 0..N fixedUpdate (State stack policy)
-> updateFrame
-> commit State commands
-> Audio completion
-> begin RenderFramePacket + extract RenderScene
-> update/commit UI + build DisplayList
-> submit + present/skip
-> complete/abandon packet + latch presented Camera2D
```

不要把 UI route 放到 ActionMapper 之后；不要让 `blocksUIUpdateBelow` 回改本帧已完成的 route；
`blocksGameplayInputBelow` 只给下层空 action snapshot，不跳过其 frame callback。

## 创建、关闭与资源事务

- Factory 创建成功后立即进入明确 RAII owner；失败保留第一个结构化错误并逆序回滚。
- WindowSurface lease 晚于 Platform window 创建、早于 Platform 释放；suspended 0x0 surface 继续 CPU 帧，
  Render skip 不伪造 submit/present。
- Task shutdown 必须有界 stop/join；timeout 保留 owner 供重试，Host hard boundary 不继续析构被 worker 引用的对象。
- Render item 只保存 packet-local `FrameResourceRef`；Scene/Prefab/TileMap/FX 保存 weak AssetHandle。
- Sprite/Mesh registry 是 resident Lease/GPU/binding owner；retirement 只有 backend 接受后才消费 owner，
  active frame pin 或失败必须保留可重试状态。
- source asset 只在 Cooker 读取；Runtime 只消费经过校验并原子发布的 Cooked Catalog。

## 修改后选择验证面

先读取 `docs/building.md`、`docs/testing.md` 并使用 `$build-and-test-tina`。只构建受影响 target，直接运行
GoogleTest executable，不使用 CTest，不 `--clean-first`。公开头变更要补 header-isolation/外部 consumer
并扫描第三方 token；生命周期/backend 变更再扩大到对应 sample 与产品 gate。
