#include <tina/gameplay/Scheduler.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/gameplay/GameplayErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace Tina::Gameplay {

namespace {

// Whole periods still owed after the catch-up bound stopped delivering, and the
// sub-period leftover to keep.
//
// The leftover is what keeps a repeating timer from drifting: a 100 ms timer
// advanced by 60 ms twice fires once and carries 20 ms, rather than resetting to
// zero and losing a tick every period.
//
// The owed periods are *dropped* rather than carried, and counted instead.
// Carrying them would make one hitch produce max-steps deliveries on every
// following advance until the backlog drained -- turning a single stall into a
// train of them. Dropping resyncs the timer to now, and the counter is what makes
// that loss visible in stats rather than only in behaviour.
struct Backlog final {
    Core::u64 discardedSteps = 0;
    Core::Duration remainder{};
};

[[nodiscard]] Backlog measureBacklog(Core::Duration elapsed, Core::Duration period) noexcept
{
    if (period.count() <= 0.0 || elapsed < period) {
        return Backlog{.discardedSteps = 0, .remainder = elapsed};
    }
    const double owed = std::floor(elapsed.count() / period.count());
    Core::Duration remainder{elapsed.count() - period.count() * owed};
    if (!std::isfinite(remainder.count()) || remainder.count() < 0.0) {
        remainder = Core::Duration{0.0};
    }
    // Saturated rather than wrapped: a tiny period under a large accumulated delta
    // exceeds u64, and a wrapped count would read as a small, plausible backlog.
    const double saturated =
        (std::min)(owed, static_cast<double>((std::numeric_limits<Core::u64>::max)()));
    return Backlog{.discardedSteps = static_cast<Core::u64>(saturated), .remainder = remainder};
}

} // namespace

struct Scheduler::Impl final {
    struct Entry final {
        TimerCallback callback{};
        Core::Duration interval{};
        // Counts up toward the next delivery. Seeded with `interval - initialDelay`
        // so a first delay shorter than the interval still fires early without a
        // separate "first tick" branch in advance().
        Core::Duration elapsed{};
        Core::Duration nextInterval{};
        Repeat repeat = Repeat::once();
        Core::u32 delivered = 0;
        bool ignoresTimeScale = false;
        bool paused = false;
        // Set when cancel() lands during dispatch. The callback cannot be
        // destroyed there -- it may be the frame currently running -- so the entry
        // is retired after the loop.
        bool cancelPending = false;
        // Advance sequence this timer becomes eligible at. A timer scheduled from
        // inside a callback first runs on the next advance, so delivery order does
        // not depend on how deeply the callbacks nested.
        Core::u64 armedAtAdvance = 0;
    };

    using TimerPool = Core::GenerationPool<Entry, Detail::SchedulerTimerTag>;

    Impl(const SchedulerConfig& configuration, std::pmr::memory_resource& resource,
         TimerPool&& timerPool)
        : config(configuration), memory(&resource), timers(std::move(timerPool)),
          liveTimers(std::pmr::polymorphic_allocator<TimerId>{&resource})
    {
    }

    SchedulerConfig config{};
    std::pmr::memory_resource* memory = nullptr;
    TimerPool timers;
    // Iteration order. The pool resolves ids but has no stable traversal, and
    // "timers fire in the order they were scheduled" is the only order a game can
    // reason about.
    std::pmr::vector<TimerId> liveTimers;
    double timeScale = 1.0;
    Core::u64 advanceSequence = 0;
    bool dispatching = false;
    SchedulerStats stats{};

    [[nodiscard]] Entry* find(TimerId timer) noexcept { return timers.tryGet(timer); }
    [[nodiscard]] const Entry* find(TimerId timer) const noexcept { return timers.tryGet(timer); }

    void retire(TimerId timer) noexcept
    {
        const auto position = std::find(liveTimers.begin(), liveTimers.end(), timer);
        if (position != liveTimers.end()) {
            liveTimers.erase(position);
        }
        // Destroys the callback, releasing whatever it captured.
        (void)timers.erase(timer);
    }

    void reclaimCancelled() noexcept
    {
        // Copied because retire() mutates liveTimers.
        for (Core::usize index = 0; index < liveTimers.size();) {
            const TimerId timer = liveTimers[index];
            Entry* const entry = find(timer);
            if (entry != nullptr && entry->cancelPending) {
                retire(timer);
                continue;
            }
            ++index;
        }
    }
};

Scheduler::Scheduler(Impl* impl) noexcept : m_impl(impl) {}

Scheduler::~Scheduler() noexcept
{
    delete m_impl;
    m_impl = nullptr;
}

Scheduler::Scheduler(Scheduler&& other) noexcept : m_impl(std::exchange(other.m_impl, nullptr)) {}

Scheduler& Scheduler::operator=(Scheduler&& other) noexcept
{
    if (this != &other) {
        delete m_impl;
        m_impl = std::exchange(other.m_impl, nullptr);
    }
    return *this;
}

Core::Result<Scheduler> Scheduler::Create(SchedulerConfig config)
{
    if (config.timerCapacity == 0) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "Scheduler timerCapacity must be greater than zero");
    }
    if (config.maximumCatchUpStepsPerAdvance == 0) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "Scheduler maximumCatchUpStepsPerAdvance must be greater than zero");
    }

    std::pmr::memory_resource& resource = config.memoryResource != nullptr
        ? *config.memoryResource
        : *std::pmr::get_default_resource();

    auto timers = Impl::TimerPool::Create(config.timerCapacity, resource);
    if (!timers) {
        return Core::failure(GameplayErrorCode::AllocationFailed, timers.error().message);
    }

    try {
        auto* impl = new Impl(config, resource, std::move(*timers));
        // Reserved once so scheduling never allocates afterwards.
        impl->liveTimers.reserve(config.timerCapacity);
        return Scheduler(impl);
    } catch (const std::bad_alloc&) {
        return Core::failure(GameplayErrorCode::AllocationFailed,
                             "Scheduler storage allocation failed");
    }
}

Core::Result<TimerId> Scheduler::schedule(TimerDesc desc)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration, "Scheduler was not created");
    }
    if (!desc.callback) {
        return Core::failure(GameplayErrorCode::MissingCallback, "Timer callback is empty");
    }
    if (!isValidDuration(desc.interval)) {
        return Core::failure(GameplayErrorCode::InvalidArgument,
                             "Timer interval must be finite and non-negative");
    }
    if (desc.initialDelay.has_value() && !isValidDuration(*desc.initialDelay)) {
        return Core::failure(GameplayErrorCode::InvalidArgument,
                             "Timer initialDelay must be finite and non-negative");
    }
    if (!desc.repeat.isValid()) {
        return Core::failure(GameplayErrorCode::InvalidArgument,
                             "Timer repeat count must be at least 1 unless infinite");
    }
    if (m_impl->liveTimers.size() >= m_impl->config.timerCapacity) {
        return Core::failure(GameplayErrorCode::CapacityExceeded,
                             "Scheduler timerCapacity is exhausted");
    }

    // An absent initialDelay means one interval, which is the ordinary periodic
    // timer. A present zero means "fire on the next advance", which is why the
    // field is an optional instead of a zero-means-unset duration.
    const Core::Duration firstDelay = desc.initialDelay.value_or(desc.interval);
    Impl::Entry entry{
        .callback = std::move(desc.callback),
        .interval = desc.interval,
        .elapsed = Core::Duration{0.0},
        .nextInterval = firstDelay,
        .repeat = desc.repeat,
        .delivered = 0,
        .ignoresTimeScale = desc.ignoresTimeScale,
        .paused = desc.startPaused,
        .cancelPending = false,
        .armedAtAdvance = m_impl->advanceSequence + (m_impl->dispatching ? 1 : 0),
    };

    Core::Result<TimerId> timer = m_impl->timers.tryEmplace(std::move(entry));
    if (!timer) {
        return Core::failure(GameplayErrorCode::CapacityExceeded, timer.error().message);
    }
    m_impl->liveTimers.push_back(*timer);
    m_impl->stats.activeTimerHighWater =
        (std::max)(m_impl->stats.activeTimerHighWater, m_impl->liveTimers.size());
    return *timer;
}

Core::Result<TimerId> Scheduler::scheduleAfter(Core::Duration delay, TimerCallback callback)
{
    return schedule(TimerDesc{
        .interval = delay,
        .initialDelay = delay,
        .repeat = Repeat::once(),
        .callback = std::move(callback),
    });
}

Core::Result<TimerId> Scheduler::scheduleEvery(Core::Duration interval, TimerCallback callback)
{
    return schedule(TimerDesc{
        .interval = interval,
        .repeat = Repeat::forever(),
        .callback = std::move(callback),
    });
}

Core::Status Scheduler::cancel(TimerId timer)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration, "Scheduler was not created");
    }
    Impl::Entry* const entry = m_impl->find(timer);
    if (entry == nullptr || entry->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle,
                             "timer handle is unknown or already cancelled");
    }
    ++m_impl->stats.cancelledCount;
    if (m_impl->dispatching) {
        // This may be the timer whose callback is running; retiring it here would
        // destroy that callback mid-invocation.
        entry->cancelPending = true;
        return Core::success();
    }
    m_impl->retire(timer);
    return Core::success();
}

void Scheduler::cancelAll() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    if (m_impl->dispatching) {
        for (const TimerId timer : m_impl->liveTimers) {
            Impl::Entry* const entry = m_impl->find(timer);
            if (entry != nullptr && !entry->cancelPending) {
                entry->cancelPending = true;
                ++m_impl->stats.cancelledCount;
            }
        }
        return;
    }
    m_impl->stats.cancelledCount += m_impl->liveTimers.size();
    m_impl->liveTimers.clear();
    m_impl->timers.clear();
}

Core::Status Scheduler::setPaused(TimerId timer, bool paused)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration, "Scheduler was not created");
    }
    Impl::Entry* const entry = m_impl->find(timer);
    if (entry == nullptr || entry->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle, "timer handle is unknown");
    }
    entry->paused = paused;
    return Core::success();
}

Core::Result<bool> Scheduler::isPaused(TimerId timer) const
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration, "Scheduler was not created");
    }
    const Impl::Entry* const entry = m_impl->find(timer);
    if (entry == nullptr || entry->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle, "timer handle is unknown");
    }
    return entry->paused;
}

Core::Result<Core::Duration> Scheduler::remaining(TimerId timer) const
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration, "Scheduler was not created");
    }
    const Impl::Entry* const entry = m_impl->find(timer);
    if (entry == nullptr || entry->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle, "timer handle is unknown");
    }
    const Core::Duration left = entry->nextInterval - entry->elapsed;
    return left.count() > 0.0 ? left : Core::Duration{0.0};
}

bool Scheduler::isActive(TimerId timer) const noexcept
{
    if (m_impl == nullptr) {
        return false;
    }
    const Impl::Entry* const entry = m_impl->find(timer);
    return entry != nullptr && !entry->cancelPending;
}

Core::Status Scheduler::setTimeScale(double scale)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration, "Scheduler was not created");
    }
    if (!isValidTimeScale(scale)) {
        return Core::failure(GameplayErrorCode::InvalidArgument,
                             "time scale must be finite and non-negative");
    }
    m_impl->timeScale = scale;
    return Core::success();
}

double Scheduler::timeScale() const noexcept
{
    return m_impl != nullptr ? m_impl->timeScale : 1.0;
}

Core::Status Scheduler::advance(Core::Duration delta)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration, "Scheduler was not created");
    }
    if (!isValidDuration(delta)) {
        return Core::failure(GameplayErrorCode::InvalidArgument,
                             "advance delta must be finite and non-negative");
    }
    if (m_impl->dispatching) {
        return Core::failure(GameplayErrorCode::ReentrantDispatch,
                             "Scheduler::advance was re-entered from a timer callback");
    }

    Impl& impl = *m_impl;
    const Core::u64 sequence = impl.advanceSequence;
    ++impl.stats.advanceCount;
    impl.dispatching = true;
    // A guard rather than end-of-function restores: a callback is game code and may
    // throw, and a scheduler left permanently "dispatching" would refuse every
    // later advance for the rest of the process.
    auto endDispatch = Core::makeScopeExit([&impl]() noexcept {
        impl.dispatching = false;
        ++impl.advanceSequence;
        impl.reclaimCancelled();
    });

    const Core::Duration scaledDelta{delta.count() * impl.timeScale};

    // Indexed over a size captured before the loop: a callback may schedule new
    // timers, and those are armed for the next advance anyway.
    const Core::usize timerCount = impl.liveTimers.size();
    for (Core::usize index = 0; index < timerCount; ++index) {
        // Re-resolved every iteration rather than held: a callback may cancel other
        // timers, and liveTimers only shrinks after the loop, so the id can outlive
        // its entry within it.
        const TimerId timer = impl.liveTimers[index];
        Impl::Entry* entry = impl.find(timer);
        if (entry == nullptr || entry->cancelPending || entry->paused ||
            entry->armedAtAdvance > sequence) {
            continue;
        }

        entry->elapsed += entry->ignoresTimeScale ? delta : scaledDelta;

        // Deliveries are stepped one period at a time rather than computed as a
        // single count, because the first period is nextInterval (the initial
        // delay) while every later one is interval. Collapsing them into one
        // division would use the delay as the period for the whole catch-up
        // backlog, which is wrong in both directions depending on which is larger.
        Core::u32 steps = 0;
        while (steps < impl.config.maximumCatchUpStepsPerAdvance) {
            entry = impl.find(timer);
            if (entry == nullptr || entry->cancelPending ||
                entry->repeat.isComplete(entry->delivered)) {
                break;
            }
            const Core::Duration period = entry->nextInterval;
            // A zero period cannot be subdivided, so such a timer delivers exactly
            // once per advance instead of spinning to the catch-up bound.
            if (period.count() > 0.0 && entry->elapsed < period) {
                break;
            }

            // The accumulator is settled before dispatching: a callback that reads
            // remaining() should see the post-tick value, and one that cancels this
            // timer must not leave a partially updated accumulator behind.
            entry->elapsed = period.count() > 0.0 ? (entry->elapsed - period)
                                                  : Core::Duration{0.0};
            // After the first delivery the timer runs on its own interval, so a
            // one-off initial delay never becomes the period.
            entry->nextInterval = entry->interval;
            ++entry->delivered;
            ++steps;

            const TimerEvent event{
                .timer = timer,
                .iteration = entry->delivered,
                .interval = entry->interval,
            };
            ++impl.stats.deliveredCount;
            entry->callback(event);

            // Re-resolved after the callback: it may have cancelled this timer, or
            // cancelled and rescheduled into the same pool slot.
            entry = impl.find(timer);
            if (entry == nullptr || entry->cancelPending) {
                break;
            }
            if (entry->repeat.isComplete(entry->delivered)) {
                // A finished timer retires itself. Leaving it live would grow
                // activeCount for the life of the scene and eventually exhaust
                // timerCapacity with timers that can never fire again.
                entry->cancelPending = true;
                break;
            }
            if (period.count() <= 0.0) {
                break;
            }
        }

        entry = impl.find(timer);
        if (entry == nullptr || entry->cancelPending) {
            continue;
        }
        // Whatever the catch-up bound refused. Dropped and counted rather than
        // carried; see measureBacklog.
        const Backlog backlog = measureBacklog(entry->elapsed, entry->nextInterval);
        entry->elapsed = backlog.remainder;
        impl.stats.discardedCatchUpSteps += backlog.discardedSteps;
    }

    return Core::success();
}

Core::usize Scheduler::activeCount() const noexcept
{
    if (m_impl == nullptr) {
        return 0;
    }
    Core::usize count = 0;
    for (const TimerId timer : m_impl->liveTimers) {
        const Impl::Entry* const entry = m_impl->find(timer);
        if (entry != nullptr && !entry->cancelPending) {
            ++count;
        }
    }
    return count;
}

SchedulerStats Scheduler::stats() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    SchedulerStats snapshot = m_impl->stats;
    snapshot.timerCapacity = m_impl->config.timerCapacity;
    snapshot.activeTimerCount = activeCount();
    return snapshot;
}

} // namespace Tina::Gameplay
