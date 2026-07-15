//
// EventProfiler.hpp - 事件系统性能分析器
// 监控事件处理性能，帮助优化
//

#pragma once

#include "EventCore.hpp"
#include "../core/Time.hpp"
#include "../core/Log.hpp"
#include "../core/Container.hpp"  // 使用封装的容器
#include <chrono>

namespace Tina::Engine {

using namespace Tina::Container;  // 使用容器命名空间

// ==================== 事件性能数据 ====================

struct EventPerfData {
    uint32_t count = 0;              // 触发次数
    double totalTime = 0.0;           // 总耗时（毫秒）
    double minTime = 999999.0;        // 最小耗时
    double maxTime = 0.0;             // 最大耗时
    double avgTime = 0.0;             // 平均耗时

    void record(double timeMs) {
        count++;
        totalTime += timeMs;
        minTime = Min(minTime, timeMs);
        maxTime = Max(maxTime, timeMs);
        avgTime = totalTime / count;
    }

    void reset() {
        count = 0;
        totalTime = 0.0;
        minTime = 999999.0;
        maxTime = 0.0;
        avgTime = 0.0;
    }
};

// ==================== 事件性能分析器 ====================

class EventProfiler {
public:
    static EventProfiler& getInstance() {
        static EventProfiler instance;
        return instance;
    }

    // 开始计时
    void beginEvent(EventTypeId typeId) {
        m_startTimes[typeId] = std::chrono::high_resolution_clock::now();
    }

    // 结束计时并记录
    void endEvent(EventTypeId typeId) {
        auto endTime = std::chrono::high_resolution_clock::now();

        auto it = m_startTimes.find(typeId);
        if (it != m_startTimes.end()) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - it->second).count() / 1000.0;  // 转换为毫秒

            m_perfData[typeId].record(duration);
            m_startTimes.erase(it);
        }
    }

    // 记录队列延迟
    void recordQueueLatency(EventTypeId typeId, double latencyMs) {
        m_queueLatency[typeId].record(latencyMs);
    }

    // 打印性能报告
    void printReport() const {
        TINA_INFO("========== 事件性能报告 ==========");

        // 事件处理性能
        TINA_INFO("事件处理性能:");
        for (const auto& [typeId, data] : m_perfData) {
            if (data.count > 0) {
                TINA_INFO("  {} - 次数: {}, 平均: {:.3f}ms, 最小: {:.3f}ms, 最大: {:.3f}ms",
                    eventTypeIdToString(typeId),
                    data.count,
                    data.avgTime,
                    data.minTime,
                    data.maxTime);
            }
        }

        // 队列延迟
        TINA_INFO("队列延迟:");
        for (const auto& [typeId, data] : m_queueLatency) {
            if (data.count > 0) {
                TINA_INFO("  {} - 平均延迟: {:.3f}ms",
                    eventTypeIdToString(typeId),
                    data.avgTime);
            }
        }

        // 热点事件（触发最频繁的前5个）
        printHotEvents();
    }

    // 重置所有统计
    void reset() {
        m_perfData.clear();
        m_queueLatency.clear();
        m_startTimes.clear();
    }

    // 获取特定事件的性能数据
    const EventPerfData* getPerfData(EventTypeId typeId) const {
        auto it = m_perfData.find(typeId);
        return it != m_perfData.end() ? &it->second : nullptr;
    }

private:
    EventProfiler() = default;

    void printHotEvents() const {
        // 收集并排序
        Vector<Pair<EventTypeId, uint32_t>> events;
        for (const auto& [typeId, data] : m_perfData) {
            events.push_back({typeId, data.count});
        }

        // 按触发次数排序
        Sort(events.begin(), events.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        // 打印前5个
        TINA_INFO("热点事件（TOP 5）:");
        for (size_t i = 0; i < Min<size_t>(5, events.size()); ++i) {
            TINA_INFO("  {}: {} - {} 次",
                i + 1,
                eventTypeIdToString(events[i].first),
                events[i].second);
        }
    }

    using TimePoint = std::chrono::high_resolution_clock::time_point;

    HashMap<EventTypeId, EventPerfData> m_perfData;
    HashMap<EventTypeId, EventPerfData> m_queueLatency;
    HashMap<EventTypeId, TimePoint> m_startTimes;
};

// ==================== RAII 性能计时器 ====================

class EventPerfTimer {
public:
    explicit EventPerfTimer(EventTypeId typeId) : m_typeId(typeId) {
        EventProfiler::getInstance().beginEvent(m_typeId);
    }

    ~EventPerfTimer() {
        EventProfiler::getInstance().endEvent(m_typeId);
    }

private:
    EventTypeId m_typeId;
};

// ==================== 性能监控宏 ====================

#ifdef ENABLE_EVENT_PROFILING
    #define EVENT_PERF_BEGIN(typeId) EventProfiler::getInstance().beginEvent(typeId)
    #define EVENT_PERF_END(typeId) EventProfiler::getInstance().endEvent(typeId)
    #define EVENT_PERF_TIMER(typeId) EventPerfTimer _perfTimer(typeId)
    #define EVENT_PERF_REPORT() EventProfiler::getInstance().printReport()
    #define EVENT_PERF_RESET() EventProfiler::getInstance().reset()
#else
    #define EVENT_PERF_BEGIN(typeId) ((void)0)
    #define EVENT_PERF_END(typeId) ((void)0)
    #define EVENT_PERF_TIMER(typeId) ((void)0)
    #define EVENT_PERF_REPORT() ((void)0)
    #define EVENT_PERF_RESET() ((void)0)
#endif

} // namespace Tina::Engine