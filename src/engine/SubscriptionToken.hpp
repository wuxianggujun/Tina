#pragma once

#include <cstdint>
#include <atomic>
#include "../core/Container.hpp"  // 使用封装的容器

namespace Tina::Engine {

// 前向声明
class EventDispatcher;
enum class EventTypeId : uint32_t;

// 订阅ID类型
using SubscriptionId = uint64_t;

/**
 * 优化的订阅令牌 - RAII风格的事件订阅管理
 *
 * 改进：
 * - 移除std::function，避免动态内存分配
 * - 直接存储必要的信息
 * - 更轻量级的实现
 * - 添加生命周期安全检查（防止悬空指针）
 */
class SubscriptionToken {
public:
    SubscriptionToken() = default;

    // 创建一个有效的订阅令牌
    SubscriptionToken(EventDispatcher* dispatcher, EventTypeId typeId, SubscriptionId id)
        : m_dispatcher(dispatcher)
        , m_typeId(static_cast<uint32_t>(typeId))
        , m_subscriptionId(id)
        , m_valid(true) {}

    // 禁止复制
    SubscriptionToken(const SubscriptionToken&) = delete;
    SubscriptionToken& operator=(const SubscriptionToken&) = delete;

    // 允许移动
    SubscriptionToken(SubscriptionToken&& other) noexcept
        : m_dispatcher(other.m_dispatcher)
        , m_typeId(other.m_typeId)
        , m_subscriptionId(other.m_subscriptionId)
        , m_valid(other.m_valid.load(std::memory_order_relaxed)) {
        other.m_dispatcher = nullptr;
        other.m_subscriptionId = 0;
        other.m_valid.store(false, std::memory_order_relaxed);
    }

    SubscriptionToken& operator=(SubscriptionToken&& other) noexcept {
        if (this != &other) {
            unsubscribe();
            m_dispatcher = other.m_dispatcher;
            m_typeId = other.m_typeId;
            m_subscriptionId = other.m_subscriptionId;
            m_valid.store(other.m_valid.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.m_dispatcher = nullptr;
            other.m_subscriptionId = 0;
            other.m_valid.store(false, std::memory_order_relaxed);
        }
        return *this;
    }

    ~SubscriptionToken() {
        unsubscribe();
    }

    // 手动取消订阅
    void unsubscribe();  // 定义在cpp文件中，避免循环依赖

    // 检查是否有效
    bool isValid() const {
        return m_valid.load(std::memory_order_acquire) && 
               m_dispatcher != nullptr && 
               m_subscriptionId != 0;
    }

    // 失效令牌（当 EventDispatcher 销毁时调用）
    void invalidate() {
        m_valid.store(false, std::memory_order_release);
        m_dispatcher = nullptr;
    }

    // 重置令牌（取消当前订阅）
    void reset() {
        unsubscribe();
    }

    // 获取订阅ID（用于调试）
    SubscriptionId getId() const {
        return m_subscriptionId;
    }

private:
    EventDispatcher* m_dispatcher = nullptr;
    uint32_t m_typeId = 0;
    SubscriptionId m_subscriptionId = 0;
    std::atomic<bool> m_valid{false};  // 原子标志，防止多线程竞争
};

/**
 * 订阅管理器 - 管理多个订阅
 *
 */
class SubscriptionManager {
public:
    SubscriptionManager() = default;

    ~SubscriptionManager() {
        unsubscribeAll();
    }

    // 禁止复制
    SubscriptionManager(const SubscriptionManager&) = delete;
    SubscriptionManager& operator=(const SubscriptionManager&) = delete;

    // 允许移动
    SubscriptionManager(SubscriptionManager&&) = default;
    SubscriptionManager& operator=(SubscriptionManager&&) = default;

    // 添加一个订阅令牌
    void add(SubscriptionToken token) {
        m_tokens.push_back(Container::Move(token));
    }

    // 取消所有订阅
    void unsubscribeAll() {
        // 显式调用 unsubscribe 确保清理
        for (auto& token : m_tokens) {
            token.unsubscribe();
        }
        m_tokens.clear();
    }

    // 获取订阅数量
    size_t count() const {
        return m_tokens.size();
    }

    // 保留容量（预分配内存）
    void reserve(size_t capacity) {
        m_tokens.reserve(capacity);
    }

private:
    Container::Vector<SubscriptionToken> m_tokens;
};

} // namespace Tina::Engine