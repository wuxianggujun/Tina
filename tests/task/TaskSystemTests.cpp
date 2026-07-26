#include <gtest/gtest.h>

#include <tina/task/TaskErrors.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <limits>

namespace Tina::Tests {

TEST(DisabledTaskSystemTest, IsAlwaysIdleAndShutdownIsIdempotent)
{
    auto taskSystemResult = Task::createDisabledTaskSystem(Task::TaskSystemCreateParams{});
    ASSERT_TRUE(taskSystemResult.has_value());
    ASSERT_NE(*taskSystemResult, nullptr);

    EXPECT_TRUE((*taskSystemResult)->isIdle());
    EXPECT_FALSE((*taskSystemResult)->scheduleIo([] {}).has_value());
    EXPECT_EQ((*taskSystemResult)->scheduleIo([] {}).error().code, Task::TaskErrorCode::NotSupported);
    EXPECT_FALSE((*taskSystemResult)->scheduleCpu([] {}).has_value());
    EXPECT_EQ((*taskSystemResult)->scheduleCpu([] {}).error().code, Task::TaskErrorCode::NotSupported);
    (*taskSystemResult)->shutdownAndJoin();
    (*taskSystemResult)->shutdownAndJoin();
    EXPECT_TRUE((*taskSystemResult)->isIdle());
}

TEST(DisabledTaskSystemTest, ShutdownDeadlineValidatesAndRepeatedSuccessIsIdempotent)
{
    auto taskSystemResult = Task::createDisabledTaskSystem(Task::TaskSystemCreateParams{});
    ASSERT_TRUE(taskSystemResult.has_value());

    const Core::Duration invalidDeadlines[] = {
        Core::Duration::zero(),
        Core::Duration{-1.0},
        Core::Duration{std::numeric_limits<double>::infinity()},
        Core::Duration{std::numeric_limits<double>::quiet_NaN()},
    };
    for (const Core::Duration deadline : invalidDeadlines)
    {
        const auto status = (*taskSystemResult)->shutdownAndJoinFor(deadline);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, Task::TaskErrorCode::InvalidArgument);
        EXPECT_FALSE((*taskSystemResult)->isStopping());
    }

    EXPECT_TRUE((*taskSystemResult)->shutdownAndJoinFor(Core::Duration{0.001}).has_value());
    EXPECT_TRUE((*taskSystemResult)->isStopping());
    EXPECT_TRUE((*taskSystemResult)->shutdownAndJoinFor(Core::Duration{0.001}).has_value());
}

} // namespace Tina::Tests
