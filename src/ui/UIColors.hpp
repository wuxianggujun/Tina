//
// UI 颜色常量库（基于 Core::Color）
// - 统一管理 UI 默认色，逐步替代魔法数字

#pragma once

#include "../core/Color.hpp"

namespace Tina::UI::UIColors {

using Tina::Core::Color;

// === 面板颜色 ===
inline const Color PanelBg         = Color::rgba(0.20f, 0.20f, 0.20f, 0.80f);
inline const Color PanelDarkBg     = Color::rgba(0.15f, 0.15f, 0.18f, 0.95f);  // 角色面板深色背景
inline const Color ToolbarBg       = Color::rgba(0.10f, 0.10f, 0.12f, 0.85f);  // 工具栏背景
inline const Color LabelText       = Color::rgba(1.00f, 1.00f, 1.00f, 1.00f);

// === 按钮颜色 ===
inline const Color ButtonNormal    = Color::rgba(0.30f, 0.30f, 0.35f, 0.90f);
inline const Color ButtonHover     = Color::rgba(0.40f, 0.40f, 0.50f, 0.90f);
inline const Color ButtonPressed   = Color::rgba(0.20f, 0.20f, 0.25f, 0.90f);
inline const Color ButtonText      = Color::rgba(1.00f, 1.00f, 1.00f, 1.00f);
inline const Color ButtonDisabled  = Color::rgba(0.30f, 0.30f, 0.30f, 0.50f);  // 禁用按钮

// === 角色面板专用颜色 ===
inline const Color TitleGold       = Color::rgba(1.00f, 1.00f, 0.50f, 1.00f);  // 金色标题
inline const Color TextWhite       = Color::rgba(1.00f, 1.00f, 1.00f, 1.00f);  // 白色文本
inline const Color TextGray        = Color::rgba(0.70f, 0.70f, 0.70f, 1.00f);  // 灰色文本
inline const Color ControlledGreen = Color::rgba(0.20f, 1.00f, 0.30f, 1.00f);  // 正在控制（绿色）

// === 按钮主题色 ===
inline const Color ButtonBlue      = Color::rgba(0.20f, 0.40f, 0.70f, 1.00f);  // 蓝色按钮
inline const Color ButtonBlueHover = Color::rgba(0.30f, 0.50f, 0.85f, 1.00f);  // 蓝色按钮悬停

// === 角标颜色 ===
inline const Color BadgeBg         = Color::rgba(0.18f, 0.18f, 0.20f, 0.95f);
inline const Color BadgeText       = Color::rgba(0.95f, 0.90f, 0.50f, 1.00f);
inline const Color BadgeHighlight  = Color::rgba(0.95f, 0.90f, 0.50f, 0.80f);

// === 选中高亮 ===
inline const Color SelectionHL     = Color::rgba(0.95f, 0.85f, 0.35f, 1.00f);

// === 进度条颜色 ===
inline const Color ProgressBgDark  = Color::rgba(0.30f, 0.10f, 0.10f, 0.80f);  // 深红色背景
inline const Color ProgressGreen   = Color::rgba(0.20f, 0.80f, 0.20f, 1.00f);  // 绿色填充（健康）
inline const Color ProgressYellow  = Color::rgba(0.90f, 0.70f, 0.20f, 1.00f);  // 黄色填充（受伤）
inline const Color ProgressRed     = Color::rgba(0.90f, 0.20f, 0.20f, 1.00f);  // 红色填充（危险）

// === 其它通用色 ===
inline const Color Border          = Color::rgba(0.00f, 0.00f, 0.00f, 1.00f);  // 黑色边框

} // namespace Tina::UI::UIColors
