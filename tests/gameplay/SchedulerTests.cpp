#include <tina/gameplay/Scheduler.hpp>

#include <tina/gameplay/GameplayErrors.hpp>

#include <gtest/gtest.h>

#include <exception>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <vector>

namespace Tina::Gameplay {
namespace {

class CountingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] Core::usize allocationCount() const noexcept { return m_allocationCount; }
    void resetCount() noexcept { m_allocationCount = 0; }

private:
    void* do_allocate(Core::usize bytes, Core::usize alignment) override
    {
        ++m_allocationCount;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, Core::usize bytes, Core::usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    Core::usize m_allocationCount = 0;
};

constexpr Core::Duration seconds(double value) noexcept
{
    return Core::Duration{value};
}

[[nodiscard]] Scheduler makeScheduler(SchedulerConfig config = {})
{
    Core::Result<Scheduler> scheduler = Scheduler::Create(config);
    EXPECT_TRUE(scheduler.has_value());
    return std::move(*scheduler);
}

} // namespace

TEST(SchedulerTests, CreateRejectsZeroCapacityAndZeroCatchUpBound)
{
    Core::Result<Scheduler> noCapacity = Scheduler::Create(SchedulerConfig{.timerCapacity = 0});
    ASSERT_FALSE(noCapacity.has_value());
    EXPECT_EQ(noCapacity.error().code, GameplayErrorCode::InvalidConfiguration);

    // A zero catch-up bound would mean no timer can ever be delivered, which reads
    // as "the scheduler silently does nothing" rather than as a configuration error.
    Core::Result<Scheduler> noCatchUp =
        Scheduler::Create(SchedulerConfig{.maximumCatchUpStepsPerAdvance = 0});
    ASSERT_FALSE(noCatchUp.has_value());
    EXPECT_EQ(noCatchUp.error().code, GameplayErrorCode::InvalidConfiguration);
}

TEST(SchedulerTests, ScheduleRejectsAnEmptyCallback)
{
    Scheduler scheduler = makeScheduler();
    Core::Result<TimerId> timer = scheduler.schedule(TimerDesc{.interval = seconds(1.0)});
    ASSERT_FALSE(timer.has_value());
    EXPECT_EQ(timer.error().code, GameplayErrorCode::MissingCallback);
    EXPECT_EQ(scheduler.activeCount(), 0U);
}

TEST(SchedulerTests, ScheduleRejectsNonFiniteAndNegativeDurations)
{
    Scheduler scheduler = makeScheduler();
    constexpr double quietNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double infinity = std::numeric_limits<double>::infinity();

    for (const double bad : {-1.0, quietNaN, infinity}) {
        Core::Result<TimerId> byInterval = scheduler.schedule(TimerDesc{
            .interval = seconds(bad),
            .callback = [](const TimerEvent&) {},
        });
        ASSERT_FALSE(byInterval.has_value()) << "interval " << bad;
        EXPECT_EQ(byInterval.error().code, GameplayErrorCode::InvalidArgument);

        Core::Result<TimerId> byDelay = scheduler.schedule(TimerDesc{
            .interval = seconds(1.0),
            .initialDelay = seconds(bad),
            .callback = [](const TimerEvent&) {},
        });
        ASSERT_FALSE(byDelay.has_value()) << "initialDelay " << bad;
        EXPECT_EQ(byDelay.error().code, GameplayErrorCode::InvalidArgument);
    }
    EXPECT_EQ(scheduler.activeCount(), 0U);
}

// Repeat{count = 0, infinite = false} is rejected rather than read as "forever". A
// count computed from content that came out empty and a deliberate endless timer
// behave nothing alike, and overloading zero makes them indistinguishable.
TEST(SchedulerTests, ScheduleRejectsAZeroRepeatCountInsteadOfReadingItAsForever)
{
    Scheduler scheduler = makeScheduler();
    Core::Result<TimerId> timer = scheduler.schedule(TimerDesc{
        .interval = seconds(1.0),
        .repeat = Repeat{.count = 0, .infinite = false},
        .callback = [](const TimerEvent&) {},
    });
    ASSERT_FALSE(timer.has_value());
    EXPECT_EQ(timer.error().code, GameplayErrorCode::InvalidArgument);
    EXPECT_EQ(scheduler.activeCount(), 0U);

    // The same struct with infinite set is accepted, which is the distinction the
    // rejection above exists to preserve.
    Core::Result<TimerId> forever = scheduler.schedule(TimerDesc{
        .interval = seconds(1.0),
        .repeat = Repeat::forever(),
        .callback = [](const TimerEvent&) {},
    });
    EXPECT_TRUE(forever.has_value());
}

TEST(SchedulerTests, ScheduleFailsClosedAtCapacity)
{
    Scheduler scheduler = makeScheduler(SchedulerConfig{.timerCapacity = 2});
    EXPECT_TRUE(scheduler.scheduleEvery(seconds(1.0), [](const TimerEvent&) {}).has_value());
    EXPECT_TRUE(scheduler.scheduleEvery(seconds(1.0), [](const TimerEvent&) {}).has_value());

    Core::Result<TimerId> overflow =
        scheduler.scheduleEvery(seconds(1.0), [](const TimerEvent&) {});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, GameplayErrorCode::CapacityExceeded);
    EXPECT_EQ(scheduler.activeCount(), 2U);
    EXPECT_EQ(scheduler.stats().timerCapacity, 2U);
}

// The sub-period leftover is kept rather than zeroed. Every value here is a multiple
// of 1/16 so the arithmetic is exact in binary and the assertion is about the rule
// rather than about rounding.
TEST(SchedulerTests, LeftoverBelowOnePeriodIsCarriedRatherThanZeroed)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    Core::Result<TimerId> timer = scheduler.schedule(TimerDesc{
        .interval = seconds(0.25),
        .repeat = Repeat::forever(),
        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
    });
    ASSERT_TRUE(timer.has_value());

    EXPECT_TRUE(scheduler.advance(seconds(0.1875)).has_value());
    EXPECT_EQ(deliveries, 0);
    EXPECT_TRUE(scheduler.advance(seconds(0.1875)).has_value());
    EXPECT_EQ(deliveries, 1);

    // 0.375 elapsed minus one 0.25 period leaves 0.125, so 0.125 is still owed before
    // the next delivery. Zeroing the accumulator instead would report a full 0.25 and
    // lose a rounding's worth of time on every period.
    Core::Result<Core::Duration> remaining = scheduler.remaining(*timer);
    ASSERT_TRUE(remaining.has_value());
    EXPECT_DOUBLE_EQ(remaining->count(), 0.125);
}

// The consequence of carrying, stated as a count. A 0.25 s timer advanced in 0.1875 s
// steps owes three deliveries per four advances; an implementation that reset the
// accumulator would need two advances per delivery and produce eight instead of
// twelve. This is the drift that hand-written accumulators exhibit.
TEST(SchedulerTests, CarryingLeftoverKeepsAPeriodFromDrifting)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.25),
                        .repeat = Repeat::forever(),
                        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
                    })
                    .has_value());

    for (int step = 0; step < 16; ++step) {
        ASSERT_TRUE(scheduler.advance(seconds(0.1875)).has_value());
    }

    // 16 * 0.1875 == 3.0 seconds of gameplay time, which is twelve 0.25 s periods.
    EXPECT_EQ(deliveries, 12);
    EXPECT_EQ(scheduler.stats().discardedCatchUpSteps, 0U);
}

// Whole periods beyond the catch-up bound are dropped and counted, not carried.
// Carrying them would make one hitch produce a full bound of deliveries on every
// following advance until the backlog drained, turning one stall into a train.
TEST(SchedulerTests, BacklogBeyondTheCatchUpBoundIsDiscardedAndCounted)
{
    Scheduler scheduler = makeScheduler(SchedulerConfig{.maximumCatchUpStepsPerAdvance = 8});
    int deliveries = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.125),
                        .repeat = Repeat::forever(),
                        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
                    })
                    .has_value());

    // Two seconds owes sixteen deliveries at 0.125 s. Eight are made and eight are
    // refused.
    ASSERT_TRUE(scheduler.advance(seconds(2.0)).has_value());
    EXPECT_EQ(deliveries, 8);
    EXPECT_EQ(scheduler.stats().deliveredCount, 8U);
    EXPECT_EQ(scheduler.stats().discardedCatchUpSteps, 8U);

    // The timer resynced to now rather than keeping a backlog: the next ordinary
    // advance delivers once, not eight more times.
    ASSERT_TRUE(scheduler.advance(seconds(0.125)).has_value());
    EXPECT_EQ(deliveries, 9);
    EXPECT_EQ(scheduler.stats().discardedCatchUpSteps, 8U);
}

// A zero interval cannot be subdivided, so it means "once per advance" rather than
// spinning to the catch-up bound. Without this rule a zero-interval timer would fire
// maximumCatchUpStepsPerAdvance times per frame and read as a runaway callback.
TEST(SchedulerTests, AZeroIntervalDeliversExactlyOncePerAdvanceRegardlessOfDelta)
{
    Scheduler scheduler = makeScheduler(SchedulerConfig{.maximumCatchUpStepsPerAdvance = 8});
    int deliveries = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.0),
                        .repeat = Repeat::forever(),
                        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
                    })
                    .has_value());

    ASSERT_TRUE(scheduler.advance(seconds(10.0)).has_value());
    EXPECT_EQ(deliveries, 1);
    ASSERT_TRUE(scheduler.advance(seconds(0.0)).has_value());
    EXPECT_EQ(deliveries, 2);
    EXPECT_EQ(scheduler.stats().discardedCatchUpSteps, 0U);
}

// An absent initialDelay means one whole interval, which is the ordinary periodic
// timer.
TEST(SchedulerTests, AnAbsentInitialDelayMeansOneWholeInterval)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    Core::Result<TimerId> timer = scheduler.schedule(TimerDesc{
        .interval = seconds(0.5),
        .repeat = Repeat::forever(),
        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
    });
    ASSERT_TRUE(timer.has_value());

    Core::Result<Core::Duration> beforeAnyAdvance = scheduler.remaining(*timer);
    ASSERT_TRUE(beforeAnyAdvance.has_value());
    EXPECT_DOUBLE_EQ(beforeAnyAdvance->count(), 0.5);

    EXPECT_TRUE(scheduler.advance(seconds(0.25)).has_value());
    EXPECT_EQ(deliveries, 0);
    EXPECT_TRUE(scheduler.advance(seconds(0.25)).has_value());
    EXPECT_EQ(deliveries, 1);
}

// A present zero initialDelay means "fire on the next advance", which is a different
// request from an absent one. This pair is the reason the field is an optional rather
// than a duration where zero means unset.
TEST(SchedulerTests, AZeroInitialDelayFiresOnTheNextAdvanceUnlikeAnAbsentOne)
{
    Scheduler scheduler = makeScheduler();
    int immediate = 0;
    int deferred = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(1.0),
                        .initialDelay = seconds(0.0),
                        .repeat = Repeat::forever(),
                        .callback = [&immediate](const TimerEvent&) { ++immediate; },
                    })
                    .has_value());
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(1.0),
                        .repeat = Repeat::forever(),
                        .callback = [&deferred](const TimerEvent&) { ++deferred; },
                    })
                    .has_value());

    // Even a zero-length advance delivers the zero-delay timer, because its first
    // period has already elapsed.
    ASSERT_TRUE(scheduler.advance(seconds(0.0)).has_value());
    EXPECT_EQ(immediate, 1);
    EXPECT_EQ(deferred, 0);
}

// A first delay shorter than the interval fires early and then the timer settles onto
// its own interval, so a one-off delay never becomes the period.
TEST(SchedulerTests, AShortFirstDelayDoesNotBecomeThePeriod)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(1.0),
                        .initialDelay = seconds(0.25),
                        .repeat = Repeat::forever(),
                        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
                    })
                    .has_value());

    ASSERT_TRUE(scheduler.advance(seconds(0.25)).has_value());
    EXPECT_EQ(deliveries, 1);

    // Another 0.25 would fire again if the delay had been kept as the period.
    ASSERT_TRUE(scheduler.advance(seconds(0.25)).has_value());
    EXPECT_EQ(deliveries, 1);
    ASSERT_TRUE(scheduler.advance(seconds(0.75)).has_value());
    EXPECT_EQ(deliveries, 2);
}

// A finite repeat delivers exactly its count and then retires itself. Leaving finished
// timers live would grow activeCount for the life of the scene and eventually exhaust
// timerCapacity with timers that can never fire again.
TEST(SchedulerTests, AFiniteRepeatDeliversItsCountAndThenRetiresItself)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    Core::u32 lastIteration = 0;
    Core::Result<TimerId> timer = scheduler.schedule(TimerDesc{
        .interval = seconds(0.25),
        .repeat = Repeat::times(3),
        .callback = [&](const TimerEvent& event) {
            ++deliveries;
            lastIteration = event.iteration;
        },
    });
    ASSERT_TRUE(timer.has_value());

    // One second owes four periods, but the repeat count stops it at three.
    ASSERT_TRUE(scheduler.advance(seconds(1.0)).has_value());
    EXPECT_EQ(deliveries, 3);
    // 1-based, so a repeating callback can tell "first tick" from "steady state"
    // without keeping its own counter.
    EXPECT_EQ(lastIteration, 3U);

    EXPECT_FALSE(scheduler.isActive(*timer));
    EXPECT_EQ(scheduler.activeCount(), 0U);
    EXPECT_EQ(scheduler.stats().deliveredCount, 3U);
    // Self-retirement is not a cancellation: keeping the counters apart is what lets
    // "the game cancelled this" be told from "this finished".
    EXPECT_EQ(scheduler.stats().cancelledCount, 0U);

    // The handle is stale afterwards rather than silently reusable.
    EXPECT_EQ(scheduler.remaining(*timer).error().code, GameplayErrorCode::InvalidHandle);
}

// Retiring destroys the callback, so whatever it captured is released. A scheduler that
// kept finished callbacks alive would hold scene objects past their scene.
TEST(SchedulerTests, RetiringATimerReleasesWhatItsCallbackCaptured)
{
    Scheduler scheduler = makeScheduler();
    std::weak_ptr<int> observer;
    {
        auto captured = std::make_shared<int>(7);
        observer = captured;
        ASSERT_TRUE(scheduler
                        .schedule(TimerDesc{
                            .interval = seconds(0.5),
                            .repeat = Repeat::once(),
                            .callback = [captured = std::move(captured)](const TimerEvent&) {},
                        })
                        .has_value());
    }
    EXPECT_FALSE(observer.expired());

    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_TRUE(observer.expired());
}

// cancelAll() outside dispatch releases everything immediately.
TEST(SchedulerTests, CancelAllReleasesEveryCallback)
{
    Scheduler scheduler = makeScheduler();
    std::weak_ptr<int> observer;
    {
        auto captured = std::make_shared<int>(1);
        observer = captured;
        ASSERT_TRUE(scheduler
                        .schedule(TimerDesc{
                            .interval = seconds(1.0),
                            .repeat = Repeat::forever(),
                            .callback = [captured = std::move(captured)](const TimerEvent&) {},
                        })
                        .has_value());
    }

    scheduler.cancelAll();
    EXPECT_EQ(scheduler.activeCount(), 0U);
    EXPECT_EQ(scheduler.stats().cancelledCount, 1U);
    EXPECT_TRUE(observer.expired());
}

TEST(SchedulerTests, CancelRejectsAStaleOrDoubleCancelledHandle)
{
    Scheduler scheduler = makeScheduler();
    Core::Result<TimerId> timer =
        scheduler.scheduleEvery(seconds(1.0), [](const TimerEvent&) {});
    ASSERT_TRUE(timer.has_value());

    EXPECT_TRUE(scheduler.cancel(*timer).has_value());
    // A double cancel usually means two owners each believe they hold the timer, so it
    // is reported rather than absorbed.
    Core::Status again = scheduler.cancel(*timer);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, GameplayErrorCode::InvalidHandle);
    EXPECT_EQ(scheduler.setPaused(*timer, true).error().code, GameplayErrorCode::InvalidHandle);
    EXPECT_FALSE(scheduler.isPaused(*timer).has_value());
}

// A paused timer does not accumulate time, so pausing does not merely defer a delivery
// that then arrives all at once on resume.
TEST(SchedulerTests, APausedTimerAccumulatesNoTime)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    Core::Result<TimerId> timer = scheduler.schedule(TimerDesc{
        .interval = seconds(0.5),
        .repeat = Repeat::forever(),
        .startPaused = true,
        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
    });
    ASSERT_TRUE(timer.has_value());

    Core::Result<bool> paused = scheduler.isPaused(*timer);
    ASSERT_TRUE(paused.has_value());
    EXPECT_TRUE(*paused);

    ASSERT_TRUE(scheduler.advance(seconds(10.0)).has_value());
    EXPECT_EQ(deliveries, 0);
    // Ten seconds while paused did not bank twenty periods.
    Core::Result<Core::Duration> remaining = scheduler.remaining(*timer);
    ASSERT_TRUE(remaining.has_value());
    EXPECT_DOUBLE_EQ(remaining->count(), 0.5);

    ASSERT_TRUE(scheduler.setPaused(*timer, false).has_value());
    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(deliveries, 1);
    EXPECT_EQ(scheduler.stats().discardedCatchUpSteps, 0U);
}

// Zero is a valid time scale and means a full pause, not an error and not a division
// by zero somewhere in the accumulator.
TEST(SchedulerTests, TimeScaleScalesEveryDeltaAndZeroIsAFullPause)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.5),
                        .repeat = Repeat::forever(),
                        .callback = [&deliveries](const TimerEvent&) { ++deliveries; },
                    })
                    .has_value());

    ASSERT_TRUE(scheduler.setTimeScale(0.5).has_value());
    EXPECT_DOUBLE_EQ(scheduler.timeScale(), 0.5);
    // Half speed: half a second of real time is a quarter of gameplay time.
    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(deliveries, 0);
    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(deliveries, 1);

    ASSERT_TRUE(scheduler.setTimeScale(0.0).has_value());
    ASSERT_TRUE(scheduler.advance(seconds(100.0)).has_value());
    EXPECT_EQ(deliveries, 1);
}

// Running timers backwards would need a rewind contract nothing here implements, and
// accepting -1 silently would simply stall every timer forever.
TEST(SchedulerTests, TimeScaleRejectsNegativeAndNonFiniteValues)
{
    Scheduler scheduler = makeScheduler();
    constexpr double quietNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double infinity = std::numeric_limits<double>::infinity();

    for (const double bad : {-1.0, quietNaN, infinity}) {
        Core::Status status = scheduler.setTimeScale(bad);
        ASSERT_FALSE(status.has_value()) << "scale " << bad;
        EXPECT_EQ(status.error().code, GameplayErrorCode::InvalidArgument);
    }
    // Rejected values leave the previous scale in place rather than half-applying.
    EXPECT_DOUBLE_EQ(scheduler.timeScale(), 1.0);
}

// A pause-menu animation and an "unstick the player" watchdog must keep running when
// the gameplay scale is zero, which is exactly when they are needed.
TEST(SchedulerTests, ATimerCanOptOutOfTheTimeScale)
{
    Scheduler scheduler = makeScheduler();
    int scaled = 0;
    int unscaled = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.5),
                        .repeat = Repeat::forever(),
                        .callback = [&scaled](const TimerEvent&) { ++scaled; },
                    })
                    .has_value());
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.5),
                        .repeat = Repeat::forever(),
                        .ignoresTimeScale = true,
                        .callback = [&unscaled](const TimerEvent&) { ++unscaled; },
                    })
                    .has_value());

    ASSERT_TRUE(scheduler.setTimeScale(0.0).has_value());
    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(scaled, 0);
    EXPECT_EQ(unscaled, 1);
}

TEST(SchedulerTests, AdvanceRejectsNonFiniteAndNegativeDeltas)
{
    Scheduler scheduler = makeScheduler();
    constexpr double quietNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double infinity = std::numeric_limits<double>::infinity();

    for (const double bad : {-1.0, quietNaN, infinity}) {
        Core::Status status = scheduler.advance(seconds(bad));
        ASSERT_FALSE(status.has_value()) << "delta " << bad;
        EXPECT_EQ(status.error().code, GameplayErrorCode::InvalidArgument);
    }
    // A rejected advance is not counted, so advanceCount stays a count of real frames.
    EXPECT_EQ(scheduler.stats().advanceCount, 0U);
}

// Re-entering advance() from a callback is refused rather than recursed into. Allowing
// it would make delivery order depend on how deeply the callbacks happened to nest.
TEST(SchedulerTests, AdvanceRefusesToBeReenteredFromACallback)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    Core::ErrorCode innerCode{};
    bool innerAttempted = false;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.5),
                        .repeat = Repeat::forever(),
                        .callback =
                            [&](const TimerEvent&) {
                                ++deliveries;
                                if (!innerAttempted) {
                                    innerAttempted = true;
                                    Core::Status inner = scheduler.advance(seconds(0.5));
                                    ASSERT_FALSE(inner.has_value());
                                    innerCode = inner.error().code;
                                }
                            },
                    })
                    .has_value());

    // The outer advance still succeeds: the inner call changed nothing.
    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_TRUE(innerAttempted);
    EXPECT_EQ(innerCode, GameplayErrorCode::ReentrantDispatch);
    EXPECT_EQ(deliveries, 1);

    // Dispatch was left clean, so the next ordinary advance works.
    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(deliveries, 2);
}

// A callback is game code and may throw. The dispatch flag is restored by a scope guard
// rather than at the end of the function, because a scheduler left permanently
// "dispatching" would refuse every later advance for the rest of the process.
TEST(SchedulerTests, AThrowingCallbackDoesNotLeaveTheSchedulerWedged)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.5),
                        .repeat = Repeat::forever(),
                        .callback =
                            [&deliveries](const TimerEvent&) {
                                ++deliveries;
                                if (deliveries == 1) {
                                    throw std::runtime_error("callback failed");
                                }
                            },
                    })
                    .has_value());

    EXPECT_THROW(static_cast<void>(scheduler.advance(seconds(0.5))), std::runtime_error);
    EXPECT_EQ(deliveries, 1);

    // Not ReentrantDispatch: the guard cleared the flag while the exception unwound.
    Core::Status afterThrow = scheduler.advance(seconds(0.5));
    ASSERT_TRUE(afterThrow.has_value());
    EXPECT_EQ(deliveries, 2);
}

// Cancelling from inside the timer's own callback is safe. The entry cannot be retired
// there -- that would destroy the callback currently running -- so it is marked and
// reclaimed after the dispatch loop.
TEST(SchedulerTests, ATimerMayCancelItselfFromItsOwnCallback)
{
    Scheduler scheduler = makeScheduler();
    int deliveries = 0;
    TimerId self{};
    Core::Result<TimerId> timer = scheduler.schedule(TimerDesc{
        .interval = seconds(0.25),
        .repeat = Repeat::forever(),
        .callback =
            [&](const TimerEvent& event) {
                ++deliveries;
                EXPECT_EQ(event.timer, self);
                EXPECT_TRUE(scheduler.cancel(event.timer).has_value());
            },
    });
    ASSERT_TRUE(timer.has_value());
    self = *timer;

    // One second owes four deliveries, but the first one cancels the timer, so the
    // catch-up loop stops instead of running a destroyed callback three more times.
    ASSERT_TRUE(scheduler.advance(seconds(1.0)).has_value());
    EXPECT_EQ(deliveries, 1);
    EXPECT_FALSE(scheduler.isActive(self));
    EXPECT_EQ(scheduler.activeCount(), 0U);
}

// A callback may cancel a *different* timer that this same advance has not visited yet.
// That timer is skipped rather than delivered to after its owner gave it up, which is
// the case a hand-rolled list of callbacks gets wrong by iterating a mutated container.
TEST(SchedulerTests, ACallbackMayCancelAnotherTimerBeforeItIsVisited)
{
    Scheduler scheduler = makeScheduler();
    TimerId victim{};
    int victimDeliveries = 0;
    int cancellerDeliveries = 0;

    // Scheduled first, so it is visited first and its cancel lands before the victim's
    // turn comes up. Iteration order is scheduling order.
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.5),
                        .repeat = Repeat::forever(),
                        .callback =
                            [&](const TimerEvent&) {
                                ++cancellerDeliveries;
                                if (scheduler.isActive(victim)) {
                                    EXPECT_TRUE(scheduler.cancel(victim).has_value());
                                }
                            },
                    })
                    .has_value());

    Core::Result<TimerId> scheduled = scheduler.schedule(TimerDesc{
        .interval = seconds(0.5),
        .repeat = Repeat::forever(),
        .callback = [&victimDeliveries](const TimerEvent&) { ++victimDeliveries; },
    });
    ASSERT_TRUE(scheduled.has_value());
    victim = *scheduled;

    // Both are due, but the victim is cancelled during the first callback and so is
    // never delivered to at all.
    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(cancellerDeliveries, 1);
    EXPECT_EQ(victimDeliveries, 0);
    EXPECT_FALSE(scheduler.isActive(victim));
    EXPECT_EQ(scheduler.activeCount(), 1U);

    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(cancellerDeliveries, 2);
    EXPECT_EQ(victimDeliveries, 0);
}

// A timer scheduled from inside a callback first runs on the *next* advance, so delivery
// order never depends on how deeply the callbacks nested.
TEST(SchedulerTests, ATimerScheduledFromACallbackFirstRunsOnTheNextAdvance)
{
    Scheduler scheduler = makeScheduler();
    int nested = 0;
    bool scheduledOnce = false;
    ASSERT_TRUE(scheduler
                    .schedule(TimerDesc{
                        .interval = seconds(0.5),
                        .repeat = Repeat::forever(),
                        .callback =
                            [&](const TimerEvent&) {
                                if (scheduledOnce) {
                                    return;
                                }
                                scheduledOnce = true;
                                // Zero initial delay: it is due immediately and would
                                // fire within this advance if it were armed for it.
                                EXPECT_TRUE(scheduler
                                                .schedule(TimerDesc{
                                                    .interval = seconds(0.5),
                                                    .initialDelay = seconds(0.0),
                                                    .repeat = Repeat::forever(),
                                                    .callback = [&nested](const TimerEvent&) {
                                                        ++nested;
                                                    },
                                                })
                                                .has_value());
                            },
                    })
                    .has_value());

    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(nested, 0);
    EXPECT_EQ(scheduler.activeCount(), 2U);

    ASSERT_TRUE(scheduler.advance(seconds(0.0)).has_value());
    EXPECT_EQ(nested, 1);
}

// Iteration order is scheduling order. The pool resolves ids but has no stable
// traversal, and "timers fire in the order they were scheduled" is the only order a
// game can reason about.
TEST(SchedulerTests, DeliveryOrderFollowsSchedulingOrder)
{
    Scheduler scheduler = makeScheduler();
    std::vector<int> order;
    for (int index = 0; index < 4; ++index) {
        ASSERT_TRUE(scheduler
                        .schedule(TimerDesc{
                            .interval = seconds(0.5),
                            .repeat = Repeat::forever(),
                            .callback = [&order, index](const TimerEvent&) {
                                order.push_back(index);
                            },
                        })
                        .has_value());
    }

    ASSERT_TRUE(scheduler.advance(seconds(0.5)).has_value());
    EXPECT_EQ(order, std::vector<int>({0, 1, 2, 3}));
}

// Storage is taken once at Create. Scheduling, dispatching and cancelling afterwards
// must not reach the allocator, which is the whole reason the capacity is fixed.
TEST(SchedulerTests, NothingAllocatesAfterCreate)
{
    CountingMemoryResource resource;
    Scheduler scheduler =
        makeScheduler(SchedulerConfig{.timerCapacity = 8, .memoryResource = &resource});
    EXPECT_GT(resource.allocationCount(), 0U);
    resource.resetCount();

    std::vector<TimerId> timers;
    for (int index = 0; index < 8; ++index) {
        Core::Result<TimerId> timer =
            scheduler.scheduleEvery(seconds(0.25), [](const TimerEvent&) {});
        ASSERT_TRUE(timer.has_value());
        timers.push_back(*timer);
    }
    EXPECT_EQ(resource.allocationCount(), 0U);

    ASSERT_TRUE(scheduler.advance(seconds(1.0)).has_value());
    EXPECT_EQ(resource.allocationCount(), 0U);

    for (const TimerId timer : timers) {
        EXPECT_TRUE(scheduler.cancel(timer).has_value());
    }
    EXPECT_EQ(resource.allocationCount(), 0U);
}

// The high-water mark keeps its peak after timers retire, which is what makes it usable
// for sizing a capacity; activeTimerCount is the live figure.
TEST(SchedulerTests, StatsSeparateTheLiveCountFromThePeak)
{
    Scheduler scheduler = makeScheduler(SchedulerConfig{.timerCapacity = 16});
    std::vector<TimerId> timers;
    for (int index = 0; index < 5; ++index) {
        Core::Result<TimerId> timer =
            scheduler.scheduleEvery(seconds(1.0), [](const TimerEvent&) {});
        ASSERT_TRUE(timer.has_value());
        timers.push_back(*timer);
    }
    EXPECT_EQ(scheduler.stats().activeTimerCount, 5U);
    EXPECT_EQ(scheduler.stats().activeTimerHighWater, 5U);

    for (const TimerId timer : timers) {
        EXPECT_TRUE(scheduler.cancel(timer).has_value());
    }
    EXPECT_EQ(scheduler.stats().activeTimerCount, 0U);
    EXPECT_EQ(scheduler.stats().activeTimerHighWater, 5U);
    EXPECT_EQ(scheduler.stats().cancelledCount, 5U);
}

// A moved-from Scheduler is inert rather than crashing: every entry point reports
// InvalidConfiguration instead of dereferencing a null implementation.
TEST(SchedulerTests, AMovedFromSchedulerIsInertRatherThanUndefined)
{
    Scheduler source = makeScheduler();
    Core::Result<TimerId> timer =
        source.scheduleEvery(seconds(0.5), [](const TimerEvent&) {});
    ASSERT_TRUE(timer.has_value());

    Scheduler moved = std::move(source);
    EXPECT_EQ(moved.activeCount(), 1U);

    EXPECT_EQ(source.advance(seconds(0.5)).error().code,
              GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(source.scheduleEvery(seconds(0.5), [](const TimerEvent&) {}).error().code,
              GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(source.cancel(*timer).error().code, GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(source.setTimeScale(2.0).error().code, GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(source.activeCount(), 0U);
    EXPECT_FALSE(source.isActive(*timer));
    EXPECT_EQ(source.stats().timerCapacity, 0U);
    // cancelAll on a moved-from scheduler is a no-op, not a null dereference.
    source.cancelAll();
}

} // namespace Tina::Gameplay
