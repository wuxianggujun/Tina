# ADR 0005：GLFW + 原生窄适配，不引入 SDL/SDL3

- 状态：Accepted
- 日期：2026-07-16

## 背景

Tina 已使用 GLFW 完成 Windows/Linux 窗口、键鼠和标准 Gamepad。为 IME、原生窗口句柄或
平台诊断再引入 SDL/SDL3，会形成第二套窗口、输入、事件和生命周期语义。

## 决定

`tina_platform` 只定义平台无关契约和 Headless 实现；`tina_platform_glfw` 是唯一真实窗口与
基础输入 backend。GLFW 未覆盖的能力通过最窄平台适配补齐，例如 Windows IME 使用 IMM32。
不引入 SDL、SDL3 或 SDL_mixer，公共接口不暴露 GLFW/Win32 类型。

## 结果

- 窗口、输入和事件只有一套所有权与时间线；
- Linux IME 等缺口需要单独实现并通过能力查询表达；
- Headless Runtime 不链接 GLFW；
- 平台扩展不得演变为通用原生句柄 Service Locator。

## 被拒绝方案

- GLFW 与 SDL3 并存：职责重复且事件/手柄映射容易分叉；
- 用 SDL3 完整替换当前 GLFW 路径：没有解决已量化缺口，不值得迁移成本。
