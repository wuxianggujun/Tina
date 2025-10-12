//
// EventDispatcher.hpp - 优化后的事件分发器
// 职责：订阅事件、分发事件、管理处理器
// 性能：O(1) 订阅，O(H) 分发（H 是处理器数量）
// 优化：移除dynamic_cast，使用静态分发，减少动态内存分配
//

#pragma once

#include "EventCore.hpp"
#include "EventQueue.hpp"
#include "../core/Log.hpp"
#include "../core/Container.hpp"  // 使用封装的容器
#include "../core/Memory.hpp"     // 使用封装的智能指针
#include <atomic>
#include <type_traits>

namespace Tina::Engine {

using namespace Tina::Container;  // 使用容器命名空间
using Tina::Memory::UniquePtr;    // 导入智能指针类型
using Tina::Memory::MakeUnique;

// ==================== 事件处理器类型 ====================

// 事件处理器（使用 fixed_function，64 字节栈内存）
template<typename E>
using EventHandler = FixedFunction<HANDLER_FUNCTION_SIZE, void, const E&>;

// 订阅ID类型
using SubscriptionId = uint64_t;

// ==================== 优化的处理器存储 ====================

// 单个事件类型的处理器容器
template<typename E>
class TypedEventHandlers {
public:
    struct HandlerEntry {
        SubscriptionId id;
        EventHandler<E> handler;
    };

    // 添加处理器
    SubscriptionId add(EventHandler<E> handler) {
        SubscriptionId id = m_nextId++;
        m_handlers.push_back({id, Container::Move(handler)});
        return id;
    }

    // 移除处理器
    bool remove(SubscriptionId id) {
        auto it = Container::FindIf(m_handlers.begin(), m_handlers.end(),
            [id](const HandlerEntry& entry) { return entry.id == id; });

        if (it != m_handlers.end()) {
            m_handlers.erase(it);
            return true;
        }
        return false;
    }

    // 分发事件到所有处理器
    void dispatch(const E& event) {
        for (auto& entry : m_handlers) {
            if (entry.handler) {
                entry.handler(event);
            }
        }
    }

    // 清除所有处理器
    void clear() {
        m_handlers.clear();
    }

    // 获取处理器数量
    size_t size() const {
        return m_handlers.size();
    }

private:
    Vector<HandlerEntry> m_handlers;
    static inline std::atomic<SubscriptionId> m_nextId{1};
};

// ==================== 处理器存储管理器 ====================

// 处理器存储的基类（用于类型擦除）
class HandlerStorageBase {
public:
    virtual ~HandlerStorageBase() = default;
    virtual void dispatchWrapper(const EventWrapper& wrapper) = 0;
    virtual size_t getHandlerCount() const = 0;
    virtual bool removeHandler(SubscriptionId id) = 0;
    virtual void clear() = 0;
};

// 具体类型的处理器存储
template<typename E>
class TypedHandlerStorage : public HandlerStorageBase {
public:
    TypedEventHandlers<E> handlers;

    void dispatchWrapper(const EventWrapper& wrapper) override {
        // 直接转换，不使用dynamic_cast
        const E* event = wrapper.as<E>();
        if (event) {
            handlers.dispatch(*event);
        }
    }

    size_t getHandlerCount() const override {
        return handlers.size();
    }

    bool removeHandler(SubscriptionId id) override {
        return handlers.remove(id);
    }

    void clear() override {
        handlers.clear();
    }
};

// ==================== 优化的事件分发器 ====================

class EventDispatcher {
public:
    EventDispatcher() : m_nextSubscriptionId(1) {
        // 预分配所有可能的处理器存储
        initializeHandlerStorages();
    }

    ~EventDispatcher() = default;

    // 禁止拷贝
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    // ==================== 订阅事件 ====================

    // 订阅事件（lambda 或函数对象），返回订阅ID
    template<typename E>
    SubscriptionId subscribe(EventHandler<E> handler) {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId >= static_cast<uint32_t>(EventTypeId::MaxEventTypes)) {
            TINA_ERROR("无效的事件类型 ID: {}", typeId);
            return 0;
        }

        // 获取对应类型的处理器存储
        auto* storage = getTypedStorage<E>(typeId);
        if (!storage) {
            TINA_ERROR("无法获取事件类型 {} 的处理器存储", eventTypeIdToString(E::TYPE_ID));
            return 0;
        }

        SubscriptionId id = storage->handlers.add(Container::Move(handler));
        ++m_subscribeCount;

        // 记录订阅信息，用于取消订阅
        m_subscriptions[id] = typeId;

        return id;
    }

    // 便捷订阅：成员函数
    template<typename E, typename T>
    SubscriptionId subscribe(T* obj, void (T::*method)(const E&)) {
        return subscribe<E>([obj, method](const E& e) {
            (obj->*method)(e);
        });
    }

    // 便捷订阅：const 成员函数
    template<typename E, typename T>
    SubscriptionId subscribe(const T* obj, void (T::*method)(const E&) const) {
        return subscribe<E>([obj, method](const E& e) {
            (obj->*method)(e);
        });
    }

    // 取消订阅
    void unsubscribe(EventTypeId typeId, SubscriptionId id) {
        auto typeIndex = static_cast<uint32_t>(typeId);
        if (typeIndex >= m_handlerStorages.size()) {
            return;
        }

        auto& storage = m_handlerStorages[typeIndex];
        if (storage && storage->removeHandler(id)) {
            --m_subscribeCount;
            m_subscriptions.erase(id);
        }
    }

    // 取消某个类型的所有订阅
    void unsubscribeAll(EventTypeId typeId) {
        auto typeIndex = static_cast<uint32_t>(typeId);
        if (typeIndex >= m_handlerStorages.size()) {
            return;
        }

        auto& storage = m_handlerStorages[typeIndex];
        if (storage) {
            auto count = storage->getHandlerCount();
            storage->clear();
            m_subscribeCount -= count;

            // 清理订阅映射表
            auto it = m_subscriptions.begin();
            while (it != m_subscriptions.end()) {
                if (it->second == typeIndex) {
                    it = m_subscriptions.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // ==================== 分发事件 ====================

    // 分发单个事件（具体类型）
    template<typename E>
    void dispatch(const E& event) {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId >= m_handlerStorages.size()) {
            return;
        }

        auto* storage = getTypedStorage<E>(typeId);
        if (storage) {
            storage->handlers.dispatch(event);
            ++m_dispatchCount;
        }
    }

    // 分发事件包装器（用于队列出队后）
    void dispatch(const EventWrapper& event) {
        auto typeId = static_cast<uint32_t>(event.typeId);
        if (typeId >= m_handlerStorages.size()) {
            return;
        }

        auto& storage = m_handlerStorages[typeId];
        if (storage) {
            storage->dispatchWrapper(event);
            ++m_dispatchCount;
        }
    }

    // 批量分发（性能优化：按类型分组）
    void dispatchBatch(const Vector<EventWrapper>& events) {
        // 按类型分组
        Array<Vector<const EventWrapper*>,
              static_cast<size_t>(EventTypeId::MaxEventTypes)> batches;

        for (const auto& event : events) {
            auto typeId = static_cast<uint32_t>(event.typeId);
            if (typeId < batches.size()) {
                batches[typeId].push_back(&event);
            }
        }

        // 批量处理
        for (uint32_t typeId = 0; typeId < batches.size(); ++typeId) {
            auto& batch = batches[typeId];
            if (batch.empty()) continue;

            auto& storage = m_handlerStorages[typeId];
            if (storage) {
                for (auto* event : batch) {
                    storage->dispatchWrapper(*event);
                }
            }
        }

        m_dispatchCount += events.size();
    }

    // ==================== 管理操作 ====================

    // 清除所有处理器
    void clearAll() {
        for (auto& storage : m_handlerStorages) {
            if (storage) {
                storage->clear();
            }
        }
        m_subscriptions.clear();
        resetStats();
    }

    // 清除指定类型的处理器
    template<typename E>
    void clear() {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId < m_handlerStorages.size() && m_handlerStorages[typeId]) {
            m_handlerStorages[typeId]->clear();

            // 清理订阅映射表
            auto it = m_subscriptions.begin();
            while (it != m_subscriptions.end()) {
                if (it->second == typeId) {
                    it = m_subscriptions.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // ==================== 统计信息 ====================

    uint64_t getSubscribeCount() const { return m_subscribeCount; }
    uint64_t getDispatchCount() const { return m_dispatchCount; }

    // 获取指定类型的处理器数量
    template<typename E>
    size_t getHandlerCount() const {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId >= m_handlerStorages.size()) {
            return 0;
        }

        auto& storage = m_handlerStorages[typeId];
        return storage ? storage->getHandlerCount() : 0;
    }

    // 获取所有处理器总数
    size_t getTotalHandlerCount() const {
        size_t total = 0;
        for (const auto& storage : m_handlerStorages) {
            if (storage) {
                total += storage->getHandlerCount();
            }
        }
        return total;
    }

    // 重置统计
    void resetStats() {
        m_subscribeCount = 0;
        m_dispatchCount = 0;
    }

    // 调试信息
    void printStats() const {
        TINA_INFO("EventDispatcher 统计:");
        TINA_INFO("  订阅次数: {}", m_subscribeCount);
        TINA_INFO("  分发次数: {}", m_dispatchCount);
        TINA_INFO("  总处理器数: {}", getTotalHandlerCount());

        // 打印每种事件类型的处理器数量
        for (uint32_t i = 0; i < m_handlerStorages.size(); ++i) {
            if (m_handlerStorages[i] && m_handlerStorages[i]->getHandlerCount() > 0) {
                auto typeId = static_cast<EventTypeId>(i);
                TINA_INFO("    {}: {} 个处理器",
                    eventTypeIdToString(typeId),
                    m_handlerStorages[i]->getHandlerCount());
            }
        }
    }

private:
    // 初始化处理器存储
    void initializeHandlerStorages() {
        // 为每种事件类型预分配存储
        // 这里需要根据实际的事件类型创建对应的TypedHandlerStorage
        // 由于模板限制，我们需要显式实例化每种类型
        m_handlerStorages.resize(static_cast<size_t>(EventTypeId::MaxEventTypes));
    }

    // 获取具体类型的存储（延迟创建）
    template<typename E>
    TypedHandlerStorage<E>* getTypedStorage(uint32_t typeId) {
        if (!m_handlerStorages[typeId]) {
            // 延迟创建，避免预分配所有类型
            m_handlerStorages[typeId] = MakeUnique<TypedHandlerStorage<E>>();
        }
        // 静态转换，避免dynamic_cast
        return static_cast<TypedHandlerStorage<E>*>(m_handlerStorages[typeId].get());
    }

private:
    // 处理器存储数组（按事件类型索引）
    Vector<UniquePtr<HandlerStorageBase>> m_handlerStorages;

    // 订阅ID到类型ID的映射（用于取消订阅）
    HashMap<SubscriptionId, uint32_t> m_subscriptions;

    // 统计信息
    uint64_t m_subscribeCount = 0;
    uint64_t m_dispatchCount = 0;

    // 订阅ID生成器
    std::atomic<SubscriptionId> m_nextSubscriptionId;
};

} // namespace Tina::Engine