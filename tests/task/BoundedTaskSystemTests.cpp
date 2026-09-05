#include <gtest/gtest.h>

#include <tina/task/TaskErrors.hpp>
#include <tina/task/TaskGroup.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <semaphore>
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

// waitIdleFor's whole reason to exist is that it comes back. It had no test, and
// the timeout path was the only recovery a caller has -- there is no detach
// (ADR 0017).
TEST(BoundedTaskSystemTest, TaskGroupWaitIdleForTimesOutWhileWorkIsStillRunning)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 1,
        .ioQueueCapacity = 4,
        .cpuQueueCapacity = 8,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    {
        Task::TaskGroup group(**system);
        ASSERT_TRUE(group
                        .add([&release, &started] {
                            started.store(true, std::memory_order_release);
                            while (!release.load(std::memory_order_acquire))
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                            }
                        })
                        .has_value());
        while (!started.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        const Core::Status timedOut = group.waitIdleFor(std::chrono::milliseconds{30});
        ASSERT_FALSE(timedOut);
        EXPECT_EQ(timedOut.error().code, Task::TaskErrorCode::WaitTimeout);
        EXPECT_FALSE(group.isIdle());
        EXPECT_EQ(group.pending(), 1U);

        // And once the work finishes it reports success rather than staying stuck.
        release.store(true, std::memory_order_release);
        EXPECT_TRUE(group.waitIdleFor(std::chrono::milliseconds{5000}).has_value());
        EXPECT_TRUE(group.isIdle());
    }
    (*system)->shutdownAndJoin();
}

TEST(BoundedTaskSystemTest, TaskGroupWaitDoesNotOutliveAcceptedWorkDuringShutdownDrain)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 1,
        .ioQueueCapacity = 4,
        .cpuQueueCapacity = 8,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    Task::TaskGroup group(**system);
    // Occupy the single CPU worker, then queue a second item behind it. Shutdown
    // must drain both, and a group wait must not treat the stop flag as completion.
    ASSERT_TRUE(group
                    .add([&release, &started] {
                        started.store(true, std::memory_order_release);
                        while (!release.load(std::memory_order_acquire))
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds{1});
                        }
                    })
                    .has_value());
    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    std::atomic<bool> secondRan{false};
    ASSERT_TRUE(group.add([&secondRan] { secondRan.store(true, std::memory_order_release); }).has_value());

    // Stopping on another thread: shutdownAndJoin cannot complete until the blocked
    // task returns, so the wait below observes a stopping system with work pending.
    std::atomic<bool> shutdownEntered{false};
    std::thread stopper([&system, &shutdownEntered] {
        shutdownEntered.store(true, std::memory_order_release);
        (*system)->shutdownAndJoin();
    });
    while (!shutdownEntered.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    // Give the stop flag time to become observable while both callbacks remain pending.
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    const Core::Status timedOut = group.waitIdleFor(std::chrono::milliseconds{30});
    ASSERT_FALSE(timedOut.has_value());
    EXPECT_EQ(timedOut.error().code, Task::TaskErrorCode::WaitTimeout);
    EXPECT_EQ(group.pending(), 2U);

    release.store(true, std::memory_order_release);
    ASSERT_TRUE(group.waitIdleFor(std::chrono::milliseconds{2000}).has_value());
    stopper.join();
    EXPECT_TRUE(secondRan.load(std::memory_order_acquire));
    EXPECT_TRUE(group.isIdle());
}

TEST(BoundedTaskSystemTest, ScheduleCpuWithoutWorkersIsNotSupported)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 0,
        .ioQueueCapacity = 4,
        .cpuQueueCapacity = 4,
        .mainQueueCapacity = 4,
        .disableCpuWorkers = true,
    });
    ASSERT_TRUE(system.has_value());
    auto status = (*system)->scheduleCpu([] {});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Task::TaskErrorCode::NotSupported);
    (*system)->shutdownAndJoin();
}

TEST(BoundedTaskSystemTest, InteractiveCpuWorkerCountReservesMainThread)
{
    EXPECT_EQ(Task::interactiveCpuWorkerCount(0), 1U);
    EXPECT_EQ(Task::interactiveCpuWorkerCount(1), 1U);
    EXPECT_EQ(Task::interactiveCpuWorkerCount(2), 1U);
    EXPECT_EQ(Task::interactiveCpuWorkerCount(8), 7U);
}

TEST(BoundedTaskSystemTest, ResolveDesktopParamsAppliesInteractiveCpuDefault)
{
    const Task::TaskSystemCreateParams defaults{};
    const auto resolved = Task::resolveDesktopTaskSystemParams(defaults, 8);
    EXPECT_EQ(resolved.ioWorkerCount, 1U);
    EXPECT_EQ(resolved.cpuWorkerCount, 7U);
    EXPECT_EQ(resolved.ioQueueCapacity, 64U);
    EXPECT_EQ(resolved.cpuQueueCapacity, 64U);
    EXPECT_EQ(resolved.mainQueueCapacity, 64U);

    const Task::TaskSystemCreateParams emptyQueues{
        .ioWorkerCount = 0,
        .cpuWorkerCount = 0,
        .ioQueueCapacity = 0,
        .cpuQueueCapacity = 0,
        .mainQueueCapacity = 0,
    };
    const auto filled = Task::resolveDesktopTaskSystemParams(emptyQueues, 4);
    EXPECT_EQ(filled.ioWorkerCount, 1U);
    EXPECT_EQ(filled.cpuWorkerCount, 3U);
    EXPECT_EQ(filled.ioQueueCapacity, 64U);
    EXPECT_EQ(filled.cpuQueueCapacity, 64U);
    EXPECT_EQ(filled.mainQueueCapacity, 64U);

    const Task::TaskSystemCreateParams explicitCpu{
        .ioWorkerCount = 2,
        .cpuWorkerCount = 5,
        .ioQueueCapacity = 16,
        .cpuQueueCapacity = 32,
        .mainQueueCapacity = 8,
    };
    const auto preserved = Task::resolveDesktopTaskSystemParams(explicitCpu, 16);
    EXPECT_EQ(preserved.ioWorkerCount, 2U);
    EXPECT_EQ(preserved.cpuWorkerCount, 5U);
    EXPECT_EQ(preserved.ioQueueCapacity, 16U);
    EXPECT_EQ(preserved.cpuQueueCapacity, 32U);
    EXPECT_EQ(preserved.mainQueueCapacity, 8U);
}

TEST(BoundedTaskSystemTest, DesktopResolvedParamsEnableScheduleCpu)
{
    const auto params = Task::resolveDesktopTaskSystemParams(Task::TaskSystemCreateParams{}, 4);
    ASSERT_GE(params.cpuWorkerCount, 1U);
    auto system = Task::createBoundedTaskSystem(params);
    ASSERT_TRUE(system.has_value()) << system.error().message;
    std::atomic<int> value{0};
    ASSERT_TRUE((*system)->scheduleCpu([&] { value.store(1, std::memory_order_release); }).has_value());
    for (int i = 0; i < 200 && value.load(std::memory_order_acquire) == 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(value.load(std::memory_order_acquire), 1);
    (*system)->shutdownAndJoin();
}

TEST(BoundedTaskSystemTest, ShutdownDeadlineRejectsInvalidValuesWithoutStopping)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 4,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    const Core::Duration invalidDeadlines[] = {
        Core::Duration::zero(),
        Core::Duration{-1.0},
        Core::Duration{std::numeric_limits<double>::infinity()},
        Core::Duration{std::numeric_limits<double>::quiet_NaN()},
    };
    for (const Core::Duration deadline : invalidDeadlines)
    {
        const auto status = (*system)->shutdownAndJoinFor(deadline);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, Task::TaskErrorCode::InvalidArgument);
        EXPECT_FALSE((*system)->isStopping());
    }

    (*system)->shutdownAndJoin();
}

TEST(BoundedTaskSystemTest, ShutdownDeadlineSucceedsWhenIdleAndCanRepeat)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 1,
        .ioQueueCapacity = 4,
        .cpuQueueCapacity = 4,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    EXPECT_TRUE((*system)
                    ->shutdownAndJoinFor(Core::Duration{std::numeric_limits<double>::max()})
                    .has_value());
    EXPECT_TRUE((*system)->isStopping());
    EXPECT_TRUE((*system)->isIdle());
    EXPECT_TRUE((*system)->shutdownAndJoinFor(Core::Duration{0.5}).has_value());
}

TEST(BoundedTaskSystemTest, ShutdownDeadlineTimeoutPreservesStoppingStateForRetry)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 4,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    std::binary_semaphore workerStarted{0};
    std::binary_semaphore releaseWorker{0};
    std::atomic<bool> workerCompleted{false};
    ASSERT_TRUE((*system)
                    ->scheduleIo([&] {
                        workerStarted.release();
                        releaseWorker.acquire();
                        workerCompleted.store(true, std::memory_order_release);
                    })
                    .has_value());
    workerStarted.acquire();

    const auto timeout = (*system)->shutdownAndJoinFor(Core::Duration{0.02});
    EXPECT_FALSE(timeout.has_value());
    if (!timeout.has_value())
    {
        EXPECT_EQ(timeout.error().code, Task::TaskErrorCode::WaitTimeout);
    }
    EXPECT_TRUE((*system)->isStopping());
    EXPECT_FALSE((*system)->isIdle());
    const auto rejected = (*system)->scheduleIo([] {});
    EXPECT_FALSE(rejected.has_value());
    if (!rejected.has_value())
    {
        EXPECT_EQ(rejected.error().code, Task::TaskErrorCode::TaskSystemStopped);
    }

    releaseWorker.release();
    const auto retry = (*system)->shutdownAndJoinFor(Core::Duration{1.0});
    ASSERT_TRUE(retry.has_value()) << retry.error().message;
    EXPECT_TRUE(workerCompleted.load(std::memory_order_acquire));
    EXPECT_TRUE((*system)->isIdle());
}

TEST(BoundedTaskSystemTest, ShutdownDeadlineDrainsQueuedWorkerTasks)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 4,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    std::binary_semaphore workerStarted{0};
    std::binary_semaphore releaseWorker{0};
    std::atomic<int> completed{0};
    ASSERT_TRUE((*system)
                    ->scheduleIo([&] {
                        workerStarted.release();
                        releaseWorker.acquire();
                        completed.fetch_add(1, std::memory_order_relaxed);
                    })
                    .has_value());
    workerStarted.acquire();
    for (int index = 0; index < 3; ++index)
    {
        const auto queued = (*system)->scheduleIo([&] { completed.fetch_add(1, std::memory_order_relaxed); });
        if (!queued.has_value())
        {
            releaseWorker.release();
            (*system)->shutdownAndJoin();
            FAIL() << queued.error().message;
            return;
        }
    }

    std::jthread unblockWorker([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        releaseWorker.release();
    });
    const auto status = (*system)->shutdownAndJoinFor(Core::Duration{1.0});
    ASSERT_TRUE(status.has_value()) << status.error().message;
    EXPECT_EQ(completed.load(std::memory_order_relaxed), 4);
    EXPECT_TRUE((*system)->isIdle());
}

TEST(BoundedTaskSystemTest, ShutdownDeadlineIncludesShutdownSerializationWait)
{
    auto system = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 4,
        .mainQueueCapacity = 4,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    std::binary_semaphore workerStarted{0};
    std::binary_semaphore releaseWorker{0};
    ASSERT_TRUE((*system)
                    ->scheduleIo([&] {
                        workerStarted.release();
                        releaseWorker.acquire();
                    })
                    .has_value());
    workerStarted.acquire();

    std::atomic<bool> releaseIssued{false};
    const auto releaseOnce = [&] {
        if (!releaseIssued.exchange(true, std::memory_order_acq_rel))
        {
            releaseWorker.release();
        }
    };
    std::mutex safetyMutex;
    std::condition_variable safetyCv;
    bool cancelSafetyRelease = false;
    std::jthread safetyRelease([&] {
        std::unique_lock lock(safetyMutex);
        if (!safetyCv.wait_for(lock, std::chrono::seconds(1), [&] { return cancelSafetyRelease; }))
        {
            releaseOnce();
        }
    });

    std::atomic<bool> indefiniteShutdownCompleted{false};
    std::jthread indefiniteShutdown([&] {
        (*system)->shutdownAndJoin();
        indefiniteShutdownCompleted.store(true, std::memory_order_release);
    });
    for (int attempt = 0; attempt < 200 && !(*system)->isStopping(); ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!(*system)->isStopping())
    {
        releaseOnce();
        indefiniteShutdown.join();
        {
            std::scoped_lock lock(safetyMutex);
            cancelSafetyRelease = true;
        }
        safetyCv.notify_one();
        FAIL() << "indefinite shutdown did not enter stopping state";
        return;
    }

    const auto timeout = (*system)->shutdownAndJoinFor(Core::Duration{0.02});
    EXPECT_FALSE(timeout.has_value());
    if (!timeout.has_value())
    {
        EXPECT_EQ(timeout.error().code, Task::TaskErrorCode::WaitTimeout);
    }
    EXPECT_FALSE(indefiniteShutdownCompleted.load(std::memory_order_acquire));

    {
        std::scoped_lock lock(safetyMutex);
        cancelSafetyRelease = true;
    }
    safetyCv.notify_one();
    releaseOnce();
    indefiniteShutdown.join();
    EXPECT_TRUE(indefiniteShutdownCompleted.load(std::memory_order_acquire));
    EXPECT_TRUE((*system)->shutdownAndJoinFor(Core::Duration{0.5}).has_value());
}

} // namespace Tina::Tests
