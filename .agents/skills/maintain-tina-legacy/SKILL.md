---
name: maintain-tina-legacy
description: Legacy 产品图已退役。此 skill 仅说明迁移后状态并重定向到 vNext 产品路径；勿再修改已删除的 src/game、src/engine 或 Tina.exe。
---

# Legacy 产品已退役

## 状态

横版 2D 小游戏、`Tina.exe`、旧 `src/ui`（Legacy UI）、`src/game`/`engine`/`ecs`/`renderer` 等**已删除**。  
`TINA_BUILD_LEGACY=ON` 会 **FATAL**。

## 不要做

- 不要恢复 Legacy 源码树或 smoke 命令（`--smoke-*`）。  
- 不要在 `include/tina` 引入旧 Application/EnTT 公开边界。  
- 不要把本 skill 当日常维护入口。

## 请改用

| 需求 | Skill / 路径 |
| --- | --- |
| 架构 / 删除扫尾 | `$navigate-tina-architecture`、`docs/m12-*.md` |
| 构建测试 | `$build-and-test-tina` |
| Runtime / Desktop | `$develop-tina-vnext-runtime` |
| UI | `$develop-tina-ui`（`src/ui` + `include/tina/ui`） |
| 产品样例 | `tina_sample_2d`、`tina_sample_3d` |

历史 ADR 可保留旧名称；当前主题文档与代码优先。
