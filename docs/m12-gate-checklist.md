# M12 Legacy 删除门禁清单（跟踪）

> 状态：跟踪文档，不授权删除。删除前必须取 `docs/architecture.md`、`docs/design-freeze.md`、
> `docs/roadmap.md` 适用门禁的**严格并集**。本文件只记录事实与缺口。
>
> 更新 tip：跟踪 `codex/tina-vnext-runtime`。  
> 证据摘录：[m12-evidence-windows.md](m12-evidence-windows.md)。  
> **删 Legacy UI 专项盘点**：[m12-legacy-ui-retirement.md](m12-legacy-ui-retirement.md)（当前 **Blocked**）。

## 总原则

| # | 门槛 | 状态 | 证据 / 备注 |
| --- | --- | --- | --- |
| G0 | 非 clean 构建图可复现；无 wipe `out/build` 依赖 | Working | 用户硬性禁 clean |
| G1 | vNext 2D 产品门禁：`tina_sample_2d` Catalog/TileMap/角色/Box2D/UI | **Partial→Evidence** | Windows Debug 300 帧 exit 0（见 evidence）；本树 `productGate=bgfx`（physics/freetype 未开） |
| G2 | vNext UI 产品门禁：HUD/设置控件 + 输入消费 | Partial | 随 sample_2d 有 UI panels/labels/buttons；UIA/截图回归后置 |
| G3 | vNext 3D 产品门禁：Cooked + 可见 + 资源账本 | **Partial→Strong** | **E0–E9 Done**；cgltf 仍首 primitive/solid Unlit |
| G4 | Asset/Cooker：最终 Cooker 产物路径 | Partial | recipe + cgltf 最小 cook；multi-mesh/纹理仍薄 |
| G5 | Audio 产品门禁：miniaudio + 关闭安全 | **Partial→Evidence** | `tina_audio_tests` 15/15；sample_2d 300 帧 AudioClip lease + one-shot 观测 |
| G6 | Windows/Linux 构建 + 直接 GoogleTest 全绿 | Open | 本机 Windows 为主；Linux 未跑 |
| G7 | Legacy smoke 四条最后一次基线 | **N/A** | 用户授权退役产品；源码已删，不再跑 Legacy smoke |
| G8 | 旧接口零 include/link/call | **In progress** | 产品源码已删；构建图默认 OFF + FATAL；扫尾 docs/submodule |
| G9 | 独立可回滚删除提交 | **In progress** | 本批删除提交 |

## 3D 产品门禁拆分（G3）

| 切片 | 内容 | 状态 |
| --- | --- | --- |
| M9–M11-E9 | fixture → cube → Scene → Prefab → cgltf → product sample | **Done**（见 checklist 历史行） |

## 仍不得开 M12 的原因

1. G1 全 feature 图（physics+freetype+真实 miniaudio device）若要求更严 `productGate`，需 product-2d preset 复验。
2. G6 Linux、G7 Legacy 四条最终 smoke、G8 零引用、G9 独立删除提交未做。
3. cgltf 仍薄（无 multi-mesh/外部纹理）；instantiate 仍 fixture meshKey。
4. ~~Legacy UI 零引用~~ **产品源码已删**（见 [m12-legacy-ui-retirement.md](m12-legacy-ui-retirement.md)）。剩余：docs/submodule/`resources` 扫尾与 vNext 门禁复验。

## 本机快速复验（禁止 clean）

```powershell
# G1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
# G3 E9
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
# G5 unit
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_audio_tests.exe
```
