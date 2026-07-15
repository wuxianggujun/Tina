//
// UIConstants - UI渲染器相关常量定义
// 职责：集中管理所有魔术数字，提高代码可维护性
//

#pragma once

#include <cstdint>
#include <bgfx/bgfx.h>

namespace Tina::UI {

// ==================== 视图ID（全局约定） ====================
// 说明：统一管理 bgfx view 的编号，减少魔术数字分散
// bgfx按view ID从小到大渲染，所以背景view必须比UI view的ID小
constexpr uint16_t VIEW_CLEAR        = 0; // 清屏/默认
constexpr uint16_t VIEW_BACKGROUND   = 1; // 菜单/过场背景（MenuScene、WorldSelectScene专用）
constexpr uint16_t VIEW_WORLD_SOLID  = 2; // 世界-不透明（地形/静态）
constexpr uint16_t VIEW_WORLD_ALPHA  = 3; // 世界-半透明/角色/特效
constexpr uint16_t VIEW_UI           = 4; // UI 图层（最上层）

// 可选：强类型枚举（如需更严格的类型控制，可逐步替换）
enum class ViewId : uint16_t {
    Clear       = VIEW_CLEAR,
    WorldSolid  = VIEW_WORLD_SOLID,
    WorldAlpha  = VIEW_WORLD_ALPHA,
    Background  = VIEW_BACKGROUND,
    UI          = VIEW_UI,
};

// ==================== 批处理相关常量 ====================

// 索引限制（uint16_t 最大值）
constexpr uint32_t MAX_VERTICES_PER_BATCH = 65536;

// 几何图元顶点数
constexpr uint32_t VERTICES_PER_RECT = 4;      // 每个矩形4个顶点
constexpr uint32_t INDICES_PER_RECT = 6;        // 每个矩形6个索引（2个三角形）

// 默认预分配容量（避免频繁内存重分配）
constexpr uint32_t DEFAULT_COLOR_VERTEX_RESERVE = 1024;   // 预留256个矩形的顶点
constexpr uint32_t DEFAULT_COLOR_INDEX_RESERVE = 1536;    // 预留256个矩形的索引
constexpr uint32_t DEFAULT_SPRITE_BATCH_RESERVE = 8;      // 预留8个不同纹理批次
constexpr uint32_t DEFAULT_TEXT_CMD_RESERVE = 64;         // 预留64个文本命令

// 批次大小限制
constexpr uint32_t DEFAULT_MAX_BATCH_VERTICES = 65536;    // 默认最大批次顶点数
constexpr uint32_t DEPTH_SORTED_MAX_VERTICES = 32768;     // 深度排序时使用较小批次

// ==================== 文本渲染相关常量 ====================

// 字体大小阈值
constexpr int LINEAR_FILTER_FONT_SIZE_THRESHOLD = 24;     // 小于此字号使用线性过滤
constexpr int DEFAULT_FONT_SIZE = 16;                      // 默认字号

// ==================== 渲染层常量（大的在上） ====================
constexpr int LAYER_DEFAULT = 0;    // 默认层
constexpr int LAYER_OVERLAY = 50;   // 覆盖层（如遮罩/菜单）
constexpr int LAYER_DIALOG  = 100;  // 对话框/模态层

// ==================== 性能监控相关常量 ====================

// 性能统计
constexpr size_t PROFILER_HISTORY_SIZE = 60;              // 性能历史记录帧数（1秒@60fps）
constexpr uint32_t PERFORMANCE_WARNING_DRAWCALLS = 50;     // Draw Calls 警告阈值
constexpr float BATCH_EFFICIENCY_WARNING = 0.7f;           // 批处理效率警告阈值（70%）
constexpr float FRAME_TIME_WARNING_MS = 16.0f;            // 帧时间警告阈值（60fps）
constexpr uint32_t VERTEX_COUNT_WARNING = 100000;         // 顶点数警告阈值

// ==================== 日志级别相关常量 ====================

// 调试输出控制
constexpr bool ENABLE_BATCH_TRACE_LOG = false;            // 是否启用批处理跟踪日志
constexpr bool ENABLE_PERFORMANCE_LOG = true;             // 是否启用性能日志

// ==================== 渲染状态相关常量 ====================

// 渲染状态标志（用于批处理合并判断）
constexpr uint64_t UI_RENDER_STATE_DEFAULT =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

} // namespace Tina::UI
