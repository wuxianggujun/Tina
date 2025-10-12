#include "SubscriptionToken.hpp"
#include "EventDispatcher.hpp"

namespace Tina::Engine {

void SubscriptionToken::unsubscribe() {
    if (m_dispatcher && m_subscriptionId != 0) {
        m_dispatcher->unsubscribe(static_cast<EventTypeId>(m_typeId), m_subscriptionId);
        m_dispatcher = nullptr;
        m_subscriptionId = 0;
    }
}

} // namespace Tina::Engine