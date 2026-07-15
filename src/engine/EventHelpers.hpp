//
// EventHelpers.hpp - 事件系统便捷工具
// 提供宏、辅助函数和批量订阅支持
//

#pragma once

#include "EventSystem.hpp"
#include "SubscriptionToken.hpp"
#include "../core/Log.hpp"
#include "../core/Container.hpp"  // 使用封装的容器

namespace Tina::Engine {

using namespace Tina::Container;  // 使用容器命名空间

// ==================== 便捷宏 ====================

// 订阅事件的便捷宏
#define TINA_SUBSCRIBE_EVENT(manager, eventType, handler) \
    manager.add(app()->events().subscribe<eventType>(this, &handler))

// 批量订阅事件
#define TINA_SUBSCRIBE_EVENTS(...) \
    do { \
        __VA_ARGS__ \
    } while(0)

// 调试模式下的事件订阅（带日志）
#ifdef DEBUG
    #define TINA_DEBUG_SUBSCRIBE(manager, eventType, handler) \
        do { \
            TINA_INFO("订阅事件: {} -> {}", #eventType, #handler); \
            manager.add(app()->events().subscribe<eventType>(this, &handler)); \
        } while(0)
#else
    #define TINA_DEBUG_SUBSCRIBE(manager, eventType, handler) \
        TINA_SUBSCRIBE_EVENT(manager, eventType, handler)
#endif

// ==================== 批量订阅辅助类 ====================

template<typename T>
class EventSubscriber {
public:
    explicit EventSubscriber(T* owner, EventSystem* eventSystem)
        : m_owner(owner), m_eventSystem(eventSystem) {}

    // 链式订阅接口
    template<typename E>
    EventSubscriber& on(void (T::*method)(const E&)) {
        if (m_eventSystem) {
            m_subscriptions.add(m_eventSystem->subscribe<E>(m_owner, method));
        }
        return *this;
    }

    // 链式订阅接口（const 方法）
    template<typename E>
    EventSubscriber& on(void (T::*method)(const E&) const) {
        if (m_eventSystem) {
            m_subscriptions.add(m_eventSystem->subscribe<E>(m_owner, method));
        }
        return *this;
    }

    // 获取订阅数量
    size_t count() const {
        return m_subscriptions.count();
    }

    // 取消所有订阅
    void unsubscribeAll() {
        m_subscriptions.unsubscribeAll();
    }

private:
    T* m_owner;
    EventSystem* m_eventSystem;
    SubscriptionManager m_subscriptions;
};

// ==================== 事件批处理器 ====================

class EventBatch {
public:
    EventBatch() = default;

    // 添加事件到批次
    template<typename E>
    EventBatch& add(const E& event) {
        m_events.push_back(EventWrapper(event));
        return *this;
    }

    // 获取事件数量
    size_t size() const {
        return m_events.size();
    }

    // 清空批次
    void clear() {
        m_events.clear();
    }

    // 获取事件列表（供 EventSystem 使用）
    const Container::Vector<EventWrapper>& events() const {
        return m_events;
    }

private:
    Container::Vector<EventWrapper> m_events;
};

// ==================== 事件跟踪器（调试用） ====================

#ifdef DEBUG
class EventTracker {
public:
    static EventTracker& getInstance() {
        static EventTracker instance;
        return instance;
    }

    // 记录事件触发
    void recordEvent(EventTypeId typeId) {
        m_eventCounts[static_cast<size_t>(typeId)]++;
        m_totalEvents++;
    }

    // 记录订阅
    void recordSubscription(EventTypeId typeId) {
        m_subscriptionCounts[static_cast<size_t>(typeId)]++;
    }

    // 打印统计信息
    void printStats() const {
        TINA_INFO("===== 事件系统统计 =====");
        TINA_INFO("总事件数: {}", m_totalEvents);

        for (size_t i = 0; i < m_eventCounts.size(); ++i) {
            if (m_eventCounts[i] > 0) {
                auto typeId = static_cast<EventTypeId>(i);
                TINA_INFO("  {} - 触发: {} 次, 订阅: {} 个",
                    eventTypeIdToString(typeId),
                    m_eventCounts[i],
                    m_subscriptionCounts[i]);
            }
        }
    }

    // 重置统计
    void reset() {
        m_eventCounts.fill(0);
        m_subscriptionCounts.fill(0);
        m_totalEvents = 0;
    }

private:
    EventTracker() {
        m_eventCounts.fill(0);
        m_subscriptionCounts.fill(0);
    }

    Array<uint32_t, static_cast<size_t>(EventTypeId::MaxEventTypes)> m_eventCounts;
    Array<uint32_t, static_cast<size_t>(EventTypeId::MaxEventTypes)> m_subscriptionCounts;
    uint32_t m_totalEvents = 0;
};

// 调试宏
#define TRACK_EVENT(typeId) EventTracker::getInstance().recordEvent(typeId)
#define TRACK_SUBSCRIPTION(typeId) EventTracker::getInstance().recordSubscription(typeId)
#define PRINT_EVENT_STATS() EventTracker::getInstance().printStats()
#define RESET_EVENT_STATS() EventTracker::getInstance().reset()

#else
// Release 模式下的空宏
#define TRACK_EVENT(typeId) ((void)0)
#define TRACK_SUBSCRIPTION(typeId) ((void)0)
#define PRINT_EVENT_STATS() ((void)0)
#define RESET_EVENT_STATS() ((void)0)
#endif

} // namespace Tina::Engine