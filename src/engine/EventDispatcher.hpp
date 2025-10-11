//
// EventDispatcher.hpp - 事件分发器（基于 EASTL fixed_function）
// 职责：订阅事件、分发事件、管理处理器
// 性能：O(1) 订阅，O(H) 分发（H 是处理器数量）
//

#pragma once

#include "EventCore.hpp"
#include "EventQueue.hpp"
#include "../core/Log.hpp"
#include <EASTL/fixed_function.h>
#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <EASTL/unique_ptr.h>

namespace Tina::Engine {

// ==================== 事件处理器类型 ====================

// 事件处理器（使用 eastl::fixed_function，64 字节栈内存）
template<typename E>
using EventHandler = eastl::fixed_function<HANDLER_FUNCTION_SIZE, void(const E&)>;

// ==================== 类型擦除包装器 ====================

// 处理器包装器基类（用于类型擦除）
struct HandlerWrapperBase {
    virtual ~HandlerWrapperBase() = default;
    virtual void invoke(const EventWrapper& event) = 0;
    virtual size_t getHandlerCount() const = 0;
};

// 具体类型的处理器包装器
template<typename E>
struct TypedHandlerWrapper : HandlerWrapperBase {
    eastl::vector<EventHandler<E>> handlers;

    void invoke(const EventWrapper& event) override {
        // 从 EventWrapper 中提取具体事件
        const E* concreteEvent = event.as<E>();
        if (concreteEvent) {
            for (auto& handler : handlers) {
                if (handler) {  // 检查有效性
                    handler(*concreteEvent);
                }
            }
        }
    }

    size_t getHandlerCount() const override {
        return handlers.size();
    }

    void addHandler(EventHandler<E> handler) {
        handlers.push_back(eastl::move(handler));
    }

    void clear() {
        handlers.clear();
    }
};

// ==================== 事件分发器 ====================

class EventDispatcher {
public:
    EventDispatcher() {
        // 初始化处理器数组（所有元素为 nullptr）
        for (auto& wrapper : m_handlers) {
            wrapper = nullptr;
        }
    }

    ~EventDispatcher() = default;

    // 禁止拷贝
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    // ==================== 订阅事件 ====================

    // 订阅事件（lambda 或函数对象）
    template<typename E>
    void subscribe(EventHandler<E> handler) {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId >= static_cast<uint32_t>(EventTypeId::MaxEventTypes)) {
            TINA_ERROR("无效的事件类型 ID: {}", typeId);
            return;
        }

        // 获取或创建对应类型的包装器
        auto& wrapper = m_handlers[typeId];
        if (!wrapper) {
            wrapper = eastl::make_unique<TypedHandlerWrapper<E>>();
        }

        // 类型安全检查（运行时）
        auto* typedWrapper = dynamic_cast<TypedHandlerWrapper<E>*>(wrapper.get());
        if (!typedWrapper) {
            TINA_ERROR("事件类型不匹配: {}", eventTypeIdToString(E::TYPE_ID));
            return;
        }

        typedWrapper->addHandler(eastl::move(handler));
        ++m_subscribeCount;
    }

    // 便捷订阅：成员函数
    template<typename E, typename T>
    void subscribe(T* obj, void (T::*method)(const E&)) {
        subscribe<E>([obj, method](const E& e) {
            (obj->*method)(e);
        });
    }

    // 便捷订阅：const 成员函数
    template<typename E, typename T>
    void subscribe(const T* obj, void (T::*method)(const E&) const) {
        subscribe<E>([obj, method](const E& e) {
            (obj->*method)(e);
        });
    }

    // ==================== 分发事件 ====================

    // 分发单个事件（具体类型）
    template<typename E>
    void dispatch(const E& event) {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId >= m_handlers.size()) {
            return;
        }

        auto& wrapper = m_handlers[typeId];
        if (wrapper) {
            EventWrapper eventWrapper(event);
            wrapper->invoke(eventWrapper);
            ++m_dispatchCount;
        }
    }

    // 分发事件包装器（用于队列出队后）
    void dispatch(const EventWrapper& event) {
        auto typeId = static_cast<uint32_t>(event.typeId);
        if (typeId >= m_handlers.size()) {
            return;
        }

        auto& wrapper = m_handlers[typeId];
        if (wrapper) {
            wrapper->invoke(event);
            ++m_dispatchCount;
        }
    }

    // 批量分发（性能优化：按类型分组）
    void dispatchBatch(const eastl::vector<EventWrapper>& events) {
        // 按类型分组
        eastl::array<eastl::vector<const EventWrapper*>,
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

            auto& wrapper = m_handlers[typeId];
            if (wrapper) {
                for (auto* event : batch) {
                    wrapper->invoke(*event);
                }
            }
        }

        m_dispatchCount += events.size();
    }

    // ==================== 管理操作 ====================

    // 清除所有处理器
    void clearAll() {
        for (auto& wrapper : m_handlers) {
            wrapper.reset();
        }
        resetStats();
    }

    // 清除指定类型的处理器
    template<typename E>
    void clear() {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId < m_handlers.size()) {
            m_handlers[typeId].reset();
        }
    }

    // ==================== 统计信息 ====================

    uint64_t getSubscribeCount() const { return m_subscribeCount; }
    uint64_t getDispatchCount() const { return m_dispatchCount; }

    // 获取指定类型的处理器数量
    template<typename E>
    size_t getHandlerCount() const {
        auto typeId = static_cast<uint32_t>(E::TYPE_ID);
        if (typeId >= m_handlers.size()) {
            return 0;
        }

        auto& wrapper = m_handlers[typeId];
        return wrapper ? wrapper->getHandlerCount() : 0;
    }

    // 获取所有处理器总数
    size_t getTotalHandlerCount() const {
        size_t total = 0;
        for (const auto& wrapper : m_handlers) {
            if (wrapper) {
                total += wrapper->getHandlerCount();
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
        for (uint32_t i = 0; i < m_handlers.size(); ++i) {
            if (m_handlers[i] && m_handlers[i]->getHandlerCount() > 0) {
                auto typeId = static_cast<EventTypeId>(i);
                TINA_INFO("    {}: {} 个处理器",
                    eventTypeIdToString(typeId),
                    m_handlers[i]->getHandlerCount());
            }
        }
    }

private:
    // 处理器数组（按事件类型索引）
    eastl::array<eastl::unique_ptr<HandlerWrapperBase>,
                 static_cast<size_t>(EventTypeId::MaxEventTypes)> m_handlers;

    // 统计信息
    uint64_t m_subscribeCount = 0;
    uint64_t m_dispatchCount = 0;
};

} // namespace Tina::Engine
