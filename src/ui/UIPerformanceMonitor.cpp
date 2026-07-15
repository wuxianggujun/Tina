//
// UIPerformanceMonitor - 性能监控实现
//

#include "UIPerformanceMonitor.hpp"
#include "../core/Log.hpp"
#include <algorithm>
#include <numeric>
#include <chrono>

namespace Tina::UI {

void UIPerformanceMonitor::beginFrame() {
    if (!m_enabled) return;
    
    m_frameStartTime = std::chrono::steady_clock::now().time_since_epoch().count();
    m_currentStats.reset();
}

void UIPerformanceMonitor::endFrame() {
    if (!m_enabled) return;
    
    // 计算帧时间
    uint64_t frameEndTime = std::chrono::steady_clock::now().time_since_epoch().count();
    m_currentStats.frameTimeMs = (frameEndTime - m_frameStartTime) / 1000000.0f;  // 纳秒转毫秒
    
    // 计算批处理效率
    m_currentStats.calculate();
    
    // 保存到历史
    addToHistory(m_currentStats);
    
    // 更新平均值
    updateAverageStats();
    
    // 保存为上一帧数据
    m_lastFrameStats = m_currentStats;
}

void UIPerformanceMonitor::setHistorySize(size_t size) {
    m_maxHistorySize = size;
    if (m_history.size() > m_maxHistorySize) {
        m_history.erase(m_history.begin(), 
                       m_history.begin() + (m_history.size() - m_maxHistorySize));
    }
}

void UIPerformanceMonitor::reset() {
    m_currentStats.reset();
    m_lastFrameStats.reset();
    m_averageStats.reset();
    m_history.clear();
}

void UIPerformanceMonitor::updateAverageStats() {
    if (m_history.empty()) {
        m_averageStats.reset();
        return;
    }
    
    // 计算平均值
    m_averageStats.reset();
    for (const auto& stats : m_history) {
        m_averageStats.drawCalls += stats.drawCalls;
        m_averageStats.vertices += stats.vertices;
        m_averageStats.triangles += stats.triangles;
        m_averageStats.rectCount += stats.rectCount;
        m_averageStats.imageCount += stats.imageCount;
        m_averageStats.textCount += stats.textCount;
        m_averageStats.batchEfficiency += stats.batchEfficiency;
        m_averageStats.frameTimeMs += stats.frameTimeMs;
    }
    
    size_t count = m_history.size();
    m_averageStats.drawCalls /= count;
    m_averageStats.vertices /= count;
    m_averageStats.triangles /= count;
    m_averageStats.rectCount /= count;
    m_averageStats.imageCount /= count;
    m_averageStats.textCount /= count;
    m_averageStats.batchEfficiency /= count;
    m_averageStats.frameTimeMs /= count;
}

void UIPerformanceMonitor::addToHistory(const PerformanceStats& stats) {
    m_history.push_back(stats);
    
    // 限制历史大小
    if (m_history.size() > m_maxHistorySize) {
        m_history.erase(m_history.begin());
    }
}

Container::Vector<PerformanceWarning> UIPerformanceMonitor::analyzePerformance() const {
    Container::Vector<PerformanceWarning> warnings;
    
    if (!m_enabled || m_history.empty()) {
        return warnings;
    }
    
    const auto& stats = m_lastFrameStats;
    
    // 检查绘制调用数
    if (stats.drawCalls > m_drawCallThreshold) {
        warnings.push_back({
            PerformanceWarningLevel::Warning,
            "Draw calls过多: " + std::to_string(stats.drawCalls),
            "考虑启用批处理或使用更激进的批处理策略"
        });
    }
    
    // 检查顶点数
    if (stats.vertices > m_vertexThreshold) {
        warnings.push_back({
            PerformanceWarningLevel::Warning,
            "顶点数过多: " + std::to_string(stats.vertices),
            "考虑减少UI元素数量或使用LOD技术"
        });
    }
    
    // 检查帧时间
    if (stats.frameTimeMs > m_frameTimeThreshold) {
        warnings.push_back({
            PerformanceWarningLevel::Critical,
            "帧时间过长: " + std::to_string(stats.frameTimeMs) + "ms",
            "UI渲染性能不足，考虑优化渲染逻辑或减少UI复杂度"
        });
    }
    
    // 检查批处理效率
    if (stats.batchEfficiency < m_batchEfficiencyThreshold) {
        warnings.push_back({
            PerformanceWarningLevel::Info,
            "批处理效率较低: " + std::to_string(static_cast<int>(stats.batchEfficiency * 100)) + "%",
            "考虑调整批处理策略或重新组织UI元素绘制顺序"
        });
    }
    
    return warnings;
}

bool UIPerformanceMonitor::hasPerformanceIssues() const {
    if (!m_enabled || m_history.empty()) {
        return false;
    }
    
    const auto& stats = m_lastFrameStats;
    return stats.drawCalls > m_drawCallThreshold ||
           stats.vertices > m_vertexThreshold ||
           stats.frameTimeMs > m_frameTimeThreshold ||
           stats.batchEfficiency < m_batchEfficiencyThreshold;
}

void UIPerformanceMonitor::logCurrentStats() const {
    if (!m_enabled) {
        TINA_WARN("UIPerformanceMonitor: 性能监控未启用");
        return;
    }
    
    const auto& stats = m_lastFrameStats;
    TINA_INFO("=== UI渲染性能统计（当前帧）===");
    TINA_INFO("Draw Calls: {}", stats.drawCalls);
    TINA_INFO("顶点数: {}", stats.vertices);
    TINA_INFO("三角形数: {}", stats.triangles);
    TINA_INFO("矩形数: {}", stats.rectCount);
    TINA_INFO("图片数: {}", stats.imageCount);
    TINA_INFO("文本数: {}", stats.textCount);
    TINA_INFO("批处理效率: {:.1f}%", stats.batchEfficiency * 100.0f);
    TINA_INFO("帧时间: {:.2f}ms ({:.1f} FPS)", stats.frameTimeMs, 1000.0f / stats.frameTimeMs);
    TINA_INFO("================================");
}

void UIPerformanceMonitor::logAverageStats() const {
    if (!m_enabled) {
        TINA_WARN("UIPerformanceMonitor: 性能监控未启用");
        return;
    }
    
    if (m_history.empty()) {
        TINA_WARN("UIPerformanceMonitor: 没有历史数据");
        return;
    }
    
    const auto& stats = m_averageStats;
    TINA_INFO("=== UI渲染性能统计（平均值，{}帧）===", m_history.size());
    TINA_INFO("Draw Calls: {}", stats.drawCalls);
    TINA_INFO("顶点数: {}", stats.vertices);
    TINA_INFO("三角形数: {}", stats.triangles);
    TINA_INFO("矩形数: {}", stats.rectCount);
    TINA_INFO("图片数: {}", stats.imageCount);
    TINA_INFO("文本数: {}", stats.textCount);
    TINA_INFO("批处理效率: {:.1f}%", stats.batchEfficiency * 100.0f);
    TINA_INFO("帧时间: {:.2f}ms ({:.1f} FPS)", stats.frameTimeMs, 1000.0f / stats.frameTimeMs);
    TINA_INFO("====================================");
}

void UIPerformanceMonitor::logWarnings() const {
    auto warnings = analyzePerformance();
    
    if (warnings.empty()) {
        TINA_INFO("UIPerformanceMonitor: 无性能问题");
        return;
    }
    
    TINA_WARN("=== UI性能警告 ===");
    for (const auto& warning : warnings) {
        switch (warning.level) {
            case PerformanceWarningLevel::Info:
                TINA_INFO("[信息] {}", warning.message);
                TINA_INFO("  建议: {}", warning.suggestion);
                break;
            case PerformanceWarningLevel::Warning:
                TINA_WARN("[警告] {}", warning.message);
                TINA_WARN("  建议: {}", warning.suggestion);
                break;
            case PerformanceWarningLevel::Critical:
                TINA_ERROR("[严重] {}", warning.message);
                TINA_ERROR("  建议: {}", warning.suggestion);
                break;
            default:
                break;
        }
    }
    TINA_WARN("==================");
}

} // namespace Tina::UI