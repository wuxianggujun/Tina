#include "SubscriptionToken.hpp"
#include "EventDispatcher.hpp"

namespace Tina::Engine {

void SubscriptionToken::unsubscribe() {
    // 检查令牌是否有效（防止重复取消订阅或访问已销毁的 dispatcher）
    if (!m_valid.load(std::memory_order_acquire)) {
        return;
    }
    
    const auto lifetime = m_dispatcherLifetime.lock();
    if (lifetime && lifetime->load(std::memory_order_acquire) &&
        m_dispatcher && m_subscriptionId != 0) {
        m_dispatcher->unsubscribe(static_cast<EventTypeId>(m_typeId), m_subscriptionId);
    }

    m_dispatcher = nullptr;
    m_subscriptionId = 0;
    m_dispatcherLifetime.reset();
    m_valid.store(false, std::memory_order_release);
}

} // namespace Tina::Engine
