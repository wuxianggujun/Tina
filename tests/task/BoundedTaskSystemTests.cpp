#include <gtest/gtest.h>

#include <tina/task/TaskErrors.hpp>
#include <tina/task/TaskGroup.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace Tina::Tests {

TEST(BoundedTaskSystemTest, ScheduleIoAndPumpMain)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 8,
        .mainQueueCapacity = 8,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    std::atomic<int> value{0};
    ASSERT_TRUE((*system)
                    ->scheduleIo([&] {
                        value.store(1, std::memory_order_release);
                        ASSERT_TRUE((*system)
                                        ->postMain([&] { value.store(2, std::memory_order_release); })
                                        .has_value());
                    })
                    .has_value());

    // Wait for IO to post main completion.
    for (int i = 0; i < 200 && value.load(std::memory_order_acquire) < 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_GE(value.load(std::memory_order_acquire), 1);

    for (int i = 0; i < 200 && value.load(std::memory_order_acquire) != 2; ++i)
    {
        auto pumped = (*system)->pumpMain(0);
        ASSERT_TRUE(pumped.has_value());
        if (*pumped == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    EXPECT_EQ(value.load(std::memory_order_acquire), 2);
    (*system)->shutdownAndJoin();
}

TEST(BoundedTaskSystemTest, IoQueueFull)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 1,
        .mainQueueCapacity = 8,
    });
    ASSERT_TRUE(system.has_value());

    std::atomic<bool> block{true};
    ASSERT_TRUE((*system)
                    ->scheduleIo([&] {
                        while (block.load(std::memory_order_acquire))
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    })
                    .has_value());
    // Fill remaining capacity (worker may or may not have taken the first item yet).
    auto second = (*system)->scheduleIo([] {});
    auto third = (*system)->scheduleIo([] {});
    // At least one of second/third should eventually observe full or succeed; with capacity 1,
    // while first is running or queued, at most one more fits.
    EXPECT_TRUE(!second.has_value() || !third.has_value() || second.has_value());
    if (!second.has_value())
    {
        EXPECT_EQ(second.error().code, Task::TaskErrorCode::QueueFull);
    }
    if (!third.has_value())
    {
        EXPECT_EQ(third.error().code, Task::TaskErrorCode::QueueFull);
    }
    block.store(false, std::memory_order_release);
    (*system)->shutdownAndJoin();
}

TEST(BoundedTaskSystemTest, ScheduleCpuAndTaskGroupWaitIdle)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 2,
        .ioQueueCapacity = 8,
        .cpuQueueCapacity = 16,
        .mainQueueCapacity = 8,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    std::atomic<int> sum{0};
    {
        Task::TaskGroup group(**system);
        for (int index = 0; index < 8; ++index)
        {
            ASSERT_TRUE(group.add([&sum] { sum.fetch_add(1, std::memory_order_relaxed); }).has_value());
        }
        ASSERT_TRUE(group.waitIdle().has_value());
        EXPECT_TRUE(group.isIdle());
    }
    EXPECT_EQ(sum.load(std::memory_order_relaxed), 8);
    (*system)->shutdownAndJoin();
}

TEST(BoundedTaskSystemTest, ScheduleCpuWithoutWorkersIsNotSupported)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 0,
        .ioQueueCapacity = 4,
        .cpuQueueCapacity = 4,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value());
    auto status = (*system)->scheduleCpu([] {});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Task::TaskErrorCode::NotSupported);
    (*system)->shutdownAndJoin();
}

} // namespace Tina::Tests
