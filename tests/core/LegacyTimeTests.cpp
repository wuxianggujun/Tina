#include <gtest/gtest.h>

#include "core/time/Clock.hpp"
#include "core/time/FrameTimer.hpp"

#include <stdexcept>

namespace Tina::Tests {

TEST(LegacyTimeTest, ClockAndFrameTimerAreMonotonic)
{
    const auto begin = Core::Clock::now();
    const auto end = Core::Clock::now();
    EXPECT_GE(end, begin);
    EXPECT_GE(Core::Clock::secondsBetween(begin, end), 0.0);

    Core::FrameTimer timer;
    timer.beginFrame();
    EXPECT_GE(timer.frameSeconds(), 0.0);
    EXPECT_GE(timer.sinceStartupSeconds(), 0.0);
}

TEST(LegacyTimeTest, FixedTickerPreservesExistingRuntimeBehavior)
{
    Core::TimeConfig config;
    config.dt_min = 0.0;
    config.dt_max = 1.0;
    config.tick_rate = 10;
    config.max_substeps = 4;

    Core::FixedStepTicker ticker(config);
    int callbackCount = 0;
    ticker.accumulate(0.25);
    const int stepCount = ticker.step([&callbackCount](double fixedDelta) {
        EXPECT_NEAR(fixedDelta, 0.1, 1e-9);
        ++callbackCount;
    });

    EXPECT_EQ(stepCount, 2);
    EXPECT_EQ(callbackCount, 2);
    EXPECT_NEAR(ticker.alpha(), 0.5, 1e-9);
}

TEST(LegacyTimeTest, FixedTickerConsumesFailedStepBeforeRethrowing)
{
    Core::TimeConfig config;
    config.dt_max = 1.0;
    config.tick_rate = 10;
    Core::FixedStepTicker ticker(config);
    ticker.accumulate(0.1);

    int attemptedSteps = 0;
    EXPECT_THROW(
        ticker.step([&attemptedSteps](double) {
            ++attemptedSteps;
            throw std::runtime_error("simulation failed");
        }),
        std::runtime_error);
    EXPECT_EQ(attemptedSteps, 1);
    EXPECT_EQ(ticker.step([](double) noexcept {}), 0);
}

} // namespace Tina::Tests
