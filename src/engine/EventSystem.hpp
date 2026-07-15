//
// EventSystem.hpp - 统一事件系统（包含优先级、延迟、统计）
// 职责：整合队列、分发器、优先级、延迟事件
// 使用：Application 持有一个 EventSystem 实例
//

#pragma once

#include "EventCore.hpp"
#include "EventQueue.hpp"
#include "EventDispatcher.hpp"
#include "SubscriptionToken.hpp"  // 添加订阅令牌支持
#include "../core/Log.hpp"
#include "../core/Container.hpp"  // 使用封装的容器
#include <EASTL/priority_queue.h>
#include <type_traits>

// 前向声明 UI 类型
namespace Tina::UI { class UINode; }

namespace Tina::Engine {

using namespace Tina::Container;  // 使用容器命名空间

// ==================== UI 事件支持 ====================

// UI事件上下文（状态跟踪）。一个窗口只有一个上下文，但可观察场景的多个顶层根节点。
struct UIEventContext {
    Vector<UI::UINode*> roots;                // 非拥有；由当前 Scene 保证帧内生命周期
    Memory::WeakPtr<UI::UINode> rootOwner;    // SharedPtr 兼容入口的存活观察
    bool rootOwnerRequired = false;
    uint64_t treeVersion = 0;
    UI::UINode* hoveredNode = nullptr;        // 子节点：裸指针（由root保证生命周期）
    UI::UINode* pressedNode = nullptr;        // 子节点：裸指针
    UI::UINode* focusedNode = nullptr;        // 子节点：裸指针
    float mouseX = 0, mouseY = 0;
    bool mouseDown = false;
    bool mouseDownPrev = false;
};

// UI事件传播阶段
enum class UIEventPhase {
    Capture,    // 捕获阶段（从根到目标）
    Target,     // 目标阶段
    Bubble      // 冒泡阶段（从目标到根）
};

// UI事件基类（带传播控制）
template<typename Derived, EventTypeId TypeId>
struct UIEvent : Event<Derived, TypeId> {
    UI::UINode* target = nullptr;           // 事件目标
    UI::UINode* currentTarget = nullptr;    // 当前处理节点
    UIEventPhase phase = UIEventPhase::Target;
    bool propagationStopped = false;
    bool immediatePropagationStopped = false;
    
    void stopPropagation() { propagationStopped = true; }
    void stopImmediatePropagation() { 
        propagationStopped = true;
        immediatePropagationStopped = true;
    }
};

// ==================== 延迟事件包装器 ====================

// 延迟事件（用于延迟触发）
struct DelayedEvent {
    uint64_t triggerTime = 0;  // 触发时间（毫秒时间戳）
    EventWrapper event;

    // 最小堆比较器（触发时间早的优先）
    bool operator>(const DelayedEvent& other) const {
        return triggerTime > other.triggerTime;
    }
};

// ==================== 统一事件系统 ====================

class EventSystem {
public:
    EventSystem() = default;
    ~EventSystem() { shutdown(); }

    // 禁止拷贝
    EventSystem(const EventSystem&) = delete;
    EventSystem& operator=(const EventSystem&) = delete;

    // ==================== 生命周期 ====================

    // 初始化
    bool initialize() {
        if (m_initialized) return true;
        TINA_INFO("EventSystem 初始化...");
        m_dispatcher.resetStats();
        for (auto& queue : m_priorityQueues) {
            queue.clear();
        }
        clearDelayed();
        m_initialized = true;
        TINA_INFO("EventSystem 初始化完成");
        return true;
    }

    // 关闭
    void shutdown() {
        if (!m_initialized) return;
        TINA_INFO("EventSystem 关闭...");
        m_dispatcher.clearAll();
        for (auto& queue : m_priorityQueues) {
            queue.clear();
        }
        clearDelayed();
        m_uiContext.roots.clear();
        m_uiContext.rootOwner.reset();
        m_uiContext.rootOwnerRequired = false;
        m_uiContext.treeVersion = 0;
        m_uiContext.hoveredNode = nullptr;
        m_uiContext.pressedNode = nullptr;
        m_uiContext.focusedNode = nullptr;
        m_uiContext.mouseDown = false;
        m_uiContext.mouseDownPrev = false;
        m_initialized = false;
        TINA_INFO("EventSystem 关闭完成");
    }

    // ==================== 订阅事件 ====================

    // 订阅事件（lambda 或函数对象），返回 RAII 令牌
    template<typename E>
    SubscriptionToken subscribe(EventHandler<E> handler) {
        auto id = m_dispatcher.subscribe<E>(Container::Move(handler));

        // 返回优化的RAII令牌，直接存储必要信息
        return SubscriptionToken(&m_dispatcher, E::TYPE_ID, id, m_dispatcher.lifetimeToken());
    }

    // 订阅事件（成员函数），返回 RAII 令牌
    template<typename E, typename T>
    SubscriptionToken subscribe(T* obj, void (T::*method)(const E&)) {
        auto id = m_dispatcher.subscribe<E>(obj, method);

        // 返回优化的RAII令牌，直接存储必要信息
        return SubscriptionToken(&m_dispatcher, E::TYPE_ID, id, m_dispatcher.lifetimeToken());
    }

    // 订阅事件（const 成员函数），返回 RAII 令牌
    template<typename E, typename T>
    SubscriptionToken subscribe(const T* obj, void (T::*method)(const E&) const) {
        auto id = m_dispatcher.subscribe<E>(obj, method);

        // 返回优化的RAII令牌，直接存储必要信息
        return SubscriptionToken(&m_dispatcher, E::TYPE_ID, id, m_dispatcher.lifetimeToken());
    }

    // ==================== 发送事件 ====================

    // 立即触发事件（同步，直接分发）
    template<typename E>
    void trigger(const E& event) {
        #ifdef DEBUG
        TINA_TRACE("触发事件: {}", eventTypeIdToString(E::TYPE_ID));
        #endif
        m_dispatcher.dispatch(event);
    }

    // 入队事件（异步，下一帧处理）
    template<typename E>
    bool enqueue(const E& event) {
        static_assert(std::is_trivially_copyable_v<E>, "Event enqueued must be trivially copyable");
        static_assert(std::is_trivially_destructible_v<E>, "Event enqueued must be trivially destructible");
        // 根据优先级选择队列
        auto priority = static_cast<uint8_t>(event.priority);
        if (priority >= m_priorityQueues.size()) {
            priority = static_cast<uint8_t>(EventPriority::Medium);
        }
        bool success = m_priorityQueues[priority].push(event);
        
        // 检查队列使用率，发出告警
        if (!success) {
            ++m_totalOverflowCount;
            checkQueueHealth();
        }
        
        return success;
    }

    // 入队事件（指定优先级）
    template<typename E>
    bool enqueue(const E& event, EventPriority priority) {
        static_assert(std::is_trivially_copyable_v<E>, "Event enqueued must be trivially copyable");
        static_assert(std::is_trivially_destructible_v<E>, "Event enqueued must be trivially destructible");
        auto priorityIndex = static_cast<uint8_t>(priority);
        if (priorityIndex >= m_priorityQueues.size()) {
            priorityIndex = static_cast<uint8_t>(EventPriority::Medium);
        }
        return m_priorityQueues[priorityIndex].push(event);
    }

    // 延迟触发事件（指定延迟毫秒数）
    template<typename E>
    void scheduleDelayed(const E& event, uint32_t delayMs) {
        static_assert(std::is_trivially_copyable_v<E>, "Event scheduled must be trivially copyable");
        static_assert(std::is_trivially_destructible_v<E>, "Event scheduled must be trivially destructible");
        uint64_t triggerTime = getCurrentTimeMs() + delayMs;
        DelayedEvent delayed;
        delayed.triggerTime = triggerTime;
        delayed.event = EventWrapper(event);
        m_delayedEvents.push(delayed);
    }

    // ==================== 更新（每帧调用） ====================

    // 处理所有事件（建议每帧调用一次）
    void update() {
        uint64_t currentTime = getCurrentTimeMs();

        // 1. 处理延迟事件
        updateDelayedEvents(currentTime);

        // 2. 处理优先级队列（按优先级顺序，带配额限制）
        updatePriorityQueues();
    }

    // ==================== 管理操作 ====================

    // 清除所有订阅
    void clearAllSubscriptions() {
        m_dispatcher.clearAll();
    }

    // 清除指定类型的订阅
    template<typename E>
    void clearSubscriptions() {
        m_dispatcher.clear<E>();
    }

    // 清除所有队列
    void clearAllQueues() {
        for (auto& queue : m_priorityQueues) {
            queue.clear();
        }
        clearDelayed();
    }

    // 清除延迟事件
    void clearDelayed() {
        while (!m_delayedEvents.empty()) {
            m_delayedEvents.pop();
        }
    }
    
    // 检查队列健康状态（溢出告警）
    void checkQueueHealth() {
        // 每100次溢出打印一次告警
        if (m_totalOverflowCount % 100 == 1) {
            TINA_WARN("EventSystem: 事件队列溢出 {} 次，请考虑增大队列容量或优化事件处理", m_totalOverflowCount);
            
            auto stats = getQueueStats();
            TINA_INFO("  队列状态: High={}, Medium={}, Low={}, Delayed={}",
                     stats.highPrioritySize, stats.mediumPrioritySize, 
                     stats.lowPrioritySize, stats.delayedEventCount);
        }
    }

    // ==================== 统计信息 ====================

    // 获取队列统计
    struct QueueStats {
        size_t highPrioritySize = 0;
        size_t mediumPrioritySize = 0;
        size_t lowPrioritySize = 0;
        size_t delayedEventCount = 0;
        uint64_t totalPushCount = 0;
        uint64_t totalPopCount = 0;
        uint64_t totalOverflowCount = 0;
    };

    QueueStats getQueueStats() const {
        QueueStats stats;
        stats.highPrioritySize = m_priorityQueues[0].size();
        stats.mediumPrioritySize = m_priorityQueues[1].size();
        stats.lowPrioritySize = m_priorityQueues[2].size();
        stats.delayedEventCount = m_delayedEvents.size();

        for (const auto& queue : m_priorityQueues) {
            stats.totalPushCount += queue.getPushCount();
            stats.totalPopCount += queue.getPopCount();
            stats.totalOverflowCount += queue.getOverflowCount();
        }

        return stats;
    }

    // 获取分发器统计
    struct DispatcherStats {
        uint64_t subscribeCount = 0;
        uint64_t dispatchCount = 0;
        size_t totalHandlerCount = 0;
    };

    DispatcherStats getDispatcherStats() const {
        DispatcherStats stats;
        stats.subscribeCount = m_dispatcher.getSubscribeCount();
        stats.dispatchCount = m_dispatcher.getDispatchCount();
        stats.totalHandlerCount = m_dispatcher.getTotalHandlerCount();
        return stats;
    }

    // ==================== UI事件处理（新增） ====================
    
    // 设置UI根节点（接受 SharedPtr）
    void setUIRoot(Memory::SharedPtr<UI::UINode> root);
    
    // 设置UI根节点（兼容裸指针）
    void setUIRoot(UI::UINode* root);

    // 设置当前场景的顶层UI节点；一次输入更新只执行一次命中与路由。
    void setUIRoots(const Vector<UI::UINode*>& roots);
    
    // 更新UI输入（每帧调用）
    void updateUIInput(float mouseX, float mouseY, bool mouseDown);
    // 重载：包含滚轮增量（dy），用于分发 UIMouseWheel 事件
    void updateUIInput(float mouseX, float mouseY, bool mouseDown, float wheelDeltaY);
    
    // 获取UI上下文
    UIEventContext& uiContext() { return m_uiContext; }
    const UIEventContext& uiContext() const { return m_uiContext; }
    
    // 触发UI事件（带捕获/冒泡）
    template<typename E>
    void triggerUIEvent(E& event, UI::UINode* target) {
        static_assert(std::is_base_of_v<UIEvent<E, E::TYPE_ID>, E>, 
                     "UI事件必须继承自 UIEvent");
        
        if (!target) return;
        
        event.target = target;
        
        // 1. 构建事件路径（从根到目标）
        Vector<UI::UINode*> path;
        buildEventPath(target, path);
        
        // 2. 捕获阶段（从根到目标，不包括目标）
        event.phase = UIEventPhase::Capture;
        for (size_t i = 0; i < path.size() - 1 && !event.propagationStopped; ++i) {
            event.currentTarget = path[i];
            m_dispatcher.dispatch(event);  // 分发给订阅者
            if (event.immediatePropagationStopped) break;
        }
        
        // 3. 目标阶段
        if (!event.propagationStopped) {
            event.phase = UIEventPhase::Target;
            event.currentTarget = target;
            m_dispatcher.dispatch(event);
        }
        
        // 4. 冒泡阶段（从目标到根，不包括目标）
        if (!event.propagationStopped) {
            event.phase = UIEventPhase::Bubble;
            for (int i = static_cast<int>(path.size()) - 2; i >= 0 && !event.propagationStopped; --i) {
                event.currentTarget = path[i];
                m_dispatcher.dispatch(event);
                if (event.immediatePropagationStopped) break;
            }
        }
    }
    
    // 打印完整统计信息
    void printStats() const {
        TINA_INFO("========== EventSystem 统计 ==========");

        // 队列统计
        auto qStats = getQueueStats();
        TINA_INFO("队列状态:");
        TINA_INFO("  高优先级: {} 个事件", qStats.highPrioritySize);
        TINA_INFO("  中优先级: {} 个事件", qStats.mediumPrioritySize);
        TINA_INFO("  低优先级: {} 个事件", qStats.lowPrioritySize);
        TINA_INFO("  延迟事件: {} 个", qStats.delayedEventCount);
        TINA_INFO("  总入队: {}", qStats.totalPushCount);
        TINA_INFO("  总出队: {}", qStats.totalPopCount);
        TINA_INFO("  溢出次数: {}", qStats.totalOverflowCount);

        // 分发器统计
        auto dStats = getDispatcherStats();
        TINA_INFO("分发器状态:");
        TINA_INFO("  订阅次数: {}", dStats.subscribeCount);
        TINA_INFO("  分发次数: {}", dStats.dispatchCount);
        TINA_INFO("  总处理器: {}", dStats.totalHandlerCount);

        m_dispatcher.printStats();
    }

    // ==================== 直接访问（高级用法） ====================

    EventDispatcher& dispatcher() { return m_dispatcher; }
    const EventDispatcher& dispatcher() const { return m_dispatcher; }

private:
    // ==================== 内部更新方法 ====================

    // 更新延迟事件
    void updateDelayedEvents(uint64_t currentTime) {
        while (!m_delayedEvents.empty()) {
            const auto& delayed = m_delayedEvents.top();
            if (delayed.triggerTime > currentTime) {
                break;  // 还未到触发时间
            }

            // 触发事件
            m_dispatcher.dispatch(delayed.event);
            m_delayedEvents.pop();
        }
    }

    // 更新优先级队列（带配额限制）
    void updatePriorityQueues() {
        // 配额：高优先级 80%，中 15%，低 5%
        static constexpr size_t quotas[] = {
            PRIORITY_QUOTA_HIGH,
            PRIORITY_QUOTA_MEDIUM,
            PRIORITY_QUOTA_LOW
        };

        for (size_t priority = 0; priority < 3; ++priority) {
            auto& queue = m_priorityQueues[priority];
            size_t processed = 0;

            while (!queue.empty() && processed < quotas[priority]) {
                EventWrapper event;
                if (queue.pop(event)) {
                    m_dispatcher.dispatch(event);
                    ++processed;
                }
            }
        }
    }

private:
    // ==================== 成员变量 ====================

    // 事件分发器
    EventDispatcher m_dispatcher;

    // 优先级队列（3 个：高/中/低）
    Array<EventQueue<EVENT_QUEUE_CAPACITY>, 3> m_priorityQueues;

    // 延迟事件队列（最小堆）
    PriorityQueue<DelayedEvent, Vector<DelayedEvent>, Greater<DelayedEvent>> m_delayedEvents;
    
    // 统计信息
    uint64_t m_totalOverflowCount = 0;  // 总溢出次数
    
    // UI事件上下文
    UIEventContext m_uiContext;
    bool m_initialized = false;
    
    // UI事件内部方法
    void buildEventPath(UI::UINode* target, Vector<UI::UINode*>& path);
    UI::UINode* findNodeUnderMouse(UI::UINode* node, float x, float y);
    void handleMouseInput(float wheelDeltaY = 0.0f);
};

} // namespace Tina::Engine
