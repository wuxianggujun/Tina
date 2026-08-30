#pragma once

#include <atomic>

namespace Tina::Core {

// A one-way "stop was requested" flag, and a non-owning view of one that a long-running
// synchronous operation polls.
//
// This exists instead of std::stop_token because libc++ keeps stop_token behind
// _LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN through NDK 28, so a public header naming it makes
// the whole module uncompilable for Android. Enabling _LIBCPP_ENABLE_EXPERIMENTAL would turn
// on every incomplete libc++ feature at once, which is a much larger bet than this type.
//
// It is deliberately smaller than std::stop_token rather than a reimplementation of it:
// there are no stop_callback registrations, no shared-ownership refcount and no
// intrusive callback list -- the only thing any Tina caller ever did with a stop_token was
// ask stop_requested() between work items. Callers that own a thread can keep using
// std::jthread on desktop and hand its token's state through a signal.
class CancellationSignal final {
  public:
    CancellationSignal() noexcept = default;

    CancellationSignal(const CancellationSignal&) = delete;
    CancellationSignal& operator=(const CancellationSignal&) = delete;
    CancellationSignal(CancellationSignal&&) = delete;
    CancellationSignal& operator=(CancellationSignal&&) = delete;

    // Latching: once requested, a signal never goes back to un-requested. A resettable
    // signal would let an operation observe "not cancelled" after a cancel it should have
    // honoured, purely depending on when it happened to poll.
    void requestCancellation() noexcept
    {
        m_requested.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool cancellationRequested() const noexcept
    {
        return m_requested.load(std::memory_order_acquire);
    }

  private:
    std::atomic<bool> m_requested{false};
};

// Non-owning view. An empty token never reports cancellation, so a caller with nothing to
// cancel passes {} and the polling code needs no null handling.
class CancellationToken final {
  public:
    CancellationToken() noexcept = default;

    explicit CancellationToken(const CancellationSignal& signal) noexcept : m_signal(&signal) {}

    [[nodiscard]] bool cancellationRequested() const noexcept
    {
        return m_signal != nullptr && m_signal->cancellationRequested();
    }

  private:
    // The signal must outlive every token observing it. Tokens are passed down a synchronous
    // call chain whose signal is owned by the caller driving it, so the lifetime is a
    // strict nesting rather than shared ownership.
    const CancellationSignal* m_signal = nullptr;
};

} // namespace Tina::Core
