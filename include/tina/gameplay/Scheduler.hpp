#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/gameplay/GameplayTypes.hpp>

#include <memory_resource>
#include <optional>

namespace Tina::Gameplay {

namespace Detail {
struct SchedulerTimerTag final {
};
} // namespace Detail

using TimerId = Core::GenerationId<Detail::SchedulerTimerTag>;

// What the callback is told about the delivery it is handling. Passed by const
// reference so adding a field later does not change every call site's signature.
struct TimerEvent final {
    TimerId timer{};
    // 1-based. A repeating timer's callback can therefore tell "first tick" from
    // "steady state" without keeping its own counter.
    Core::u32 iteration = 0;
    Core::Duration interval{};
};

using TimerCallback = Core::MoveOnlyFunction<void(const TimerEvent&)>;

struct TimerDesc final {
    // Time between deliveries. Zero means "once per advance()": the catch-up loop
    // cannot subdivide a zero interval, so such a timer fires exactly one time per
    // advance regardless of how large the delta was.
    Core::Duration interval{};
    // Time until the *first* delivery. Absent means "one interval", which is the
    // usual periodic timer. Stated as an optional rather than overloading zero,
    // because a zero initial delay is a real request -- fire on the next advance --
    // and must not be confused with "unset".
    std::optional<Core::Duration> initialDelay{};
    Repeat repeat = Repeat::once();
    // Advances with the unscaled delta. A pause menu's own animations and any
    // "unstick the player" watchdog need this: scaling them by the gameplay time
    // scale means they stop exactly when the game is paused, which is when they
    // are needed.
    bool ignoresTimeScale = false;
    bool startPaused = false;
    TimerCallback callback{};
};

struct SchedulerConfig final {
    // Fixed at Create. Exceeding it is CapacityExceeded rather than a
    // reallocation, matching every other bounded owner in the engine.
    Core::usize timerCapacity = 256;
    // Upper bound on deliveries one timer may receive from a single advance().
    // A 10 ms timer and a 500 ms hitch owe 50 deliveries; firing all of them turns
    // one stall into a second one, and firing just one silently loses 49. So the
    // backlog is bounded and the discarded remainder is counted, which makes the
    // loss visible in stats instead of invisible in behaviour.
    Core::u32 maximumCatchUpStepsPerAdvance = 8;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct SchedulerStats final {
    Core::usize timerCapacity = 0;
    Core::usize activeTimerCount = 0;
    Core::usize activeTimerHighWater = 0;
    Core::u64 advanceCount = 0;
    Core::u64 deliveredCount = 0;
    // Deliveries the catch-up bound refused to make. Non-zero means timers ran
    // behind real time, not that anything failed.
    Core::u64 discardedCatchUpSteps = 0;
    Core::u64 cancelledCount = 0;
};

// Fixed-capacity owner-thread timer registry.
//
// It exists because "run this in 0.4 seconds" and "run this every 2 seconds" are
// the two most common gameplay requests in any game, and without an owner every
// State grows its own float accumulator plus a bool guard. Those hand-rolled
// copies are where the same three defects keep appearing: leftover delta thrown
// away so periods drift, no bound on catch-up so a hitch fires a callback fifty
// times, and cancelling a timer from inside its own callback corrupting the list
// being iterated.
//
// Time comes from the caller as an explicit delta rather than from a clock this
// object samples: the frame loop already owns the fixed/frame split (ADR 0015),
// and a scheduler that read a clock itself could not be driven from fixedUpdate
// at all.
//
// Everything here is single-owner and not thread-safe. Callbacks may schedule and
// cancel freely, including cancelling themselves; a timer scheduled from inside a
// callback first runs on the *next* advance, so dispatch order never depends on
// how deeply the callbacks nested.
class Scheduler final {
  public:
    [[nodiscard]] static Core::Result<Scheduler> Create(SchedulerConfig config = {});

    ~Scheduler() noexcept;

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&& other) noexcept;
    Scheduler& operator=(Scheduler&& other) noexcept;

    [[nodiscard]] Core::Result<TimerId> schedule(TimerDesc desc);

    // One delivery after `delay`.
    [[nodiscard]] Core::Result<TimerId> scheduleAfter(Core::Duration delay, TimerCallback callback);
    // Deliveries every `interval` until cancelled, the first one after one interval.
    [[nodiscard]] Core::Result<TimerId> scheduleEvery(Core::Duration interval, TimerCallback callback);

    // Stale or unknown ids are InvalidHandle rather than success, because a
    // double cancel usually means two owners each believe they hold the timer.
    [[nodiscard]] Core::Status cancel(TimerId timer);
    // Callbacks are destroyed here, so anything they captured is released.
    void cancelAll() noexcept;

    [[nodiscard]] Core::Status setPaused(TimerId timer, bool paused);
    [[nodiscard]] Core::Result<bool> isPaused(TimerId timer) const;
    // Time left before the next delivery. Never negative: a timer that already
    // owes deliveries reports zero.
    [[nodiscard]] Core::Result<Core::Duration> remaining(TimerId timer) const;
    [[nodiscard]] bool isActive(TimerId timer) const noexcept;

    // Multiplies every delta except the ones for timers that opted out. Zero is a
    // full pause and is valid; negative and non-finite are rejected, because
    // running timers backwards has no defined meaning here.
    [[nodiscard]] Core::Status setTimeScale(double scale);
    [[nodiscard]] double timeScale() const noexcept;

    // Advances every unpaused timer and dispatches due deliveries. Re-entering it
    // from a callback is ReentrantDispatch and changes nothing.
    [[nodiscard]] Core::Status advance(Core::Duration delta);

    [[nodiscard]] Core::usize activeCount() const noexcept;
    [[nodiscard]] SchedulerStats stats() const noexcept;

  private:
    struct Impl;

    explicit Scheduler(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Gameplay
