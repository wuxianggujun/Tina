# 2D Editor

## 产品场景

`2D-EDITOR` 的首个可闭环场景是：工具打开当前 schema-v1 World2D snapshot，Hierarchy/Inspector/gizmo 把一次
用户意图提交为一个 authoring revision，Undo/Redo 切换已经验证的 revision，Cook Preview 直接把当前 snapshot
交给 Runtime parser 与 Scene instantiate。工具不能绕过 `AssetFormat` 写半合法数据，也不维护 editor-only 或旧
schema 兼容格式。

首切片提供独立 `Tina::Editor` target 与 `World2DAuthoringDocument`。现有 `tina_sample_editor_shell` 的交互接线、
文件原子保存、TileMap/动画专用 document 是后续切片；它们必须复用这里的 revision/failure 语义，不各自实现一套
undo stack 或 cooked preview。

## 模块边界与数据流

```text
Inspector / gizmo / importer intent
  -> World2DAuthoringDocument candidate
  -> AssetFormat current-schema validate + canonical write
  -> bounded revision publication
  -> snapshotBytes()
  -> AssetFormat::parseWorld2DSnapshot()
  -> Scene::instantiateWorld2DSnapshot() / product preview
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
- `loadSnapshot(bytes)`：只接受完整通过当前 parser 的 snapshot，再按当前 writer 规范化；旧 schema 直接失败；
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

小切片只运行独立 target 与精确 filter：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug --target tina_editor_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_editor_tests.exe `
  --gtest_filter=World2DAuthoringDocumentTests.*
```

首切片关闭需要：canonical preview 与 AssetFormat writer bytes 完全一致；非法 edit 原子失败；subtree 删除；bounded
undo/redo、branch replacement 与 history byte failure；旧 schema 拒绝；Editor 两个公共头 isolation 编译通过。

后续产品切片依次为：editor shell 可交互 Scene Inspector/Undo/Redo、原子文件保存与 runtime preview；TileMap root+
chunk authoring/cook；SpriteAnimationClip timeline authoring/cook。每个切片继续只保留现行 schema，不增加旧资产兼容
分支。
