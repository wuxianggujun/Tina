#include <tina/gameplay/Action.hpp>

#include <tina/gameplay/GameplayErrors.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <utility>
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

[[nodiscard]] ActionRunner makeRunner(ActionRunnerConfig config = {})
{
    Core::Result<ActionRunner> runner = ActionRunner::Create(config);
    EXPECT_TRUE(runner.has_value());
    return std::move(*runner);
}

} // namespace

TEST(ActionRunnerTests, CreateRejectsZeroCapacityAndZeroRepeatBound)
{
    Core::Result<ActionRunner> noCapacity =
        ActionRunner::Create(ActionRunnerConfig{.actionCapacity = 0});
    ASSERT_FALSE(noCapacity.has_value());
    EXPECT_EQ(noCapacity.error().code, GameplayErrorCode::InvalidConfiguration);

    Core::Result<ActionRunner> noRepeats =
        ActionRunner::Create(ActionRunnerConfig{.maximumRepeatIterationsPerAdvance = 0});
    ASSERT_FALSE(noRepeats.has_value());
    EXPECT_EQ(noRepeats.error().code, GameplayErrorCode::InvalidConfiguration);
}

// The diagnostic names the subexpression that was wrong rather than the play call, which
// is the entire payoff of fail-late authoring.
TEST(ActionRunnerTests, PlayReportsTheAuthoringFailureTheActionRecorded)
{
    ActionRunner runner = makeRunner();
    Core::Result<ActionId> played =
        runner.play(Action::sequence(Action::delay(seconds(1.0)), Action::delay(seconds(-1.0))));
    ASSERT_FALSE(played.has_value());
    EXPECT_EQ(played.error().code, GameplayErrorCode::InvalidArgument);
    EXPECT_EQ(runner.activeCount(), 0U);
    EXPECT_EQ(runner.stats().startedCount, 0U);
}

TEST(ActionRunnerTests, PlayRejectsAnEmptyAction)
{
    ActionRunner runner = makeRunner();
    Core::Result<ActionId> played = runner.play(Action{});
    ASSERT_FALSE(played.has_value());
    EXPECT_EQ(played.error().code, GameplayErrorCode::InvalidSequence);
}

TEST(ActionRunnerTests, PlayConsumesTheAction)
{
    ActionRunner runner = makeRunner();
    Action action = Action::delay(seconds(1.0));
    ASSERT_TRUE(action.hasValue());

    Core::Result<ActionId> played = runner.play(std::move(action));
    ASSERT_TRUE(played.has_value());
    EXPECT_FALSE(action.hasValue());
    EXPECT_TRUE(runner.isPlaying(*played));
}

TEST(ActionRunnerTests, PlayFailsClosedAtCapacity)
{
    ActionRunner runner = makeRunner(ActionRunnerConfig{.actionCapacity = 2});
    EXPECT_TRUE(runner.play(Action::delay(seconds(1.0))).has_value());
    EXPECT_TRUE(runner.play(Action::delay(seconds(1.0))).has_value());

    Core::Result<ActionId> overflow = runner.play(Action::delay(seconds(1.0)));
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, GameplayErrorCode::CapacityExceeded);
    EXPECT_EQ(runner.activeCount(), 2U);
}

// play() does not start the action. Its first node runs at the next advance, which keeps
// every action's first frame identical regardless of where in the frame it was played.
TEST(ActionRunnerTests, PlayDoesNotRunTheFirstNode)
{
    ActionRunner runner = makeRunner();
    int calls = 0;
    ASSERT_TRUE(runner.play(Action::call([&calls]() { ++calls; })).has_value());
    EXPECT_EQ(calls, 0);

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(calls, 1);
}

// A zero duration applies exactly once, at alpha 1. That is what "snap to the end" means,
// and it is why call() can be a tween rather than its own node kind.
TEST(ActionRunnerTests, AZeroDurationTweenAppliesExactlyOnceAtOne)
{
    ActionRunner runner = makeRunner();
    std::vector<float> alphas;
    ASSERT_TRUE(runner
                    .play(Action::tween(seconds(0.0), Easing::Linear,
                                        [&alphas](float alpha) { alphas.push_back(alpha); }))
                    .has_value());

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    ASSERT_EQ(alphas.size(), 1U);
    EXPECT_FLOAT_EQ(alphas.front(), 1.0F);

    // It completed, so a second advance applies nothing further.
    ASSERT_TRUE(runner.advance(seconds(1.0)).has_value());
    EXPECT_EQ(alphas.size(), 1U);
    EXPECT_EQ(runner.activeCount(), 0U);
}

// The final apply is at exactly 1, not at whatever the accumulated elapsed divides to. A
// tween that stops one float epsilon short of its target is the "sprite ends at 199.997"
// defect.
TEST(ActionRunnerTests, ATweenEndsOnItsAuthoredTargetExactly)
{
    ActionRunner runner = makeRunner();
    float value = -1.0F;
    ASSERT_TRUE(runner
                    .play(Action::tweenFloat(seconds(1.0), 0.0F, 200.0F, Easing::Linear,
                                             [&value](float current) { value = current; }))
                    .has_value());

    // Deltas that do not divide the duration evenly, so an implementation reusing the
    // accumulated elapsed for the last apply would land just short.
    for (int step = 0; step < 3; ++step) {
        ASSERT_TRUE(runner.advance(seconds(0.3)).has_value());
    }
    EXPECT_LT(value, 200.0F);

    ASSERT_TRUE(runner.advance(seconds(0.3)).has_value());
    EXPECT_FLOAT_EQ(value, 200.0F);
    EXPECT_EQ(runner.activeCount(), 0U);
    EXPECT_EQ(runner.stats().completedCount, 1U);
}

TEST(ActionRunnerTests, ATweenReportsIntermediateProgressThroughItsCurve)
{
    ActionRunner runner = makeRunner();
    float value = -1.0F;
    ASSERT_TRUE(runner
                    .play(Action::tweenFloat(seconds(1.0), 0.0F, 100.0F, Easing::Linear,
                                             [&value](float current) { value = current; }))
                    .has_value());

    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    EXPECT_FLOAT_EQ(value, 25.0F);
    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    EXPECT_FLOAT_EQ(value, 50.0F);
    EXPECT_TRUE(runner.isPlaying(*runner.play(Action::delay(seconds(1.0)))));
}

// Leftover time carries across a node boundary. Dropping it is why hand-written
// sequences drift by one advance's rounding per step.
TEST(ActionRunnerTests, ASequenceCarriesLeftoverIntoItsNextChild)
{
    ActionRunner runner = makeRunner();
    float first = -1.0F;
    float second = -1.0F;
    ASSERT_TRUE(
        runner
            .play(Action::sequence(
                Action::tweenFloat(seconds(0.25), 0.0F, 1.0F, Easing::Linear,
                                   [&first](float current) { first = current; }),
                Action::tweenFloat(seconds(0.5), 0.0F, 100.0F, Easing::Linear,
                                   [&second](float current) { second = current; })))
            .has_value());

    // 0.375 finishes the 0.25 child and leaves 0.125 for the 0.5 child, which is a
    // quarter of the way through it.
    ASSERT_TRUE(runner.advance(seconds(0.375)).has_value());
    EXPECT_FLOAT_EQ(first, 1.0F);
    EXPECT_FLOAT_EQ(second, 25.0F);
}

TEST(ActionRunnerTests, ASequenceRunsItsChildrenInOrderAndCompletesOnce)
{
    ActionRunner runner = makeRunner();
    std::vector<int> order;
    ASSERT_TRUE(runner
                    .play(Action::sequence(Action::call([&order]() { order.push_back(0); }),
                                           Action::call([&order]() { order.push_back(1); }),
                                           Action::call([&order]() { order.push_back(2); })))
                    .has_value());

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(order, std::vector<int>({0, 1, 2}));
    EXPECT_EQ(runner.activeCount(), 0U);
    EXPECT_EQ(runner.stats().completedCount, 1U);
}

// A parallel consumes as much as its longest-running child does, so its leftover is the
// smallest across children. Taking the largest would let a following sequence child start
// before the slowest branch here had ended.
TEST(ActionRunnerTests, AParallelYieldsTheSmallestLeftoverAcrossItsChildren)
{
    ActionRunner runner = makeRunner();
    float after = -1.0F;
    ASSERT_TRUE(runner
                    .play(Action::sequence(
                        Action::parallel(Action::delay(seconds(0.25)),
                                         Action::delay(seconds(0.5))),
                        Action::tweenFloat(seconds(1.0), 0.0F, 100.0F, Easing::Linear,
                                           [&after](float current) { after = current; })))
                    .has_value());

    // The parallel is offered 0.75. Its short child leaves 0.5 and its long child leaves
    // 0.25, so 0.25 reaches the tween.
    ASSERT_TRUE(runner.advance(seconds(0.75)).has_value());
    EXPECT_FLOAT_EQ(after, 25.0F);
}

TEST(ActionRunnerTests, AParallelWaitsForEveryChildBeforeCompleting)
{
    ActionRunner runner = makeRunner();
    int shortCalls = 0;
    int longCalls = 0;
    ASSERT_TRUE(runner
                    .play(Action::parallel(
                        Action::sequence(Action::delay(seconds(0.25)),
                                         Action::call([&shortCalls]() { ++shortCalls; })),
                        Action::sequence(Action::delay(seconds(1.0)),
                                         Action::call([&longCalls]() { ++longCalls; }))))
                    .has_value());

    ASSERT_TRUE(runner.advance(seconds(0.5)).has_value());
    EXPECT_EQ(shortCalls, 1);
    EXPECT_EQ(longCalls, 0);
    EXPECT_EQ(runner.activeCount(), 1U);

    ASSERT_TRUE(runner.advance(seconds(0.5)).has_value());
    EXPECT_EQ(longCalls, 1);
    EXPECT_EQ(runner.activeCount(), 0U);
    // A finished child is not re-run when the parallel is advanced again.
    EXPECT_EQ(shortCalls, 1);
}

TEST(ActionRunnerTests, RepeatRunsItsChildTheAuthoredNumberOfTimes)
{
    ActionRunner runner = makeRunner();
    int calls = 0;
    ASSERT_TRUE(runner
                    .play(Action::repeat(Repeat::times(3),
                                         Action::sequence(Action::delay(seconds(0.25)),
                                                          Action::call([&calls]() { ++calls; }))))
                    .has_value());

    for (int step = 0; step < 3; ++step) {
        ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    }
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(runner.activeCount(), 0U);

    ASSERT_TRUE(runner.advance(seconds(1.0)).has_value());
    EXPECT_EQ(calls, 3);
}

TEST(ActionRunnerTests, RepeatForeverKeepsRestartingItsChild)
{
    ActionRunner runner = makeRunner();
    int calls = 0;
    Core::Result<ActionId> played = runner.play(Action::repeat(
        Repeat::forever(),
        Action::sequence(Action::delay(seconds(0.25)), Action::call([&calls]() { ++calls; }))));
    ASSERT_TRUE(played.has_value());

    for (int step = 0; step < 5; ++step) {
        ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    }
    EXPECT_EQ(calls, 5);
    EXPECT_TRUE(runner.isPlaying(*played));

    EXPECT_TRUE(runner.cancel(*played).has_value());
    EXPECT_FALSE(runner.isPlaying(*played));
}

// A repeated subtree whose total duration is zero would never return, and the failure
// looks exactly like a hang rather than like a content error. It is bounded per advance
// and the refusals are counted so the cause is visible in stats.
TEST(ActionRunnerTests, AZeroDurationRepeatIsBoundedPerAdvanceAndCounted)
{
    ActionRunner runner = makeRunner(ActionRunnerConfig{.maximumRepeatIterationsPerAdvance = 4});
    int calls = 0;
    Core::Result<ActionId> played =
        runner.play(Action::repeat(Repeat::forever(), Action::call([&calls]() { ++calls; })));
    ASSERT_TRUE(played.has_value());

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(calls, 4);
    EXPECT_EQ(runner.stats().clampedRepeatIterations, 1U);
    // Bounded, not failed: the action is still playing.
    EXPECT_TRUE(runner.isPlaying(*played));

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(calls, 8);
    EXPECT_EQ(runner.stats().clampedRepeatIterations, 2U);
}

// The bound limits how many iterations run per advance; it must not consume iterations
// that never ran. Stopping the walk without clearing the child's cursors makes the next
// advance count an iteration whose body is skipped, so a finite repeat silently applies
// fewer times than it was authored to.
TEST(ActionRunnerTests, TheRepeatBoundDefersIterationsRatherThanDroppingThem)
{
    ActionRunner runner = makeRunner(ActionRunnerConfig{.maximumRepeatIterationsPerAdvance = 2});
    int calls = 0;
    ASSERT_TRUE(runner.play(Action::repeat(Repeat::times(3), Action::call([&calls]() { ++calls; })))
                    .has_value());

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(runner.stats().clampedRepeatIterations, 1U);

    // The third iteration still owes an apply.
    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(runner.activeCount(), 0U);
    EXPECT_EQ(runner.stats().completedCount, 1U);
}

// A completed action retires itself. Leaving it live would grow activeCount for the life
// of the scene and eventually exhaust actionCapacity with trees that can never advance.
TEST(ActionRunnerTests, ACompletedActionRetiresItselfWithoutCountingAsCancelled)
{
    ActionRunner runner = makeRunner();
    Core::Result<ActionId> played = runner.play(Action::delay(seconds(0.25)));
    ASSERT_TRUE(played.has_value());

    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    EXPECT_FALSE(runner.isPlaying(*played));
    EXPECT_EQ(runner.activeCount(), 0U);
    EXPECT_EQ(runner.stats().completedCount, 1U);
    // Completing is not cancelling: keeping the counters apart is what lets "the game
    // stopped this" be told from "this finished".
    EXPECT_EQ(runner.stats().cancelledCount, 0U);
    EXPECT_EQ(runner.setPaused(*played, true).error().code, GameplayErrorCode::InvalidHandle);
}

TEST(ActionRunnerTests, RetiringAnActionReleasesWhatItsSettersCaptured)
{
    ActionRunner runner = makeRunner();
    std::weak_ptr<int> observer;
    {
        auto captured = std::make_shared<int>(3);
        observer = captured;
        ASSERT_TRUE(runner
                        .play(Action::tween(seconds(0.25), Easing::Linear,
                                            [captured = std::move(captured)](float) {}))
                        .has_value());
    }
    EXPECT_FALSE(observer.expired());

    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    EXPECT_TRUE(observer.expired());
}

TEST(ActionRunnerTests, CancelAllReleasesEverySetter)
{
    ActionRunner runner = makeRunner();
    std::weak_ptr<int> observer;
    {
        auto captured = std::make_shared<int>(3);
        observer = captured;
        ASSERT_TRUE(runner
                        .play(Action::tween(seconds(10.0), Easing::Linear,
                                            [captured = std::move(captured)](float) {}))
                        .has_value());
    }

    runner.cancelAll();
    EXPECT_EQ(runner.activeCount(), 0U);
    EXPECT_EQ(runner.stats().cancelledCount, 1U);
    EXPECT_TRUE(observer.expired());
}

TEST(ActionRunnerTests, CancelRejectsAStaleOrDoubleCancelledHandle)
{
    ActionRunner runner = makeRunner();
    Core::Result<ActionId> played = runner.play(Action::delay(seconds(1.0)));
    ASSERT_TRUE(played.has_value());

    EXPECT_TRUE(runner.cancel(*played).has_value());
    Core::Status again = runner.cancel(*played);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, GameplayErrorCode::InvalidHandle);
    EXPECT_FALSE(runner.isPaused(*played).has_value());
}

// Cancelling the action a callback is running inside takes effect at the next node
// boundary, so no further node runs and the tree stays alive until the recursion unwinds.
TEST(ActionRunnerTests, AnActionMayCancelItselfFromItsOwnCallback)
{
    ActionRunner runner = makeRunner();
    ActionId self{};
    int firstCalls = 0;
    int secondCalls = 0;
    Core::Result<ActionId> played = runner.play(Action::sequence(
        Action::call([&]() {
            ++firstCalls;
            EXPECT_TRUE(runner.cancel(self).has_value());
        }),
        Action::call([&secondCalls]() { ++secondCalls; })));
    ASSERT_TRUE(played.has_value());
    self = *played;

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(firstCalls, 1);
    // The second node of the cancelled action never ran.
    EXPECT_EQ(secondCalls, 0);
    EXPECT_FALSE(runner.isPlaying(self));
    EXPECT_EQ(runner.activeCount(), 0U);
    // Cancelled, so it is not also counted as completed.
    EXPECT_EQ(runner.stats().completedCount, 0U);
    EXPECT_EQ(runner.stats().cancelledCount, 1U);
}

TEST(ActionRunnerTests, ACallbackMayCancelAnotherActionBeforeItIsAdvanced)
{
    ActionRunner runner = makeRunner();
    ActionId victim{};
    int victimCalls = 0;
    int cancellerCalls = 0;

    ASSERT_TRUE(runner
                    .play(Action::call([&]() {
                        ++cancellerCalls;
                        if (runner.isPlaying(victim)) {
                            EXPECT_TRUE(runner.cancel(victim).has_value());
                        }
                    }))
                    .has_value());

    Core::Result<ActionId> played = runner.play(Action::call([&victimCalls]() { ++victimCalls; }));
    ASSERT_TRUE(played.has_value());
    victim = *played;

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(cancellerCalls, 1);
    EXPECT_EQ(victimCalls, 0);
    EXPECT_FALSE(runner.isPlaying(victim));
}

// An action played from inside a callback first advances on the next advance(), so
// dispatch order never depends on how deeply the callbacks nested.
TEST(ActionRunnerTests, AnActionPlayedFromACallbackFirstAdvancesOnTheNextAdvance)
{
    ActionRunner runner = makeRunner();
    int nested = 0;
    bool playedOnce = false;
    ASSERT_TRUE(runner
                    .play(Action::repeat(Repeat::forever(),
                                         Action::sequence(Action::delay(seconds(0.25)),
                                                          Action::call([&]() {
                                                              if (playedOnce) {
                                                                  return;
                                                              }
                                                              playedOnce = true;
                                                              EXPECT_TRUE(
                                                                  runner
                                                                      .play(Action::call([&nested]() {
                                                                          ++nested;
                                                                      }))
                                                                      .has_value());
                                                          }))))
                    .has_value());

    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    EXPECT_TRUE(playedOnce);
    EXPECT_EQ(nested, 0);
    EXPECT_EQ(runner.activeCount(), 2U);

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(nested, 1);
}

TEST(ActionRunnerTests, AdvanceRefusesToBeReenteredFromACallback)
{
    ActionRunner runner = makeRunner();
    Core::ErrorCode innerCode{};
    bool attempted = false;
    ASSERT_TRUE(runner
                    .play(Action::call([&]() {
                        attempted = true;
                        Core::Status inner = runner.advance(seconds(0.25));
                        ASSERT_FALSE(inner.has_value());
                        innerCode = inner.error().code;
                    }))
                    .has_value());

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_TRUE(attempted);
    EXPECT_EQ(innerCode, GameplayErrorCode::ReentrantDispatch);

    // Dispatch was left clean, so a later advance still works.
    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
}

// A setter is game code and may throw. The dispatch flag is restored by a scope guard, so
// a throwing setter does not leave the runner refusing every later advance.
TEST(ActionRunnerTests, AThrowingSetterDoesNotLeaveTheRunnerWedged)
{
    ActionRunner runner = makeRunner();
    ASSERT_TRUE(runner
                    .play(Action::call([]() { throw std::runtime_error("setter failed"); }))
                    .has_value());

    EXPECT_THROW(static_cast<void>(runner.advance(seconds(0.0))), std::runtime_error);

    int later = 0;
    ASSERT_TRUE(runner.play(Action::call([&later]() { ++later; })).has_value());
    Core::Status afterThrow = runner.advance(seconds(0.0));
    ASSERT_TRUE(afterThrow.has_value());
    EXPECT_EQ(later, 1);
}

TEST(ActionRunnerTests, APausedActionDoesNotAdvance)
{
    ActionRunner runner = makeRunner();
    float value = -1.0F;
    Core::Result<ActionId> played = runner.play(
        Action::tweenFloat(seconds(1.0), 0.0F, 100.0F, Easing::Linear,
                           [&value](float current) { value = current; }),
        ActionPlayOptions{.startPaused = true});
    ASSERT_TRUE(played.has_value());

    Core::Result<bool> paused = runner.isPaused(*played);
    ASSERT_TRUE(paused.has_value());
    EXPECT_TRUE(*paused);

    ASSERT_TRUE(runner.advance(seconds(10.0)).has_value());
    EXPECT_FLOAT_EQ(value, -1.0F);

    ASSERT_TRUE(runner.setPaused(*played, false).has_value());
    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    // Paused time was not banked: a quarter of the tween ran, not all of it.
    EXPECT_FLOAT_EQ(value, 25.0F);
}

TEST(ActionRunnerTests, TimeScaleScalesEveryDeltaAndZeroIsAFullPause)
{
    ActionRunner runner = makeRunner();
    float value = -1.0F;
    ASSERT_TRUE(runner
                    .play(Action::tweenFloat(seconds(1.0), 0.0F, 100.0F, Easing::Linear,
                                             [&value](float current) { value = current; }))
                    .has_value());

    ASSERT_TRUE(runner.setTimeScale(0.5).has_value());
    EXPECT_DOUBLE_EQ(runner.timeScale(), 0.5);
    ASSERT_TRUE(runner.advance(seconds(0.5)).has_value());
    EXPECT_FLOAT_EQ(value, 25.0F);

    ASSERT_TRUE(runner.setTimeScale(0.0).has_value());
    ASSERT_TRUE(runner.advance(seconds(100.0)).has_value());
    EXPECT_FLOAT_EQ(value, 25.0F);
}

TEST(ActionRunnerTests, TimeScaleRejectsNegativeAndNonFiniteValues)
{
    ActionRunner runner = makeRunner();
    constexpr double quietNaN = std::numeric_limits<double>::quiet_NaN();
    for (const double bad : {-1.0, quietNaN, std::numeric_limits<double>::infinity()}) {
        Core::Status status = runner.setTimeScale(bad);
        ASSERT_FALSE(status.has_value()) << "scale " << bad;
        EXPECT_EQ(status.error().code, GameplayErrorCode::InvalidArgument);
    }
    EXPECT_DOUBLE_EQ(runner.timeScale(), 1.0);
}

// A pause menu's own transitions must keep running while gameplay time is scaled to zero.
TEST(ActionRunnerTests, AnActionCanOptOutOfTheTimeScale)
{
    ActionRunner runner = makeRunner();
    float scaled = -1.0F;
    float unscaled = -1.0F;
    ASSERT_TRUE(runner
                    .play(Action::tweenFloat(seconds(1.0), 0.0F, 100.0F, Easing::Linear,
                                             [&scaled](float current) { scaled = current; }))
                    .has_value());
    ASSERT_TRUE(runner
                    .play(Action::tweenFloat(seconds(1.0), 0.0F, 100.0F, Easing::Linear,
                                             [&unscaled](float current) { unscaled = current; }),
                          ActionPlayOptions{.ignoresTimeScale = true})
                    .has_value());

    ASSERT_TRUE(runner.setTimeScale(0.0).has_value());
    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    EXPECT_FLOAT_EQ(scaled, 0.0F);
    EXPECT_FLOAT_EQ(unscaled, 25.0F);
}

TEST(ActionRunnerTests, AdvanceRejectsNonFiniteAndNegativeDeltas)
{
    ActionRunner runner = makeRunner();
    constexpr double quietNaN = std::numeric_limits<double>::quiet_NaN();
    for (const double bad : {-1.0, quietNaN, std::numeric_limits<double>::infinity()}) {
        Core::Status status = runner.advance(seconds(bad));
        ASSERT_FALSE(status.has_value()) << "delta " << bad;
        EXPECT_EQ(status.error().code, GameplayErrorCode::InvalidArgument);
    }
    EXPECT_EQ(runner.stats().advanceCount, 0U);
}

// Actions advance in the order they were played, which is the only order a game can
// reason about: the pool resolves ids but has no stable traversal.
TEST(ActionRunnerTests, ActionsAdvanceInPlayOrder)
{
    ActionRunner runner = makeRunner();
    std::vector<int> order;
    for (int index = 0; index < 4; ++index) {
        ASSERT_TRUE(runner.play(Action::call([&order, index]() { order.push_back(index); }))
                        .has_value());
    }

    ASSERT_TRUE(runner.advance(seconds(0.0)).has_value());
    EXPECT_EQ(order, std::vector<int>({0, 1, 2, 3}));
}

TEST(ActionRunnerTests, StatsSeparateTheLiveCountFromThePeak)
{
    ActionRunner runner = makeRunner(ActionRunnerConfig{.actionCapacity = 16});
    std::vector<ActionId> played;
    for (int index = 0; index < 5; ++index) {
        Core::Result<ActionId> action = runner.play(Action::delay(seconds(1.0)));
        ASSERT_TRUE(action.has_value());
        played.push_back(*action);
    }
    EXPECT_EQ(runner.stats().activeActionCount, 5U);
    EXPECT_EQ(runner.stats().activeActionHighWater, 5U);
    EXPECT_EQ(runner.stats().startedCount, 5U);

    for (const ActionId action : played) {
        EXPECT_TRUE(runner.cancel(action).has_value());
    }
    EXPECT_EQ(runner.stats().activeActionCount, 0U);
    EXPECT_EQ(runner.stats().activeActionHighWater, 5U);
    EXPECT_EQ(runner.stats().actionCapacity, 16U);
}

// Bookkeeping storage is taken at Create, so advancing and cancelling never reach the
// allocator. play() does allocate the per-node state, which is why it is the one call a
// game is expected to keep out of its hot path.
TEST(ActionRunnerTests, AdvancingAndCancellingDoNotAllocate)
{
    CountingMemoryResource resource;
    ActionRunner runner =
        makeRunner(ActionRunnerConfig{.actionCapacity = 8, .memoryResource = &resource});
    std::vector<ActionId> played;
    for (int index = 0; index < 8; ++index) {
        Core::Result<ActionId> action = runner.play(Action::sequence(
            Action::delay(seconds(0.25)), Action::delay(seconds(0.25))));
        ASSERT_TRUE(action.has_value());
        played.push_back(*action);
    }

    resource.resetCount();
    ASSERT_TRUE(runner.advance(seconds(0.25)).has_value());
    EXPECT_EQ(resource.allocationCount(), 0U);

    for (const ActionId action : played) {
        EXPECT_TRUE(runner.cancel(action).has_value());
    }
    EXPECT_EQ(resource.allocationCount(), 0U);
}

TEST(ActionRunnerTests, AMovedFromRunnerIsInertRatherThanUndefined)
{
    ActionRunner source = makeRunner();
    Core::Result<ActionId> played = source.play(Action::delay(seconds(1.0)));
    ASSERT_TRUE(played.has_value());

    ActionRunner moved = std::move(source);
    EXPECT_EQ(moved.activeCount(), 1U);

    EXPECT_EQ(source.advance(seconds(0.25)).error().code,
              GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(source.play(Action::delay(seconds(1.0))).error().code,
              GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(source.cancel(*played).error().code, GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(source.setTimeScale(2.0).error().code, GameplayErrorCode::InvalidConfiguration);
    EXPECT_FALSE(source.isPlaying(*played));
    EXPECT_EQ(source.activeCount(), 0U);
    EXPECT_EQ(source.stats().actionCapacity, 0U);
    source.cancelAll();
}

} // namespace Tina::Gameplay
