# M12 Legacy 删除门禁清单（跟踪）

> 状态：跟踪文档，不授权删除。删除前必须取 `docs/architecture.md`、`docs/design-freeze.md`、
> `docs/roadmap.md` 适用门禁的**严格并集**。本文件只记录事实与缺口。
>
> 更新 tip：跟踪 `codex/tina-vnext-runtime`。

## 总原则

| # | 门槛 | 状态 | 证据 / 备注 |
| --- | --- | --- | --- |
| G0 | 非 clean 构建图可复现；无 wipe `out/build` 依赖 | Working | 用户硬性禁 clean |
| G1 | vNext 2D 产品门禁：`tina_sample_2d` Catalog/TileMap/角色/Box2D/UI | Partial | product-2d 主线；持续复验 300 帧 |
| G2 | vNext UI 产品门禁：HUD/设置控件 + 输入消费 | Partial | C0–C5 + Semantics；UIA/截图回归后置 |
| G3 | vNext 3D 产品门禁：Cooked + 可见 + 资源账本 | Partial | **E0–E9 竖切 Done**；cgltf 仍首 primitive/solid Unlit |
| G4 | Asset/Cooker：最终 Cooker 产物路径 | Partial | recipe + cgltf 最小 cook；multi-mesh/纹理仍薄 |
| G5 | Audio 产品门禁：miniaudio + 关闭安全 | Partial | 需持续 300 帧证据 |
| G6 | Windows/Linux 构建 + 直接 GoogleTest 全绿 | Open | 本机 Windows 为主 |
| G7 | Legacy smoke 四条最后一次基线 + 画面/资源证据 | Open | 删除前最后一次 |
| G8 | 旧接口零 include/link/call + 无僵尸 shim | Open | 删除阶段扫描 |
| G9 | 独立可回滚删除提交；合入 `dev` 复验 | Open | 最后一步 |

## 3D 产品门禁拆分（G3）

| 切片 | 内容 | 状态 |
| --- | --- | --- |
| M9-A/B/C | extraction + fixture Opaque3D/Sprite2D | **Done** |
| M11-E0–E5 | StaticMesh/Material/Texture cube path | **Done**（历史竖切） |
| M8-D0/D1/D2 | Scene 3D 组件 + extract | **Done** |
| M11-E6 / E6b | Prefab wire + parse + recipe | **Done** |
| M11-E7 | cgltf v1.15 最小 glTF cook | **Done** |
| M11-E8 | Prefab instantiate → Scene | **Done** |
| M11-E9 | `tina_sample_3d` glTF+Prefab 产品 smoke | **Done**（30/300 帧 JSON：`gltfCooked`/`prefabInstantiated`） |

## 仍不得开 M12 的原因

1. G1/G2/G5 产品证据需按删除并集复验，非“3D  alone”。
2. cgltf 首切片：无外部纹理/multi-mesh/Draco/skin；instantiate 仍 fixture meshKey。
3. Linux 全门禁、Legacy 四条最终 smoke、零引用扫描、独立删除提交未做。

## 本机快速复验（禁止 clean）

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_3d
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
# 期望 JSON 含 gltfCooked/prefabInstantiated/sceneExtract 且 exit 0
```
