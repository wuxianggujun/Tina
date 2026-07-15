//
// UIPerformanceMonitor - UI性能监控系统
// 职责：收集、分析和报告UI渲染性能数据
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../core/Container.hpp"

namespace Tina::UI {

// 性能统计数据
struct PerformanceStats {
    uint32_t drawCalls = 0;        // 绘制调用次数
    uint32_t vertices = 0;          // 顶点数
    uint32_t triangles = 0;         // 三角形数
    uint32_t rectCount = 0;         // 矩形数量
    uint32_t imageCount = 0;        // 图片数量
    uint32_t textCount = 0;         // 文本数量
    float batchEfficiency = 0.0f;  // 批处理效率 (0-1)
    float frameTimeMs = 0.0f;      // 帧时间（毫秒）
    
    void reset() {
        drawCalls = vertices = triangles = 0;
        rectCount = imageCount = textCount = 0;
        batchEfficiency = frameTimeMs = 0.0f;
    }
    
    void calculate() {
        uint32_t totalElements = rectCount + imageCount + textCount;
        if (totalElements > 0 && drawCalls > 0) {
            batchEfficiency = 1.0f - static_cast<float>(drawCalls) / static_cast<float>(totalElements);
            batchEfficiency = std::max(0.0f, std::min(1.0f, batchEfficiency));
        }
    }
};

// 性能警告级别
enum class PerformanceWarningLevel {
    None,       // 无警告
    Info,       // 信息
    Warning,    // 警告
    Critical    // 严重
};

// 性能警告
struct PerformanceWarning {
    PerformanceWarningLevel level;
    std::string message;
    std::string suggestion;
};

// 性能监控器
class UIPerformanceMonitor {
public:
    UIPerformanceMonitor() = default;
    ~UIPerformanceMonitor() = default;

    // 启用/禁用监控
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // 帧开始/结束
    void beginFrame();
    void endFrame();

    // 记录渲染事件
    void recordDrawCall() { if (m_enabled) m_currentStats.drawCalls++; }
    void recordVertices(uint32_t count) { if (m_enabled) m_currentStats.vertices += count; }
    void recordTriangles(uint32_t count) { if (m_enabled) m_currentStats.triangles += count; }
    void recordRect() { if (m_enabled) m_currentStats.rectCount++; }
    void recordImage() { if (m_enabled) m_currentStats.imageCount++; }
    void recordText() { if (m_enabled) m_currentStats.textCount++; }

    // 获取统计数据
    const PerformanceStats& getCurrentStats() const { return m_currentStats; }
    const PerformanceStats& getLastFrameStats() const { return m_lastFrameStats; }
    const PerformanceStats& getAverageStats() const { return m_averageStats; }

    // 性能分析
    Container::Vector<PerformanceWarning> analyzePerformance() const;
    bool hasPerformanceIssues() const;

    // 阈值设置
    void setDrawCallThreshold(uint32_t threshold) { m_drawCallThreshold = threshold; }
    void setVertexThreshold(uint32_t threshold) { m_vertexThreshold = threshold; }
    void setFrameTimeThreshold(float ms) { m_frameTimeThreshold = ms; }

    // 历史数据
    void setHistorySize(size_t size);
    const Container::Vector<PerformanceStats>& getHistory() const { return m_history; }

    // 日志输出
    void logCurrentStats() const;
    void logAverageStats() const;
    void logWarnings() const;

    // 重置统计
    void reset();

private:
    bool m_enabled = false;
    
    // 当前帧统计
    PerformanceStats m_currentStats;
    PerformanceStats m_lastFrameStats;
    PerformanceStats m_averageStats;
    
    // 历史数据
    Container::Vector<PerformanceStats> m_history;
    size_t m_maxHistorySize = 60;  // 默认保存60帧
    
    // 性能阈值
    uint32_t m_drawCallThreshold = 100;
    uint32_t m_vertexThreshold = 100000;
    float m_frameTimeThreshold = 16.67f;  // 60 FPS
    float m_batchEfficiencyThreshold = 0.5f;
    
    // 帧时间测量
    uint64_t m_frameStartTime = 0;
    
    // 内部方法
    void updateAverageStats();
    void addToHistory(const PerformanceStats& stats);
};

} // namespace Tina::UI