# M12 Legacy 删除门禁清单（跟踪）

> 状态：跟踪文档，不授权删除。删除前必须取 `docs/architecture.md`、`docs/design-freeze.md`、
> `docs/roadmap.md` 适用门禁的**严格并集**。本文件只记录事实与缺口。
>
> 更新 tip：跟踪 `codex/tina-vnext-runtime`；完成一项在「状态」列改 **Done** 并写 tip/命令证据。

## 总原则

| # | 门槛 | 状态 | 证据 / 备注 |
| --- | --- | --- | --- |
| G0 | 非 clean 构建图可复现；无 wipe `out/build` 依赖 | Working | 用户硬性禁 clean |
| G1 | vNext 2D 产品门禁：`tina_sample_2d` Catalog/TileMap/角色/Box2D/UI | Partial | product-2d 主线 A32–A44 已收口；持续复验 300 帧 |
| G2 | vNext UI 产品门禁：HUD/设置控件 + 输入消费 | Partial | C0–C5 + Semantics；UIA/截图回归后置 |
| G3 | vNext 3D 产品门禁：Cooked + 可见 + 资源账本 | Partial | E0–E5 cube/Unlit + Scene extract；**缺 glTF/Prefab 实例化** |
| G4 | Asset/Cooker：最终 Cooker 产物路径 | Partial | recipe/assetc 子集；Prefab wire Done；**cgltf Deferred** |
| G5 | Audio 产品门禁：miniaudio + 关闭安全 | Partial | A7–A20 竖切；需持续 300 帧证据 |
| G6 | Windows/Linux 构建 + 直接 GoogleTest 全绿 | Open | 本机 Windows 为主；Linux 需另机 |
| G7 | Legacy smoke 四条最后一次基线 + 画面/资源证据 | Open | 删除前最后一次 |
| G8 | 旧接口零 include/link/call + 无僵尸 shim | Open | 删除阶段扫描 |
| G9 | 独立可回滚删除提交；合入 `dev` 复验 | Open | 最后一步 |

## 3D 产品门禁拆分（G3）

| 切片 | 内容 | 状态 |
| --- | --- | --- |
| M9-A/B/C | extraction + fixture Opaque3D/Sprite2D | **Done** |
| M11-E0–E5 | StaticMesh/Material/Texture + `tina_sample_3d` | **Done** |
| M8-D0/D1/D2 | Scene 3D 组件 + infrastructure/product Scene extract | **Done** |
| M11-E6 | Prefab payload 最小 hierarchy stub | **Done**（wire + tests；无 instantiate/cgltf） |
| M11-E7+ | cgltf → StaticMesh/Texture/Material/Prefab cook | **Open / next** |
| M11-E8 | Prefab instantiate → Scene entities | **Open** |
| M11-E9 | `tina_sample_3d` glTF/Prefab 产品门禁 | **Open**（M12 硬门槛） |

## 明确未满足、不得宣称 M12 可开

1. **glTF/cgltf Cooker** 未落地（ADR 0009 Accepted，实现 Deferred）。
2. **Prefab 实例化到 Scene** 未落地。
3. **正式 3D 产品样例**尚未证明 glTF/Material/Prefab 路径（cube recipe 不替代）。
4. **Linux 全门禁**与 **Legacy 四条 smoke 最终基线包**未在本跟踪表关闭。
5. **零引用扫描**与 **TINA_BUILD_LEGACY 默认 OFF → 删除** 未执行。

## 推荐推进顺序（继续多 worktree）

1. ~~关闭 M8-D2：`tina_sample_3d` Scene extract~~ **Done**
2. ~~完成 M11-E6 Prefab wire + tests~~ **Done**
3. 引入 cgltf v1.15（仅 `tina_assetc` PRIVATE）最小 glTF→mesh/material/texture/prefab。
4. Prefab instantiate API + sample。
5. 冻结 M12 删除 checklist 证据包；**再**独立删除提交。

## 本机快速复验（禁止 clean）

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_3d tina_sample_3d_infrastructure
cmake --build --preset windows-vnext-debug --target tina_scene_tests tina_asset_format_tests
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d_infrastructure.exe --frames=30 --frame-delay-ms=0
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe --gtest_filter=Prefab*
```
