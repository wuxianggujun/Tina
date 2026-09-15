#include <tina/gameplay/Scheduler.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/gameplay/GameplayErrors.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
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
    Core::Duration remainder{std::fmod(elapsed.count(), period.count())};
    if (!std::isfinite(remainder.count()) || remainder.count() < 0.0) {
        remainder = Core::Duration{0.0};
    }
    // Saturated rather than wrapped: a tiny period under a large accumulated delta
    // exceeds u64, and a wrapped count would read as a small, plausible backlog.
    // u64::max rounds up to 2^64 in double; casting that rounded value is UB.
    constexpr Core::u64 maximum = (std::numeric_limits<Core::u64>::max)();
    const Core::u64 discarded = owed >= static_cast<double>(maximum)
        ? maximum : static_cast<Core::u64>(owed);
    return Backlog{.discardedSteps = discarded, .remainder = remainder};
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
        TimerId nextCancelled{};
        // Advance sequence this timer becomes eligible at. A timer scheduled from
        // inside a callback first runs on the next advance, so delivery order does
        // not depend on how deeply the callbacks nested.
        Core::u64 armedAtAdvance = 0;
    };

    using TimerPool = Core::GenerationPool<Entry, Detail::SchedulerTimerTag>;

    Impl(const SchedulerConfig& configuration, std::pmr::memory_resource& resource,
         TimerPool&& timerPool)
        : config(configuration), memory(&resource), timers(std::move(timerPool)),
          liveTimers(Core::usize{0}, std::pmr::polymorphic_allocator<TimerId>{&resource})
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
    bool reclaiming = false;
    TimerId cancelledHead{};
    Core::usize activeTimers = 0;
    SchedulerStats stats{};

    [[nodiscard]] Entry* find(TimerId timer) noexcept { return timers.tryGet(timer); }
    [[nodiscard]] const Entry* find(TimerId timer) const noexcept { return timers.tryGet(timer); }

    void markCancelled(TimerId timer, Entry& entry) noexcept
    {
        if (!entry.cancelPending) {
            entry.cancelPending = true;
            --activeTimers;
            entry.nextCancelled = cancelledHead;
            cancelledHead = timer;
        }
    }

    void reclaimCancelled() noexcept
    {
        reclaiming = true;
        while (cancelledHead) {
            const TimerId timer = cancelledHead;
            cancelledHead = find(timer)->nextCancelled;
            (void)timers.erase(timer);
        }
        std::erase_if(liveTimers, [this](TimerId timer) { return !timers.contains(timer); });
        reclaiming = false;
    }
};

Scheduler::Scheduler(Impl* impl) noexcept : m_impl(impl) {}

Scheduler::~Scheduler() noexcept
{
    if (m_impl && (m_impl->dispatching || m_impl->reclaiming)) { std::terminate(); }
    delete std::exchange(m_impl, nullptr);
}

Scheduler::Scheduler(Scheduler&& other) noexcept
{
    if (other.m_impl && (other.m_impl->dispatching || other.m_impl->reclaiming)) { std::terminate(); }
    m_impl = std::exchange(other.m_impl, nullptr);
}

Scheduler& Scheduler::operator=(Scheduler&& other) noexcept
{
    if (this != &other) {
        if ((m_impl && (m_impl->dispatching || m_impl->reclaiming)) ||
            (other.m_impl && (other.m_impl->dispatching || other.m_impl->reclaiming))) { std::terminate(); }
        delete std::exchange(m_impl, std::exchange(other.m_impl, nullptr));
    }
    return *this;
}

Core::Result<Scheduler> Scheduler::Create(SchedulerConfig config)
{
    if (config.maximumCatchUpStepsPerAdvance == 0) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "Scheduler maximumCatchUpStepsPerAdvance must be greater than zero");
    }

    std::pmr::memory_resource& resource = config.memoryResource != nullptr
        ? *config.memoryResource
        : *std::pmr::get_default_resource();

    auto timers = Impl::TimerPool::Create(config.initialTimerReserve, resource);
    if (!timers) {
        return Core::failure(std::move(timers.error()).withContext("Scheduler::Create", "timer slots"));
    }

    try {
        auto impl = std::make_unique<Impl>(config, resource, std::move(*timers));
        impl->liveTimers.reserve(config.initialTimerReserve);
        return Scheduler(impl.release());
    } catch (const std::bad_alloc&) {
        return Core::failure(GameplayErrorCode::AllocationFailed,
                             "Scheduler storage allocation failed");
    } catch (const std::length_error&) {
        return Core::failure(GameplayErrorCode::CapacityExceeded,
                             "Scheduler storage exceeds addressable vector size");
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
    // Reserve all bookkeeping before publishing the timer. Slot growth never
    // moves an Entry, including one whose callback scheduled this new timer.
    if (m_impl->timers.availableCount() == 0) {
        if (m_impl->timers.capacity() == TimerId::InvalidIndex) {
            return Core::failure(GameplayErrorCode::CapacityExceeded, "TimerId index space is exhausted");
        }
        if (auto status = m_impl->timers.reserve(m_impl->timers.capacity() + 1); !status) {
            return Core::failure(std::move(status.error()));
        }
    }
    try {
        // During reclamation a capture destructor may schedule again before old
        // ids are compacted. Reserve for those temporary tombstones as well.
        auto& order = m_impl->liveTimers;
        if (order.size() == order.max_size()) {
            return Core::failure(GameplayErrorCode::CapacityExceeded, "Scheduler order storage is exhausted");
        }
        const Core::usize required = (std::max)(m_impl->timers.capacity(), order.size() + 1);
        if (required > order.capacity()) {
            const Core::usize grown = order.capacity() > order.max_size() / 2
                ? order.max_size() : order.capacity() * 2;
            order.reserve((std::max)(required, grown));
        }
    } catch (const std::bad_alloc&) {
        return Core::failure(GameplayErrorCode::AllocationFailed, "Scheduler order storage allocation failed");
    } catch (const std::length_error&) {
        return Core::failure(GameplayErrorCode::CapacityExceeded, "Scheduler order storage exceeds addressable size");
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
        return Core::failure(std::move(timer.error()));
    }
    m_impl->liveTimers.push_back(*timer);
    ++m_impl->activeTimers;
    m_impl->stats.activeTimerHighWater =
        (std::max)(m_impl->stats.activeTimerHighWater, m_impl->activeTimers);
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
    m_impl->markCancelled(timer, *entry);
    if (!m_impl->dispatching && !m_impl->reclaiming) {
        m_impl->reclaimCancelled();
    }
    return Core::success();
}

void Scheduler::cancelAll() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    for (const TimerId timer : m_impl->liveTimers) {
        Impl::Entry* const entry = m_impl->find(timer);
        if (entry != nullptr && !entry->cancelPending) {
            m_impl->markCancelled(timer, *entry);
            ++m_impl->stats.cancelledCount;
        }
    }
    if (!m_impl->dispatching && !m_impl->reclaiming) {
        m_impl->reclaimCancelled();
    }
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
    if (m_impl->dispatching || m_impl->reclaiming) {
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
        impl.reclaimCancelled();
        impl.dispatching = false;
        ++impl.advanceSequence;
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
            if (entry == nullptr || entry->cancelPending || entry->paused ||
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
            {
                // A throwing last delivery still completes its timer. Publish the
                // retirement after the callback, including exception unwinding.
                auto finishDelivery = Core::makeScopeExit([&impl, timer]() noexcept {
                    if (auto* current = impl.find(timer); current != nullptr &&
                        current->repeat.isComplete(current->delivered)) {
                        impl.markCancelled(timer, *current);
                    }
                });
                entry->callback(event);
            }

            // Re-resolved after the callback: it may have cancelled this timer, or
            // cancelled and rescheduled into the same pool slot.
            entry = impl.find(timer);
            if (entry == nullptr || entry->cancelPending) {
                break;
            }
            if (period.count() <= 0.0) {
                break;
            }
        }

        entry = impl.find(timer);
        if (entry == nullptr || entry->cancelPending || entry->paused ||
            steps != impl.config.maximumCatchUpStepsPerAdvance) {
            continue;
        }
        // Whatever the catch-up bound refused. Dropped and counted rather than
        // carried; see measureBacklog.
        const Backlog backlog = measureBacklog(entry->elapsed, entry->nextInterval);
        entry->elapsed = backlog.remainder;
        constexpr Core::u64 maximum = (std::numeric_limits<Core::u64>::max)();
        impl.stats.discardedCatchUpSteps = backlog.discardedSteps > maximum - impl.stats.discardedCatchUpSteps
            ? maximum : impl.stats.discardedCatchUpSteps + backlog.discardedSteps;
    }

    return Core::success();
}

Core::usize Scheduler::activeCount() const noexcept
{
    return m_impl != nullptr ? m_impl->activeTimers : 0;
}

SchedulerStats Scheduler::stats() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    SchedulerStats snapshot = m_impl->stats;
    snapshot.reservedTimerSlots = m_impl->timers.capacity();
    snapshot.activeTimerCount = activeCount();
    return snapshot;
}

} // namespace Tina::Gameplay
