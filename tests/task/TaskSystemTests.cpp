#include <gtest/gtest.h>

#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

namespace Tina::Tests {

TEST(DisabledTaskSystemTest, IsAlwaysIdleAndShutdownIsIdempotent)
{
    auto taskSystemResult = Task::createDisabledTaskSystem(Task::TaskSystemCreateParams{});
    ASSERT_TRUE(taskSystemResult.has_value());
    ASSERT_NE(*taskSystemResult, nullptr);

    EXPECT_TRUE((*taskSystemResult)->isIdle());
    (*taskSystemResult)->shutdownAndJoin();
    (*taskSystemResult)->shutdownAndJoin();
    EXPECT_TRUE((*taskSystemResult)->isIdle());
}

} // namespace Tina::Tests
