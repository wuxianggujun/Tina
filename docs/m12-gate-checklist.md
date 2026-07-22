# M12 Legacy 删除门禁清单（跟踪）

> 状态：**产品级 Legacy 源码与构建图已删除**（`e2ef3d5e` 起）。本文件跟踪**剩余扫尾与复验**，
> 不再写「禁止删除 UI」——删除已执行。  
> 证据：[m12-evidence-windows.md](m12-evidence-windows.md) ·  
> 退役说明：[m12-legacy-ui-retirement.md](m12-legacy-ui-retirement.md)

## 总原则

| # | 门槛 | 状态 | 证据 / 备注 |
| --- | --- | --- | --- |
| G0 | 非 clean 构建图可复现；无 wipe `out/build` | Working | 用户硬性禁 clean |
| G1 | 2D 产品：`tina_sample_2d` Catalog/TileMap/UI | **Evidence** | Windows Debug 300 帧 exit 0；全 feature 图可另验 |
| G2 | UI 产品：HUD/控件 + 输入消费 | Partial | 随 sample_2d；UIA/截图后置 |
| G3 | 3D 产品：Cooked + 可见 + 账本 | **Strong** | E0–E9 Done；cgltf 仍首 primitive |
| G4 | Asset/Cooker | Partial | recipe + 最小 glTF；multi-mesh/纹理仍薄 |
| G5 | Audio | **Evidence** | audio tests + sample_2d 300 帧观测 |
| G6 | Windows/Linux + GoogleTest | Open | Windows 为主；**Linux 未跑** |
| G7 | Legacy smoke 基线 | **N/A** | 产品源码已删，不再跑 |
| G8 | 旧接口零引用 | **Done（产品）** | 源码/target 已删；EASTL/EABase submodule 已移除 |
| G9 | 独立删除提交 | **Done（产品）** | `e2ef3d5e` + 后续扫尾提交 |

## 3D（G3）

| 切片 | 状态 |
| --- | --- |
| M9–M11-E9 fixture → glTF/Prefab product sample | **Done** |

## 剩余缺口（非整库“完成”的理由）

1. **G6 Linux** 全门禁尚未在本跟踪表关闭。  
2. **G1 全 feature**（physics + freetype + miniaudio device）若要更严 `productGate`，需 product-2d preset 复验。  
3. cgltf 仍薄；instantiate 仍 fixture meshKey。  
4. 文档/本地 skill 叙述扫尾（进行中）。

**不是**：G7/G8/G9「还没做」——产品删除与 submodule 移除**已完成**。

## 本机快速复验（禁止 clean）

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_audio_tests.exe
```

## FreeType 字体（可选）

引擎**不**强制内置 CJK 字体。解析顺序见 `cmake/TinaUiFont.cmake`：

1. CMake `-DTINA_UI_FONT_PATH=...`  
2. 环境变量 `TINA_UI_FONT_PATH`  
3. 若仓库仍有 `resources/fonts/SourceHanSansSC-Regular.otf` 则用作 fixture  

无路径时 FreeType 相关测试 **GTEST_SKIP**，Desktop/sample 走无字体字节路径。
