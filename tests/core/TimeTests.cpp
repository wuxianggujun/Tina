#include "TestHarness.hpp"

#include "core/time/Clock.hpp"
#include "core/time/FrameTimer.hpp"

#include <cmath>
#include <stdexcept>

namespace Tina::Tests {

void runTimeTests()
{
    const auto begin = Core::Clock::now();
    const auto end = Core::Clock::now();
    TINA_TEST_CHECK(end >= begin);
    TINA_TEST_CHECK(Core::Clock::secondsBetween(begin, end) >= 0.0);

    Core::FrameTimer timer;
    timer.beginFrame();
    TINA_TEST_CHECK(timer.frameSeconds() >= 0.0);
    TINA_TEST_CHECK(timer.sinceStartupSeconds() >= 0.0);

    Core::TimeConfig config;
    config.dt_min = 0.0;
    config.dt_max = 1.0;
    config.tick_rate = 10;
    config.max_substeps = 4;

    Core::FixedStepTicker ticker(config);
    int callbackCount = 0;
    ticker.accumulate(0.25);
    const int stepCount = ticker.step([&callbackCount](double fixedDelta) {
        TINA_TEST_CHECK(std::abs(fixedDelta - 0.1) < 1e-9);
        ++callbackCount;
    });

    TINA_TEST_CHECK(stepCount == 2);
    TINA_TEST_CHECK(callbackCount == 2);
    TINA_TEST_CHECK(std::abs(ticker.alpha() - 0.5) < 1e-9);

    Core::TimeConfig highRefreshConfig;
    highRefreshConfig.tick_rate = 240;
    highRefreshConfig.max_substeps = 4;
    Core::FixedStepTicker highRefreshTicker(highRefreshConfig);
    highRefreshTicker.accumulate(1.0 / 240.0);
    TINA_TEST_CHECK(highRefreshTicker.step([](double) noexcept {}) == 1);

    Core::TimeConfig exceptionConfig;
    exceptionConfig.dt_max = 1.0;
    exceptionConfig.tick_rate = 10;
    Core::FixedStepTicker exceptionTicker(exceptionConfig);
    exceptionTicker.accumulate(0.1);
    int attemptedSteps = 0;
    try {
        exceptionTicker.step([&attemptedSteps](double) {
            ++attemptedSteps;
            throw std::runtime_error("simulation failed");
        });
    } catch (const std::runtime_error&) {
    }
    TINA_TEST_CHECK(attemptedSteps == 1);
    TINA_TEST_CHECK(exceptionTicker.step([](double) noexcept {}) == 0);

    config.tick_rate = 0;
    Core::FixedStepTicker disabledTicker(config);
    disabledTicker.accumulate(0.5);
    TINA_TEST_CHECK(disabledTicker.step([](double) {}) == 0);
    TINA_TEST_CHECK(disabledTicker.alpha() == 0.0);
}

} // namespace Tina::Tests
