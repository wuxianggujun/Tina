#include <gtest/gtest.h>

#include <tina/task/TaskErrors.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

namespace Tina::Tests {

TEST(DisabledTaskSystemTest, IsAlwaysIdleAndShutdownIsIdempotent)
{
    auto taskSystemResult = Task::createDisabledTaskSystem(Task::TaskSystemCreateParams{});
    ASSERT_TRUE(taskSystemResult.has_value());
    ASSERT_NE(*taskSystemResult, nullptr);

    EXPECT_TRUE((*taskSystemResult)->isIdle());
    EXPECT_FALSE((*taskSystemResult)->scheduleIo([] {}).has_value());
    EXPECT_EQ((*taskSystemResult)->scheduleIo([] {}).error().code, Task::TaskErrorCode::NotSupported);
    (*taskSystemResult)->shutdownAndJoin();
    (*taskSystemResult)->shutdownAndJoin();
    EXPECT_TRUE((*taskSystemResult)->isIdle());
}

} // namespace Tina::Tests
