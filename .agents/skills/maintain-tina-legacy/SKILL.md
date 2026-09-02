---
name: maintain-tina-legacy
description: Legacy 产品图已退役。此 skill 仅说明迁移后状态并重定向到 vNext 产品路径；勿再修改已删除的 src/game、src/engine 或 Tina.exe。
---

# Legacy 产品已退役

## 状态

横版 2D 小游戏、`Tina.exe`、Legacy UI **产品图**（旧 Application/EnTT 产品实现）、
`src/game`/`engine`/`ecs`/`renderer` 等**已删除**。
`TINA_BUILD_LEGACY=ON` 会 **FATAL**。

> **`src/ui` 与 `include/tina/ui` 不在删除范围内。** 它们是当前唯一在用的产品 Retained UI 模块。
> 「Legacy UI 已删除」仅指旧产品图里的 UI 实现，**不构成删除当前 `src/ui` 的依据**
> （见 `README.md:6`、`README_CN.md:6` 的同一条警告）。下表把 UI 改动指向 `src/ui` 正是因为它仍然存活。

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
