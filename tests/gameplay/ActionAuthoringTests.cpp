#include <tina/gameplay/Action.hpp>

#include <tina/gameplay/GameplayErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Gameplay {
namespace {

constexpr Core::Duration seconds(double value) noexcept
{
    return Core::Duration{value};
}

[[nodiscard]] Action noopTween(Core::Duration duration = Core::Duration{1.0})
{
    return Action::tween(duration, Easing::Linear, [](float) {});
}

} // namespace

TEST(ActionAuthoringTests, ADefaultConstructedActionHoldsNothingAndHasNotFailed)
{
    Action action;
    EXPECT_FALSE(action.hasValue());
    // Empty is not the same as poisoned: nothing was authored wrongly, there is just
    // no tree.
    EXPECT_FALSE(action.failed());
    EXPECT_EQ(action.nodeCount(), 0U);
    EXPECT_FALSE(action.status().has_value());
}

TEST(ActionAuthoringTests, ATweenIsUsableAndCountsOneNode)
{
    Action action = noopTween();
    EXPECT_TRUE(action.hasValue());
    EXPECT_FALSE(action.failed());
    EXPECT_EQ(action.nodeCount(), 1U);
    EXPECT_TRUE(action.status().has_value());
}

TEST(ActionAuthoringTests, TweenRejectsAnEmptyApplyAndANonFiniteDuration)
{
    Action noApply = Action::tween(seconds(1.0), Easing::Linear, TweenApply{});
    EXPECT_FALSE(noApply.hasValue());
    EXPECT_TRUE(noApply.failed());
    EXPECT_EQ(noApply.failureCode(), GameplayErrorCode::MissingCallback);

    constexpr double quietNaN = std::numeric_limits<double>::quiet_NaN();
    for (const double bad : {-1.0, quietNaN, std::numeric_limits<double>::infinity()}) {
        Action action = Action::tween(seconds(bad), Easing::Linear, [](float) {});
        EXPECT_FALSE(action.hasValue()) << "duration " << bad;
        EXPECT_EQ(action.failureCode(), GameplayErrorCode::InvalidArgument);
    }
}

// The easing enum is authored into content, so an out-of-range value is a content
// error rather than something to clamp to Linear.
TEST(ActionAuthoringTests, TweenRejectsAnOutOfRangeEasing)
{
    Action sentinel = Action::tween(seconds(1.0), Easing::Count, [](float) {});
    EXPECT_FALSE(sentinel.hasValue());
    EXPECT_EQ(sentinel.failureCode(), GameplayErrorCode::InvalidArgument);

    Action beyond = Action::tween(seconds(1.0), static_cast<Easing>(200), [](float) {});
    EXPECT_FALSE(beyond.hasValue());
    EXPECT_EQ(beyond.failureCode(), GameplayErrorCode::InvalidArgument);
}

TEST(ActionAuthoringTests, DelayRejectsANonFiniteOrNegativeDuration)
{
    EXPECT_TRUE(Action::delay(seconds(0.5)).hasValue());
    EXPECT_TRUE(Action::delay(seconds(0.0)).hasValue());

    constexpr double quietNaN = std::numeric_limits<double>::quiet_NaN();
    for (const double bad : {-0.5, quietNaN, std::numeric_limits<double>::infinity()}) {
        Action action = Action::delay(seconds(bad));
        EXPECT_FALSE(action.hasValue()) << "duration " << bad;
        EXPECT_EQ(action.failureCode(), GameplayErrorCode::InvalidArgument);
    }
}

// call() is a zero-duration tween rather than a new node kind, which is what makes
// "after this, do that" obey the same sequencing rule as everything else.
TEST(ActionAuthoringTests, CallIsAuthoredAsASingleNode)
{
    int calls = 0;
    Action action = Action::call([&calls]() { ++calls; });
    EXPECT_TRUE(action.hasValue());
    EXPECT_EQ(action.nodeCount(), 1U);
    // Authoring does not run it; only advancing does.
    EXPECT_EQ(calls, 0);
}

TEST(ActionAuthoringTests, MovingAnActionTransfersTheTreeAndLeavesTheSourceEmpty)
{
    Action source = noopTween();
    ASSERT_TRUE(source.hasValue());

    Action moved = std::move(source);
    EXPECT_TRUE(moved.hasValue());
    EXPECT_EQ(moved.nodeCount(), 1U);
    EXPECT_FALSE(source.hasValue());
    EXPECT_EQ(source.nodeCount(), 0U);
}

TEST(ActionAuthoringTests, MoveAssignmentReplacesAnExistingTree)
{
    Action target = noopTween();
    Action source = Action::sequence(noopTween(), noopTween());
    ASSERT_TRUE(source.hasValue());
    const Core::usize sourceNodes = source.nodeCount();

    target = std::move(source);
    EXPECT_TRUE(target.hasValue());
    EXPECT_EQ(target.nodeCount(), sourceNodes);
    EXPECT_FALSE(source.hasValue());
}

// A sequence's node count is its own node plus every node of its children, because the
// children are spliced into one program rather than referenced as separate trees.
TEST(ActionAuthoringTests, SequenceSplicesItsChildrenIntoOneProgram)
{
    Action action = Action::sequence(noopTween(), noopTween(), noopTween());
    ASSERT_TRUE(action.hasValue());
    EXPECT_EQ(action.nodeCount(), 4U);
}

TEST(ActionAuthoringTests, ParallelSplicesItsChildrenIntoOneProgram)
{
    Action action = Action::parallel(noopTween(), noopTween());
    ASSERT_TRUE(action.hasValue());
    EXPECT_EQ(action.nodeCount(), 3U);
}

TEST(ActionAuthoringTests, NestedCombinatorsAccumulateNodeCounts)
{
    Action inner = Action::parallel(noopTween(), noopTween());
    ASSERT_EQ(inner.nodeCount(), 3U);

    Action outer = Action::sequence(std::move(inner), Action::delay(seconds(0.5)));
    ASSERT_TRUE(outer.hasValue());
    // The sequence node, the spliced parallel subtree (3), and the delay.
    EXPECT_EQ(outer.nodeCount(), 5U);
}

// Combinators consume their children, so each child is used exactly once and cannot be
// spliced into two parents.
TEST(ActionAuthoringTests, CombinatorsConsumeTheirChildren)
{
    Action child = noopTween();
    ASSERT_TRUE(child.hasValue());

    Action parent = Action::sequence(std::move(child), noopTween());
    EXPECT_TRUE(parent.hasValue());
    EXPECT_FALSE(child.hasValue());
}

// A combinator over nothing is InvalidSequence rather than an empty action that
// completes instantly, because an empty child list is nearly always a loop that
// produced no children.
TEST(ActionAuthoringTests, AnEmptySpanIsRejectedByBothCombinators)
{
    Action emptySequence = Action::sequence(std::span<Action>{});
    EXPECT_FALSE(emptySequence.hasValue());
    EXPECT_EQ(emptySequence.failureCode(), GameplayErrorCode::InvalidSequence);

    Action emptyParallel = Action::parallel(std::span<Action>{});
    EXPECT_FALSE(emptyParallel.hasValue());
    EXPECT_EQ(emptyParallel.failureCode(), GameplayErrorCode::InvalidSequence);
}

TEST(ActionAuthoringTests, TheSpanOverloadAcceptsARuntimeSizedChildList)
{
    std::array<Action, 3> children{noopTween(), noopTween(), noopTween()};
    Action action = Action::sequence(std::span<Action>(children));
    ASSERT_TRUE(action.hasValue());
    EXPECT_EQ(action.nodeCount(), 4U);
    for (const Action& child : children) {
        EXPECT_FALSE(child.hasValue());
    }
}

// Authoring is fail-late: an invalid argument poisons the value instead of stopping the
// expression, so composing five tweens does not mean five error checks.
TEST(ActionAuthoringTests, APoisonedChildPoisonsItsParent)
{
    Action poisoned = Action::delay(seconds(-1.0));
    ASSERT_TRUE(poisoned.failed());

    Action parent = Action::sequence(noopTween(), std::move(poisoned), noopTween());
    EXPECT_FALSE(parent.hasValue());
    EXPECT_TRUE(parent.failed());
    EXPECT_EQ(parent.failureCode(), GameplayErrorCode::InvalidArgument);
}

// The first failure is what survives, so the reported diagnostic names the earliest
// wrong subexpression rather than the last one composed.
TEST(ActionAuthoringTests, TheFirstAuthoringFailureIsTheOneThatSurvives)
{
    Action firstBad = Action::tween(seconds(1.0), Easing::Linear, TweenApply{});
    ASSERT_EQ(firstBad.failureCode(), GameplayErrorCode::MissingCallback);
    Action secondBad = Action::delay(seconds(-1.0));
    ASSERT_EQ(secondBad.failureCode(), GameplayErrorCode::InvalidArgument);

    Action parent = Action::sequence(std::move(firstBad), std::move(secondBad));
    EXPECT_EQ(parent.failureCode(), GameplayErrorCode::MissingCallback);
}

// An empty child is not a poisoned one, but it is still not something that can be
// sequenced: there is no tree to splice. A child already consumed by another
// combinator lands here too, which is how double use is caught.
TEST(ActionAuthoringTests, AnEmptyChildIsRejectedRatherThanSkipped)
{
    Action parent = Action::sequence(noopTween(), Action{});
    EXPECT_FALSE(parent.hasValue());
    EXPECT_TRUE(parent.failed());
    EXPECT_EQ(parent.failureCode(), GameplayErrorCode::InvalidSequence);

    Action consumed = noopTween();
    Action firstParent = Action::sequence(std::move(consumed), noopTween());
    ASSERT_TRUE(firstParent.hasValue());
    Action secondParent = Action::sequence(std::move(consumed), noopTween());
    EXPECT_FALSE(secondParent.hasValue());
    EXPECT_EQ(secondParent.failureCode(), GameplayErrorCode::InvalidSequence);
}

TEST(ActionAuthoringTests, StatusCarriesTheAuthoringFailureCode)
{
    Action poisoned = Action::delay(seconds(-1.0));
    Core::Status status = poisoned.status();
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, GameplayErrorCode::InvalidArgument);
    // The message is a literal recorded at authoring time, so the poisoned path stays
    // allocation-free until someone asks for it.
    EXPECT_FALSE(status.error().message.empty());
}

// Repeat{count = 0, infinite = false} is rejected rather than read as "forever", for the
// same reason it is on the Scheduler: a count that came out empty and a deliberate
// endless loop behave nothing alike.
TEST(ActionAuthoringTests, RepeatRejectsAZeroCountInsteadOfReadingItAsForever)
{
    Action zero = Action::repeat(Repeat{.count = 0, .infinite = false}, noopTween());
    EXPECT_FALSE(zero.hasValue());
    EXPECT_EQ(zero.failureCode(), GameplayErrorCode::InvalidArgument);

    Action forever = Action::repeat(Repeat::forever(), noopTween());
    EXPECT_TRUE(forever.hasValue());
    EXPECT_EQ(forever.nodeCount(), 2U);
}

TEST(ActionAuthoringTests, RepeatPropagatesAPoisonedChild)
{
    Action action = Action::repeat(Repeat::times(2), Action::delay(seconds(-1.0)));
    EXPECT_FALSE(action.hasValue());
    EXPECT_EQ(action.failureCode(), GameplayErrorCode::InvalidArgument);
}

// A tree larger than the bound is almost always a loop building nodes rather than an
// authored intent, and an unbounded tree would let one gameplay event allocate without
// limit.
TEST(ActionAuthoringTests, ATreeLargerThanTheNodeBoundIsRejected)
{
    std::vector<Action> children;
    children.reserve(MaximumActionNodeCount);
    for (Core::usize index = 0; index < MaximumActionNodeCount; ++index) {
        children.push_back(noopTween());
    }

    // MaximumActionNodeCount leaves plus the sequence node itself is one too many.
    Action action = Action::sequence(std::span<Action>(children));
    EXPECT_FALSE(action.hasValue());
    EXPECT_EQ(action.failureCode(), GameplayErrorCode::CapacityExceeded);
}

TEST(ActionAuthoringTests, ATreeExactlyAtTheNodeBoundIsAccepted)
{
    std::vector<Action> children;
    children.reserve(MaximumActionNodeCount - 1);
    for (Core::usize index = 0; index < MaximumActionNodeCount - 1; ++index) {
        children.push_back(noopTween());
    }

    Action action = Action::sequence(std::span<Action>(children));
    ASSERT_TRUE(action.hasValue()) << action.status().error().message;
    EXPECT_EQ(action.nodeCount(), MaximumActionNodeCount);
}

// A rejected combine must not leave half its children consumed and half intact, because
// the caller has no way to tell which half it still owns.
TEST(ActionAuthoringTests, ARejectedCombineStillConsumesEveryChild)
{
    std::array<Action, 3> children{noopTween(), Action::delay(seconds(-1.0)), noopTween()};
    Action action = Action::sequence(std::span<Action>(children));
    ASSERT_FALSE(action.hasValue());

    for (const Action& child : children) {
        EXPECT_FALSE(child.hasValue());
    }
}

// The typed tweens capture `from` at authoring time. Reading it at play() instead would
// make the result depend on when the action happened to start.
TEST(ActionAuthoringTests, TypedTweensAreSingleNodesOverTheirSetter)
{
    float scalar = 0.0F;
    Action floatTween =
        Action::tweenFloat(seconds(1.0), 0.0F, 10.0F, Easing::Linear,
                           [&scalar](float value) { scalar = value; });
    EXPECT_TRUE(floatTween.hasValue());
    EXPECT_EQ(floatTween.nodeCount(), 1U);

    Math::Vec2 position{};
    Action vec2Tween = Action::tweenVec2(seconds(1.0), Math::Vec2{0.0F, 0.0F},
                                        Math::Vec2{1.0F, 2.0F}, Easing::Linear,
                                        [&position](Math::Vec2 value) { position = value; });
    EXPECT_TRUE(vec2Tween.hasValue());

    Math::Vec3 translation{};
    Action vec3Tween = Action::tweenVec3(seconds(1.0), Math::Vec3{}, Math::Vec3{1.0F, 2.0F, 3.0F},
                                        Easing::Linear,
                                        [&translation](Math::Vec3 value) { translation = value; });
    EXPECT_TRUE(vec3Tween.hasValue());

    Math::Vec4 colour{};
    Action vec4Tween = Action::tweenVec4(seconds(1.0), Math::Vec4{},
                                        Math::Vec4{1.0F, 1.0F, 1.0F, 1.0F}, Easing::Linear,
                                        [&colour](Math::Vec4 value) { colour = value; });
    EXPECT_TRUE(vec4Tween.hasValue());

    // Authoring runs no setter.
    EXPECT_FLOAT_EQ(scalar, 0.0F);
}

TEST(ActionAuthoringTests, TypedTweensRejectABadDurationLikeTheUntypedOne)
{
    Action action = Action::tweenFloat(seconds(-1.0), 0.0F, 1.0F, Easing::Linear, [](float) {});
    EXPECT_FALSE(action.hasValue());
    EXPECT_EQ(action.failureCode(), GameplayErrorCode::InvalidArgument);
}

} // namespace Tina::Gameplay
