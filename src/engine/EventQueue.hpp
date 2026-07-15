//
// EventQueue.hpp - 事件队列（基于 EASTL ring_buffer）
// 职责：FIFO 事件队列，零动态内存分配
// 性能：O(1) 入队/出队，固定容量
//

#pragma once

#include "EventCore.hpp"
#include "../core/Log.hpp"
#include "../core/Container.hpp"
#include <type_traits>
#include <cstring>  // for std::memcpy

namespace Tina::Engine {

using namespace Tina::Container;

// ==================== 通用事件包装器 ====================

// 事件包装器（用于存储任意类型的事件）
// 重要：只支持POD类型事件，确保内存安全
struct EventWrapper {
    EventTypeId typeId = EventTypeId::None;
    uint32_t timestamp = 0;
    EventPriority priority = EventPriority::Medium;

    // 事件数据缓冲区
    // 注意：只支持trivially copyable和trivially destructible的类型
    // 不需要调用析构函数，因为只存储POD数据
    alignas(8) uint8_t data[128];  // 128字节应该足够大部分事件

    EventWrapper() = default;

    // 从具体事件构造（只接受POD类型）
    template<typename E>
    explicit EventWrapper(const E& event) {
        static_assert(sizeof(E) <= sizeof(data), "事件大小超过 128 字节");
        static_assert(std::is_trivially_copyable_v<E>, "事件必须是 trivially copyable");
        static_assert(std::is_trivially_destructible_v<E>, "事件必须是 trivially destructible");

        typeId = E::TYPE_ID;
        timestamp = event.timestamp;
        priority = event.priority;

        // 使用memcpy进行POD数据拷贝，避免placement new
        std::memcpy(data, &event, sizeof(E));
    }

    // 获取事件指针（需要外部保证类型正确）
    template<typename E>
    const E* as() const {
        static_assert(std::is_trivially_copyable_v<E>, "事件必须是 trivially copyable");
        static_assert(std::is_trivially_destructible_v<E>, "事件必须是 trivially destructible");
        return reinterpret_cast<const E*>(data);
    }

    template<typename E>
    E* as() {
        static_assert(std::is_trivially_copyable_v<E>, "事件必须是 trivially copyable");
        static_assert(std::is_trivially_destructible_v<E>, "事件必须是 trivially destructible");
        return reinterpret_cast<E*>(data);
    }

    // 拷贝构造和赋值（POD类型，使用默认实现）
    EventWrapper(const EventWrapper&) = default;
    EventWrapper& operator=(const EventWrapper&) = default;
};

// ==================== 事件队列 ====================

// 事件队列（基于 ring_buffer，FIFO，无动态分配）
template<size_t Capacity = EVENT_QUEUE_CAPACITY>
class EventQueue {
public:
    // EASTL ring_buffer 要求底层容器在构造时已经 resize，并保留一个哨兵槽区分满/空。
    EventQueue() : m_buffer(Capacity) {}
    ~EventQueue() = default;

    // 禁止拷贝
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    // ==================== 入队操作 ====================

    // 入队事件（拷贝）
    template<typename E>
    bool push(const E& event) {
        static_assert(std::is_trivially_copyable_v<E>, "Event must be trivially copyable for queueing");
        static_assert(std::is_trivially_destructible_v<E>, "Event must be trivially destructible for queueing");
        if (m_buffer.size() >= Capacity) {
            ++m_overflowCount;
            TINA_WARN("事件队列已满，丢弃事件：{}", eventTypeIdToString(E::TYPE_ID));
            return false;
        }

        EventWrapper wrapper(event);
        m_buffer.push_back(wrapper);
        ++m_pushCount;
        return true;
    }

    // 入队事件（移动）
    template<typename E>
    bool push(E&& event) {
        static_assert(std::is_trivially_copyable_v<E>, "Event must be trivially copyable for queueing");
        static_assert(std::is_trivially_destructible_v<E>, "Event must be trivially destructible for queueing");
        if (m_buffer.size() >= Capacity) {
            ++m_overflowCount;
            TINA_WARN("事件队列已满，丢弃事件：{}", eventTypeIdToString(E::TYPE_ID));
            return false;
        }

        EventWrapper wrapper(event);
        m_buffer.push_back(wrapper);
        ++m_pushCount;
        return true;
    }

    // ==================== 出队操作 ====================

    // 出队事件
    bool pop(EventWrapper& event) {
        if (m_buffer.empty()) {
            return false;
        }

        event = m_buffer.front();
        m_buffer.pop_front();
        ++m_popCount;
        return true;
    }

    // 查看队首事件（不移除）
    const EventWrapper* peek() const {
        if (m_buffer.empty()) {
            return nullptr;
        }
        return &m_buffer.front();
    }

    // ==================== 状态查询 ====================

    size_t size() const { return m_buffer.size(); }
    bool empty() const { return m_buffer.empty(); }
    bool full() const { return m_buffer.size() >= Capacity; }
    size_t capacity() const { return Capacity; }

    // ==================== 统计信息 ====================

    uint64_t getPushCount() const { return m_pushCount; }
    uint64_t getPopCount() const { return m_popCount; }
    uint64_t getOverflowCount() const { return m_overflowCount; }

    // 重置统计
    void resetStats() {
        m_pushCount = 0;
        m_popCount = 0;
        m_overflowCount = 0;
    }

    // 清空队列
    void clear() {
        while (!m_buffer.empty()) {
            m_buffer.pop_front();
        }
    }

private:
    using BufferContainer = FixedVector<EventWrapper, Capacity + 1, false>;
    RingBuffer<EventWrapper, BufferContainer> m_buffer;

    // 统计信息
    uint64_t m_pushCount = 0;
    uint64_t m_popCount = 0;
    uint64_t m_overflowCount = 0;
};

// ==================== 对象池（可选，用于减少事件分配） ====================

// 简单的对象池实现（基于 fixed_vector）
template<typename T, size_t Capacity = EVENT_POOL_CAPACITY>
class ObjectPool {
public:
    ObjectPool() {
        // 预分配所有对象
        m_storage.resize(Capacity);

        // 初始化空闲列表
        m_freeList.reserve(Capacity);
        for (size_t i = 0; i < Capacity; ++i) {
            m_freeList.push_back(&m_storage[i]);
        }
    }

    // 分配对象
    T* allocate() {
        if (m_freeList.empty()) {
            ++m_exhaustedCount;
            TINA_WARN("对象池耗尽: {}", typeid(T).name());
            return nullptr;
        }

        T* obj = m_freeList.back();
        m_freeList.pop_back();
        ++m_allocCount;
        return obj;
    }

    // 释放对象
    void deallocate(T* obj) {
        if (obj == nullptr) return;
        m_freeList.push_back(obj);
        ++m_deallocCount;
    }

    // 统计信息
    size_t getCapacity() const { return Capacity; }
    size_t getAvailable() const { return m_freeList.size(); }
    size_t getInUse() const { return Capacity - m_freeList.size(); }
    uint64_t getAllocCount() const { return m_allocCount; }
    uint64_t getDeallocCount() const { return m_deallocCount; }
    uint64_t getExhaustedCount() const { return m_exhaustedCount; }

private:
    FixedVector<T, Capacity, false> m_storage;  // 不允许溢出
    Vector<T*> m_freeList;

    uint64_t m_allocCount = 0;
    uint64_t m_deallocCount = 0;
    uint64_t m_exhaustedCount = 0;
};

} // namespace Tina::Engine
