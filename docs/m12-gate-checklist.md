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
| G3 | vNext 3D 产品门禁：Cooked + 可见 + 资源账本 | Partial | E0–E8 竖切齐；**E9 产品样例 glTF/Prefab 门禁未关** |
| G4 | Asset/Cooker：最终 Cooker 产物路径 | Partial | recipe + cgltf 最小 cook；完整 multi-mesh/texture 仍薄 |
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
| M11-E6 | Prefab payload 最小 hierarchy stub | **Done** |
| M11-E6b | parsePrefabFromCooked + package validation + recipe | **Done** |
| M11-E7 | cgltf v1.15 + minimal glTF → Mesh/Material/Prefab cook | **Done**（首 primitive/TRIANGLES） |
| M11-E8 | Prefab instantiate → Scene entities | **Done**（fixture mesh binding） |
| M11-E9 | `tina_sample_3d` glTF/Prefab 产品门禁 | **Open / next**（M12 硬门槛） |

## 明确未满足、不得宣称 M12 可开

1. **`tina_sample_3d` 未用 glTF/Prefab 产品路径验收**（仍以 cube recipe 为主）。
2. cgltf 首切片不支持 multi-mesh pack / 外部纹理 / Draco / skin。
3. Prefab instantiate 仍用 fixture meshKey/materialKey，非 AssetHandle 解析。
4. **Linux 全门禁**与 **Legacy 四条 smoke 最终基线包**未关闭。
5. **零引用扫描**与 **TINA_BUILD_LEGACY 默认 OFF → 删除** 未执行。

## 推荐推进顺序（继续多 worktree）

1. ~~E6 / E6b / E7 / E8~~ **Done**
2. **E9**：`tina_sample_3d`（或旁路 sample）加载 glTF cook 产物 + Prefab instantiate + 可见 smoke
3. 扩展 cgltf（纹理、多 mesh）按真实缺口
4. 冻结 M12 证据包；**再**独立删除提交

## 本机快速复验（禁止 clean）

```powershell
cmake --build --preset windows-vnext-debug --target tina_scene_tests tina_asset_tests tina_asset_format_tests
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_filter=GltfCook*
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_3d tina_sample_3d_infrastructure tina_assetc
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```
