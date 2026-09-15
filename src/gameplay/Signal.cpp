#include <tina/gameplay/Signal.hpp>

#include <utility>

namespace Tina::Gameplay {

namespace Detail {

// Out-of-line so the vtable is emitted in exactly one translation unit rather
// than in every one that instantiates a Signal payload type.
SignalControl::~SignalControl() = default;

} // namespace Detail

SignalSubscription::SignalSubscription(std::weak_ptr<Detail::SignalControl> control,
                                       Detail::SignalSlotId slot) noexcept
    : m_control(std::move(control)), m_slot(slot)
{
}

SignalSubscription::~SignalSubscription() noexcept
{
    reset();
}

SignalSubscription::SignalSubscription(SignalSubscription&& other) noexcept
    : m_control(std::move(other.m_control)), m_slot(std::exchange(other.m_slot, {}))
{
    other.m_control.reset();
}

SignalSubscription& SignalSubscription::operator=(SignalSubscription&& other) noexcept
{
    if (this != &other) {
        // Publish the incoming identity before destroying the previous callback.
        // A capture destructor may assign this same token; its later assignment
        // must not be silently overwritten (and leave an unowned subscription).
        SignalSubscription previous(std::move(*this));
        m_control = std::move(other.m_control);
        m_slot = std::exchange(other.m_slot, {});
        other.m_control.reset();
    }
    return *this;
}

void SignalSubscription::reset() noexcept
{
    // An expired signal makes this a no-op instead of a dangling write, which is
    // what lets a State release its owners in any order.
    auto previous = std::move(m_control);
    const auto slot = std::exchange(m_slot, {});
    m_control.reset();
    if (const std::shared_ptr<Detail::SignalControl> control = previous.lock()) {
        control->unsubscribeSlot(slot);
    }
}

bool SignalSubscription::isActive() const noexcept
{
    const std::shared_ptr<Detail::SignalControl> control = m_control.lock();
    return control != nullptr && control->isSlotActive(m_slot);
}

} // namespace Tina::Gameplay
