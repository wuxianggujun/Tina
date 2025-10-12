//
// EventPool.hpp - 事件对象池
// 优化内存分配，减少频繁创建销毁的开销
//

#pragma once

#include "EventCore.hpp"
#include "../core/Memory.hpp"
#include "../core/Container.hpp"  // 使用封装的容器
#include <EASTL/fixed_pool.h>

namespace Tina::Engine {

using namespace Tina::Container;  // 使用容器命名空间

// ==================== 单个事件类型的对象池 ====================

template<typename E>
class TypedEventPool {
public:
    static constexpr size_t INITIAL_POOL_SIZE = 32;
    static constexpr size_t GROW_SIZE = 16;

    TypedEventPool() {
        m_pool.reserve(INITIAL_POOL_SIZE);
        for (size_t i = 0; i < INITIAL_POOL_SIZE; ++i) {
            m_pool.emplace_back();
        }
        m_available.reserve(INITIAL_POOL_SIZE);
        for (size_t i = 0; i < INITIAL_POOL_SIZE; ++i) {
            m_available.push_back(&m_pool[i]);
        }
    }

    // 获取一个事件对象
    E* acquire() {
        if (m_available.empty()) {
            grow();
        }
        E* event = m_available.back();
        m_available.pop_back();
        return event;
    }

    // 归还一个事件对象
    void release(E* event) {
        // 重置事件对象到默认状态
        *event = E{};
        m_available.push_back(event);
    }

    // 预分配更多对象
    void reserve(size_t count) {
        while (m_pool.size() < count) {
            grow();
        }
    }

    // 获取池统计信息
    size_t poolSize() const { return m_pool.size(); }
    size_t availableCount() const { return m_available.size(); }
    size_t usedCount() const { return m_pool.size() - m_available.size(); }

private:
    void grow() {
        size_t oldSize = m_pool.size();
        m_pool.reserve(oldSize + GROW_SIZE);

        for (size_t i = 0; i < GROW_SIZE; ++i) {
            m_pool.emplace_back();
        }

        for (size_t i = oldSize; i < m_pool.size(); ++i) {
            m_available.push_back(&m_pool[i]);
        }
    }

    Vector<E> m_pool;
    Vector<E*> m_available;
};

// ==================== 全局事件池管理器 ====================

class EventPoolManager {
public:
    static EventPoolManager& getInstance() {
        static EventPoolManager instance;
        return instance;
    }

    // 获取特定类型的事件对象
    template<typename E>
    E* acquire() {
        auto& pool = getPool<E>();
        return pool.acquire();
    }

    // 归还事件对象
    template<typename E>
    void release(E* event) {
        auto& pool = getPool<E>();
        pool.release(event);
    }

    // 创建并初始化事件
    template<typename E, typename... Args>
    E* create(Args&&... args) {
        E* event = acquire<E>();
        *event = E{eastl::forward<Args>(args)...};
        return event;
    }

    // RAII 包装器，自动归还事件
    template<typename E>
    class PooledEvent {
    public:
        explicit PooledEvent(E* event = nullptr) : m_event(event) {}

        ~PooledEvent() {
            if (m_event) {
                EventPoolManager::getInstance().release(m_event);
            }
        }

        // 禁止复制
        PooledEvent(const PooledEvent&) = delete;
        PooledEvent& operator=(const PooledEvent&) = delete;

        // 允许移动
        PooledEvent(PooledEvent&& other) noexcept : m_event(other.m_event) {
            other.m_event = nullptr;
        }

        PooledEvent& operator=(PooledEvent&& other) noexcept {
            if (this != &other) {
                if (m_event) {
                    EventPoolManager::getInstance().release(m_event);
                }
                m_event = other.m_event;
                other.m_event = nullptr;
            }
            return *this;
        }

        E* operator->() { return m_event; }
        const E* operator->() const { return m_event; }
        E& operator*() { return *m_event; }
        const E& operator*() const { return *m_event; }

        E* get() { return m_event; }
        const E* get() const { return m_event; }

        E* release() {
            E* tmp = m_event;
            m_event = nullptr;
            return tmp;
        }

    private:
        E* m_event;
    };

    // 创建池化事件（RAII）
    template<typename E, typename... Args>
    PooledEvent<E> makePooled(Args&&... args) {
        return PooledEvent<E>(create<E>(eastl::forward<Args>(args)...));
    }

    // 打印池统计信息
    void printStats() const {
        TINA_INFO("===== 事件池统计 =====");
        // 这里可以添加具体的统计信息
    }

private:
    EventPoolManager() = default;

    // 获取或创建特定类型的池
    template<typename E>
    TypedEventPool<E>& getPool() {
        static TypedEventPool<E> pool;
        return pool;
    }
};

// ==================== 便捷函数 ====================

// 使用事件池触发事件
template<typename E, typename... Args>
void triggerPooledEvent(EventSystem& eventSystem, Args&&... args) {
    auto pooledEvent = EventPoolManager::getInstance().makePooled<E>(eastl::forward<Args>(args)...);
    eventSystem.trigger(*pooledEvent);
}

// 使用事件池入队事件
template<typename E, typename... Args>
void enqueuePooledEvent(EventSystem& eventSystem, Args&&... args) {
    auto pooledEvent = EventPoolManager::getInstance().makePooled<E>(eastl::forward<Args>(args)...);
    eventSystem.enqueue(*pooledEvent);
    // pooledEvent 析构时会自动归还到池中
}

} // namespace Tina::Engine