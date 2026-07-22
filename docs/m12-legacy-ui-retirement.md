# M12：Legacy 产品 / Legacy UI 退役状态

> 用户授权：废弃旧横版 2D 小游戏与 Legacy UI，产品以 vNext 为准。  
> **状态：产品源码与构建图删除已完成**；下文为事实清单，不是「Blocked 待删」。

## 已删除（源码与构建图）

| 路径 / 目标 | 说明 |
| --- | --- |
| 旧产品 `src/ui`（Legacy）、`src/game`、`src/engine`、`src/ecs`、`src/renderer`、`src/particles`、`src/physics` | 横版 2D + Legacy UI |
| `src/main.cpp`、Legacy `Tina.exe` 图 | 产品入口已废 |
| CoreLegacy / EASTL 兼容面、Procedural | 已删 |
| Legacy 专属测试 | 已删 |
| vcpkg 默认 feature `legacy` | 默认仅 `tests` |
| EASTL/EABase **submodule** | 已从 `.gitmodules` 与工作树移除 |
| 无引用玩法 `resources` | audio/textures/旧 shaders/config/多余字重等已删 |
| `src/vnext/` 目录前缀 | 已扁平为 `src/<module>` |

## 构建策略

- `TINA_BUILD_LEGACY` **OFF**；显式 **ON → FATAL_ERROR**。  
- 产品：`Tina::Desktop` + `tina_sample_*`。  
- UI：`include/tina/ui` + `src/ui`（Retained 实现）。

## 保留

| 项 | 说明 |
| --- | --- |
| `thirdparty/bgfx.cmake` | 唯一真实渲染后端依赖 |
| `branding/Tina.jpg` | 引擎吉祥物/标识（非玩法资源） |
| `resources/fonts/SourceHanSansSC-Regular.otf` | **可选** FreeType fixture；可用 `TINA_UI_FONT_PATH` 完全外置 |

## 验证（禁止 clean-first）

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

## 仍非“整库扫尾完毕”的项

- Linux 全门禁复验  
- 主题文档 / 本地 agent skill 中过时句子  
- FreeType 字体可完全移出仓库（设 `TINA_UI_FONT_PATH` 后可删 Regular fixture）
