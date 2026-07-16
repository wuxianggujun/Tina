#include <gtest/gtest.h>

#include <tina/core/time/FixedStepAccumulator.hpp>
#include <tina/core/time/MonotonicClock.hpp>

#include <support/ManualMonotonicClock.hpp>

#include <cmath>
#include <limits>
#include <utility>

namespace Tina::Tests {

TEST(MonotonicClockTest, SteadyClockIsMonotonicAndClockCanBeInjected)
{
    Core::SteadyMonotonicClock steadyClock;
    const auto begin = steadyClock.now();
    const auto end = steadyClock.now();
    EXPECT_GE(end, begin);
    EXPECT_GE(Core::durationBetween(begin, end).count(), 0.0);

    ManualMonotonicClock manualClock;
    const Core::IMonotonicClock& clock = manualClock;
    const auto manualBegin = clock.now();
    manualClock.advance(Core::Duration{0.25});
    EXPECT_NEAR(Core::durationBetween(manualBegin, clock.now()).count(), 0.25, 1.0e-8);
}

TEST(FixedStepAccumulatorTest, ProducesDeterministicStepsAndInterpolation)
{
    auto accumulatorResult = Core::FixedStepAccumulator::Create(Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.1},
        .maximumAcceptedRealDelta = Core::Duration{0.5},
        .maximumStepsPerFrame = 4,
    });
    ASSERT_TRUE(accumulatorResult);
    auto accumulator = std::move(*accumulatorResult);

    const auto idlePlan = accumulator.advance(Core::Duration{0.0});
    ASSERT_TRUE(idlePlan);
    EXPECT_EQ(idlePlan->stepCount, 0U);
    EXPECT_DOUBLE_EQ(idlePlan->interpolation, 0.0);

    const auto plan = accumulator.advance(Core::Duration{0.25});
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->stepCount, 2U);
    EXPECT_NEAR(plan->fixedDelta.count(), 0.1, 1.0e-12);
    EXPECT_NEAR(plan->interpolation, 0.5, 1.0e-12);
    EXPECT_NEAR(accumulator.pendingDelta().count(), 0.05, 1.0e-12);
}

TEST(FixedStepAccumulatorTest, CapsCatchUpAndAccountsForDiscardedTime)
{
    auto accumulatorResult = Core::FixedStepAccumulator::Create(Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.1},
        .maximumAcceptedRealDelta = Core::Duration{0.5},
        .maximumStepsPerFrame = 4,
    });
    ASSERT_TRUE(accumulatorResult);
    auto accumulator = std::move(*accumulatorResult);

    const auto partialPlan = accumulator.advance(Core::Duration{0.05});
    ASSERT_TRUE(partialPlan);
    EXPECT_EQ(partialPlan->stepCount, 0U);

    const auto plan = accumulator.advance(Core::Duration{0.8});
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->stepCount, 4U);
    EXPECT_NEAR(plan->acceptedRealDelta.count(), 0.5, 1.0e-12);
    EXPECT_NEAR(plan->rejectedRealDelta.count(), 0.3, 1.0e-12);
    EXPECT_NEAR(plan->discardedSimulationDelta.count(), 0.1, 1.0e-12);
    EXPECT_NEAR(accumulator.totalRejectedRealDelta().count(), 0.3, 1.0e-12);
    EXPECT_NEAR(accumulator.totalDiscardedSimulationDelta().count(), 0.1, 1.0e-12);
    EXPECT_NEAR(plan->interpolation, 0.5, 1.0e-12);
}

TEST(FixedStepAccumulatorTest, DropsWholeDebtButPreservesFractionalRemainder)
{
    auto accumulatorResult = Core::FixedStepAccumulator::Create(Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.1},
        .maximumAcceptedRealDelta = Core::Duration{0.6},
        .maximumStepsPerFrame = 4,
    });
    ASSERT_TRUE(accumulatorResult);
    auto accumulator = std::move(*accumulatorResult);

    const auto plan = accumulator.advance(Core::Duration{0.57});
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->stepCount, 4U);
    EXPECT_NEAR(plan->discardedSimulationDelta.count(), 0.1, 1.0e-12);
    EXPECT_NEAR(accumulator.pendingDelta().count(), 0.07, 1.0e-12);
    EXPECT_NEAR(plan->interpolation, 0.7, 1.0e-12);

    accumulator.reset();
    EXPECT_DOUBLE_EQ(accumulator.pendingDelta().count(), 0.0);
    EXPECT_DOUBLE_EQ(accumulator.totalRejectedRealDelta().count(), 0.0);
    EXPECT_DOUBLE_EQ(accumulator.totalDiscardedSimulationDelta().count(), 0.0);
}

TEST(FixedStepAccumulatorTest, RejectsInvalidConfigurationAndDeltaWithoutMutation)
{
    EXPECT_FALSE(Core::FixedStepAccumulator::Create(Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.0},
    }));
    EXPECT_FALSE(Core::FixedStepAccumulator::Create(Core::FixedStepConfig{
        .maximumAcceptedRealDelta = Core::Duration{std::numeric_limits<double>::infinity()},
    }));
    EXPECT_FALSE(Core::FixedStepAccumulator::Create(Core::FixedStepConfig{
        .maximumStepsPerFrame = 0,
    }));

    auto accumulatorResult = Core::FixedStepAccumulator::Create(Core::FixedStepConfig{});
    ASSERT_TRUE(accumulatorResult);
    auto accumulator = std::move(*accumulatorResult);

    const auto pendingPlan = accumulator.advance(Core::Duration{0.005});
    ASSERT_TRUE(pendingPlan);
    const Core::Duration pendingBeforeFailure = accumulator.pendingDelta();

    EXPECT_FALSE(accumulator.advance(Core::Duration{-0.01}));
    EXPECT_FALSE(accumulator.advance(Core::Duration{std::numeric_limits<double>::quiet_NaN()}));
    EXPECT_FALSE(accumulator.advance(Core::Duration{std::numeric_limits<double>::infinity()}));
    EXPECT_FALSE(accumulator.advance(Core::Duration{0.01}, -1.0));
    EXPECT_FALSE(accumulator.advance(
        Core::Duration{0.01},
        std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(accumulator.advance(
        Core::Duration{0.01},
        std::numeric_limits<double>::infinity()));
    EXPECT_EQ(accumulator.pendingDelta(), pendingBeforeFailure);
    EXPECT_DOUBLE_EQ(accumulator.totalRejectedRealDelta().count(), 0.0);
    EXPECT_DOUBLE_EQ(accumulator.totalDiscardedSimulationDelta().count(), 0.0);
}

TEST(FixedStepAccumulatorTest, AppliesGameplayTimeScaleAfterRealDeltaClamp)
{
    auto accumulatorResult = Core::FixedStepAccumulator::Create(Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.1},
        .maximumAcceptedRealDelta = Core::Duration{0.25},
        .maximumStepsPerFrame = 4,
    });
    ASSERT_TRUE(accumulatorResult);
    auto accumulator = std::move(*accumulatorResult);

    const auto plan = accumulator.advance(Core::Duration{0.2}, 0.5);
    ASSERT_TRUE(plan);
    EXPECT_NEAR(plan->realDelta.count(), 0.2, 1.0e-12);
    EXPECT_NEAR(plan->acceptedRealDelta.count(), 0.2, 1.0e-12);
    EXPECT_NEAR(plan->updateDelta.count(), 0.1, 1.0e-12);
    EXPECT_EQ(plan->stepCount, 1U);
}

} // namespace Tina::Tests
