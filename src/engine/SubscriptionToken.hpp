#pragma once

#include <cstdint>
#include <functional>
#include "../core/Memory.hpp"
#include "../core/Container.hpp"  // 使用封装的容器

namespace Tina::Engine {

class EventDispatcher;

/**
 * 订阅令牌 - RAII风格的事件订阅管理
 *
 * 当令牌销毁时自动取消订阅
 * 使用移动语义避免复制
 */
class SubscriptionToken {
public:
    using UnsubscribeFunc = std::function<void()>;

    SubscriptionToken() = default;

    // 创建一个有效的订阅令牌
    explicit SubscriptionToken(UnsubscribeFunc unsubscribe)
        : m_unsubscribe(std::move(unsubscribe)) {}

    // 禁止复制
    SubscriptionToken(const SubscriptionToken&) = delete;
    SubscriptionToken& operator=(const SubscriptionToken&) = delete;

    // 允许移动
    SubscriptionToken(SubscriptionToken&& other) noexcept
        : m_unsubscribe(std::move(other.m_unsubscribe)) {
        other.m_unsubscribe = nullptr;
    }

    SubscriptionToken& operator=(SubscriptionToken&& other) noexcept {
        if (this != &other) {
            unsubscribe();
            m_unsubscribe = std::move(other.m_unsubscribe);
            other.m_unsubscribe = nullptr;
        }
        return *this;
    }

    ~SubscriptionToken() {
        unsubscribe();
    }

    // 手动取消订阅
    void unsubscribe() {
        if (m_unsubscribe) {
            m_unsubscribe();
            m_unsubscribe = nullptr;
        }
    }

    // 检查是否有效
    bool isValid() const {
        return m_unsubscribe != nullptr;
    }

    // 重置令牌（取消当前订阅）
    void reset() {
        unsubscribe();
    }

private:
    UnsubscribeFunc m_unsubscribe;
};

/**
 * 订阅管理器 - 管理多个订阅
 *
 * 用于Scene等需要管理多个事件订阅的类
 */
class SubscriptionManager {
public:
    SubscriptionManager() = default;
    ~SubscriptionManager() {
        unsubscribeAll();
    }

    // 添加一个订阅令牌
    void add(SubscriptionToken token) {
        m_tokens.push_back(std::move(token));
    }

    // 取消所有订阅
    void unsubscribeAll() {
        m_tokens.clear();  // 令牌析构时会自动取消订阅
    }

    // 获取订阅数量
    size_t count() const {
        return m_tokens.size();
    }

private:
    Container::Vector<SubscriptionToken> m_tokens;
};

} // namespace Tina::Engine