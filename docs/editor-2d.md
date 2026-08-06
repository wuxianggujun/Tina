# 2D Editor

## 产品场景

`2D-EDITOR` 的首个可闭环场景是：工具打开当前 schema-v1 World2D snapshot，Hierarchy/Inspector/gizmo 把一次
用户意图提交为一个 authoring revision，Undo/Redo 切换已经验证的 revision，Cook Preview 直接把当前 snapshot
交给 Runtime parser 与 Scene instantiate。工具不能绕过 `AssetFormat` 写半合法数据，也不维护 editor-only 或旧
schema 兼容格式。

独立 `Tina::Editor` target、`World2DAuthoringDocument` 与 `World2DAuthoringFile` 已提供。`tina_sample_editor_shell` 现已把 Hierarchy/
Inspector 的 `Move X +1`、Undo、Redo 接到真实 document，并在每次成功命令后把同一份 canonical snapshot 解析、
实例化到新的 `Scene::World` 做 runtime preview 验证。当前 shell 的 retained UI 布局也已完整铺开：Toolbar、上下文
工具条、Hierarchy dock、World2D viewport 工作区、可滚动 Inspector（Identity/Transform/Components/Authoring/
Document）和底部 status bar 均由 `Flex`、`minMax`、固定控件高度与滚动容器组合；窗口变大时 viewport 与中间工作区增长，
两侧 dock 保持 bounded width。传入 `--document-path=<UTF-8 path>` 后 Toolbar Save 会原子保存当前 canonical snapshot，
并让 Toolbar/Inspector/status bar 依据 saved baseline 显示 `Modified/Saved`；未配置路径时 Save 保持 disabled。
Transform 输入、真实 viewport 绘制和非 Move 的 Inspector commit 目前明确为只读/占位状态，不伪装成已完成的
document transaction。真实渲染 viewport、TileMap/动画专用 document
仍是后续切片；它们必须复用这里的 revision/failure 语义，不各自实现一套 undo stack 或 cooked preview。

## Editor shell layout

```text
Toolbar (document/path/mode/undo/redo/save)
Context bar (breadcrumb/select/move/frame/snap)
Workspace
  Hierarchy dock (filter/actions/virtual TreeView/selection summary)
  World2D viewport (mode/tools/zoom/preview canvas/footer)
  Inspector dock (scrollable identity/transform/components/authoring/document)
Status bar (schema/entities/revision/preview/selection)
```

这层只属于 `samples/editor_shell` 的组合根，`Tina::Editor` 公共头仍不依赖 UI、Runtime、Scene 或 backend。布局 smoke 的
结构化输出额外报告 `editorLayoutRegions=6`、`viewportLayoutReady=true` 和 `inspectorScrollConfigured=true`；这些字段
只证明 retained tree 已建立，不证明真实 GPU viewport。文件保存另由 `authoringSaves`、`savedSnapshotBytes`、
`documentSaved`、`documentDirty` 和落盘 bytes 取证。

## 原子文件保存

`saveWorld2DAuthoringDocument(utf8Path, document)` 只读取 document 已发布的 `snapshotBytes()`，通过 Core
`writeFile()` 在目标同目录写完整临时文件，再用 OS 原子 replace 发布；缺失父目录会创建。Windows 使用
`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`，其他平台使用同目录 rename。replace 失败时删除临时
文件但不先删除旧目标，document、revision 与 undo/redo 也完全不变。

Shell 在真正写入前先准备 saved baseline，避免“文件已保存但 UI 因随后分配失败仍报成功”的半状态。保存成功后以
canonical bytes 与 saved baseline 的完整相等比较判断 dirty；因此保存后编辑再 Undo 回 saved bytes 会恢复 `Saved`，
而不是仅按单调 revision 误报 `Modified`。Save 是 Editor command，不是 authoring revision，也不触发多余的
`Scene::World` preview instantiate。

## 模块边界与数据流

```text
Inspector / gizmo / importer intent
  -> World2DAuthoringDocument candidate
  -> AssetFormat current-schema validate + canonical write
  -> bounded revision publication
  -> snapshotBytes()
  -> AssetFormat::parseWorld2DSnapshot() -> Scene::instantiateWorld2DSnapshot() / product preview
  -> saveWorld2DAuthoringDocument() -> Core atomic sibling replace
```

- `Editor` 依赖 `Core` 与 `AssetFormat`，不依赖 UI、Runtime、Scene 或 backend；
- `Editor` 是工具侧已安装 target，但不由 `Tina::GameSDK` 聚合链接；
- 持久化身份仍只有 stable entity/parent ID 与 `AssetId`，没有 Runtime `EntityId`、generation、Handle、Lease 或 GPU
  identity；
- `snapshotBytes()` 是唯一 preview/cook 输入，不生成第二份语义相近的 editor wire payload。

## 容量边界

| 预算 | 默认 | hard limit / 规则 |
| --- | ---: | --- |
| entity | 4096 | `World2DSnapshotWire::MaximumEntities` |
| gameplay bytes | 4 MiB | `World2DSnapshotWire::MaximumGameplayBytes` |
| history entries（含 current） | 32 | 2..256 |
| history canonical bytes（含 current） | 16 MiB | 64 bytes..1 GiB |

history vector 在 Create 时一次 reserve 到配置 entry 上限。发布新 revision 前先裁剪 redo，并按 entry/byte budget
淘汰最老 revision；不会通过隐式扩容突破配置。为了让每个成功编辑至少有一步 Undo，current 与 candidate 必须能
同时放入 history byte budget，否则本次编辑失败。

## 编辑与状态流

- `replace(desc)`：完整 batch transaction，适合多字段 Inspector commit、gizmo drag end 或 importer；
- `loadSnapshot(bytes)`：只接受完整通过当前 parser 的 snapshot，再按当前 writer 规范化并原子建立新 baseline；
  成功清空 undo/redo，旧 schema 或容量失败保留原 document/history；
- `upsertEntity(entity)`：按 stable ID 替换或追加一个 entity，parent 仍必须指向此前 entity；
- `eraseEntitySubtree(id)`：按 topological authoring order 删除目标及全部后代，避免悬空 parent；
- `setGameplay(schema, version, bytes)`：游戏自有 blob 仍要求“空 blob ↔ 零 schema/version、非空 blob ↔ 非零”；
- `undo()` / `redo()`：只移动已发布 revision cursor，不重新解析、不分配。

相同 canonical bytes 是 no-op，不增加 revision 或裁剪 redo。成功 edit/undo/redo 单调推进 document revision；达到
`u64` 最大值后饱和，不回绕。

## 失败语义

候选在任何 live mutation 前完成 document capacity、current schema validation、canonical serialization 与
history 可撤销性检查。以下失败均保留 current bytes、revision、undo depth 与 redo depth：

- stable ID 为零/重复、parent 自引用/前向引用/缺失；
- transform/component 非有限、枚举/flag/AssetId/Gameplay identity 非法；
- 输入为旧 schema、非 canonical wire bytes 或截断 payload；
- entity/gameplay 超出 document config；
- current + candidate 超出 history byte budget；
- 分配失败。

Undo/Redo 无对应 revision 时分别返回 `UndoUnavailable` / `RedoUnavailable`，不改变 cursor。

## 验收

小切片只运行独立 target、精确 filter 与对应 sample 短 smoke：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_tests tina_sample_editor_shell --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_editor_tests.exe `
  --gtest_filter=World2DAuthoringFileTests.*
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_editor_shell.exe `
  --frames=60 --frame-delay-ms=0 `
  --document-path=artifacts/editor_shell/smoke/world2d.tworld
```

Core 另保留 `WriteFileTests.FailedAtomicReplacePreservesExistingTargetDirectory` 回归。由于它目前位于 monolithic
`tina_tests`，小型 Editor 保存切片不为单个 filter 重编整个 Runtime 测试 executable；下一次大功能统一 Core gate
再执行。当前 Editor file overwrite 用例会真实经过 Windows `MoveFileExW` 覆盖路径。

人工操作按钮时使用较长帧数并传 `--no-auto-demo`；该模式仍验证 document 与 runtime preview 一般不变量，
但不把用户产生的 revision 数量误判为自动 smoke 失败。

document 与 shell 接线切片关闭需要：canonical preview 与 AssetFormat writer bytes 完全一致；非法 edit 原子失败；
subtree 删除；bounded undo/redo、branch replacement 与 history byte failure；旧 schema 拒绝；Editor 三个公共头
isolation 编译通过；文件保存 exact canonical bytes、覆盖失败不删除旧目标；shell 自动完成
edit → undo → redo → save，在四个 revision 状态均成功实例化 runtime preview，Save 不增加第五次 instantiate。

后续产品切片依次为：真实渲染 viewport 与 Inspector 字段 transaction；TileMap root+chunk authoring/cook；
SpriteAnimationClip timeline authoring/cook。每个切片继续只保留现行 schema，不增加旧资产兼容分支。
