#include <tina/gameplay/Signal.hpp>

#include <utility>

namespace Tina::Gameplay {

namespace Detail {

// Out-of-line so the vtable is emitted in exactly one translation unit rather
// than in every one that instantiates a Signal payload type.
SignalControl::~SignalControl() = default;

} // namespace Detail

SignalSubscription::SignalSubscription(std::weak_ptr<Detail::SignalControl> control, u32 slot,
                                       u32 generation) noexcept
    : m_control(std::move(control)), m_slot(slot), m_generation(generation)
{
}

SignalSubscription::~SignalSubscription() noexcept
{
    reset();
}

SignalSubscription::SignalSubscription(SignalSubscription&& other) noexcept
    : m_control(std::move(other.m_control)), m_slot(std::exchange(other.m_slot, 0)),
      m_generation(std::exchange(other.m_generation, 0))
{
    other.m_control.reset();
}

SignalSubscription& SignalSubscription::operator=(SignalSubscription&& other) noexcept
{
    if (this != &other) {
        reset();
        m_control = std::move(other.m_control);
        m_slot = std::exchange(other.m_slot, 0);
        m_generation = std::exchange(other.m_generation, 0);
        other.m_control.reset();
    }
    return *this;
}

void SignalSubscription::reset() noexcept
{
    // An expired signal makes this a no-op instead of a dangling write, which is
    // what lets a State release its owners in any order.
    if (const std::shared_ptr<Detail::SignalControl> control = m_control.lock()) {
        control->unsubscribeSlot(m_slot, m_generation);
    }
    m_control.reset();
    m_slot = 0;
    m_generation = 0;
}

bool SignalSubscription::isActive() const noexcept
{
    const std::shared_ptr<Detail::SignalControl> control = m_control.lock();
    return control != nullptr && control->isSlotActive(m_slot, m_generation);
}

} // namespace Tina::Gameplay
