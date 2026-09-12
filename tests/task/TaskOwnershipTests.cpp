#include <gtest/gtest.h>
#include <tina/core/base/Types.hpp>

#include <tina/runtime/StateTaskScope.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/task/TaskErrors.hpp>
#include <tina/task/TaskGroup.hpp>

#include <memory>
#include <memory_resource>
#include <limits>
#include <thread>

namespace Tina::Tests {
namespace {

class ControlledCpuTasks final : public Task::ITaskSystem {
public:
    bool reject = false;
    bool throwOnSchedule = false;
    Task::TaskCallable queued{};
    bool isIdle() const noexcept override { return !queued; }
    bool isStopping() const noexcept override { return false; }
    Core::Status scheduleIo(Task::TaskCallable) override
    { return Core::failure(Task::TaskErrorCode::NotSupported); }
    Core::Status scheduleCpu(Task::TaskCallable work) override
    {
        if (throwOnSchedule) { throw std::bad_alloc{}; }
        if (reject || queued) { return Core::failure(Task::TaskErrorCode::QueueFull); }
        queued = std::move(work);
        return Core::success();
    }
    Core::Status postMain(Task::TaskCallable) override
    { return Core::failure(Task::TaskErrorCode::NotSupported); }
    Core::Result<Core::u32> pumpMain(Core::u32) override { return 0; }
    void requestStop() noexcept override {}
    Core::Status shutdownAndJoinFor(Core::Duration) noexcept override { return Core::success(); }
    void shutdownAndJoin() noexcept override {}
    void execute()
    {
        auto work = std::move(queued);
        if (work) { work(); }
    }
};

class RejectAllocation final : public std::pmr::memory_resource {
    void* do_allocate(Tina::Core::usize, Tina::Core::usize) override { throw std::bad_alloc{}; }
    void do_deallocate(void*, Tina::Core::usize, Tina::Core::usize) override {}
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    { return this == &other; }
};

TEST(TaskGroupOwnershipTest, WrapperAllocationFailureDoesNotPublishPending)
{
    ControlledCpuTasks system;
    RejectAllocation memory;
    Task::TaskGroup group(system, memory);
    const auto result = group.add([] {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Core::CoreErrorCode::OutOfMemory);
    EXPECT_EQ(group.pending(), 0U);
    EXPECT_TRUE(group.waitIdleFor(Core::Duration{0}));
    EXPECT_TRUE(system.isIdle());
}

TEST(TaskGroupOwnershipTest, SchedulerRejectionAndExceptionRollBackExactlyOnce)
{
    ControlledCpuTasks system;
    Task::TaskGroup group(system);
    system.throwOnSchedule = true;
    auto failed = group.add([] {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, Core::CoreErrorCode::OutOfMemory);
    EXPECT_EQ(group.pending(), 0U);
    system.throwOnSchedule = false;
    system.reject = true;
    failed = group.add([] {});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, Task::TaskErrorCode::QueueFull);
    EXPECT_EQ(group.pending(), 0U);
    system.reject = false;
    ASSERT_TRUE(group.add([] {}));
    EXPECT_EQ(group.pending(), 1U);
    system.execute();
    EXPECT_EQ(group.pending(), 0U);
}

TEST(TaskGroupOwnershipTest, CaptureIsDestroyedBeforeIdleIsPublished)
{
    ControlledCpuTasks system;
    Task::TaskGroup group(system);
    Core::u32 pendingAtDestruction = 0;
    bool destroyed = false;
    struct Capture final {
        Task::TaskGroup& group;
        Core::u32& pending;
        bool& destroyed;
        ~Capture() { pending = group.pending(); destroyed = true; }
    };
    auto capture = std::make_unique<Capture>(group, pendingAtDestruction, destroyed);
    ASSERT_TRUE(group.add([capture = std::move(capture)] {}));
    system.execute();
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(pendingAtDestruction, 1U);
    EXPECT_EQ(group.pending(), 0U);
}

TEST(StateTaskScopeOwnershipTest, TimeoutRetainsWorkAndCanBeRetried)
{
    ControlledCpuTasks system;
    StateTaskScope scope(system, std::this_thread::get_id());
    bool workerRan = false;
    bool completionRan = false;
    ASSERT_TRUE(scope.scheduleCpu([&](Core::CancellationToken, Core::u64) { workerRan = true; }));
    ASSERT_TRUE(scope.postCompletion(scope.generation(), [&] { completionRan = true; }));
    const auto generation = scope.generation();
    auto timedOut = scope.cancelAndJoinFor(Core::Duration{0.001});
    ASSERT_FALSE(timedOut);
    EXPECT_EQ(timedOut.error().code, Task::TaskErrorCode::WaitTimeout);
    EXPECT_FALSE(system.isIdle());
    EXPECT_FALSE(scope.isCurrent(generation));
    EXPECT_FALSE(scope.postCompletion(generation, [] {}));
    system.execute();
    EXPECT_FALSE(workerRan);
    EXPECT_TRUE(scope.cancelAndJoinFor(Core::Duration{0.001}));
    const auto closedGeneration = scope.generation();
    EXPECT_TRUE(scope.cancelAndJoinFor(Core::Duration{0.001}));
    EXPECT_EQ(scope.generation(), closedGeneration);
    EXPECT_EQ(scope.pumpCompletions().value(), 0U);
    EXPECT_FALSE(completionRan);
}

TEST(StateTaskScopeOwnershipTest, InvalidDeadlineAndWrongThreadDoNotCancel)
{
    ControlledCpuTasks system;
    StateTaskScope scope(system, std::this_thread::get_id());
    const auto generation = scope.generation();
    for (double seconds : {-1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()})
    {
        EXPECT_FALSE(scope.cancelAndJoinFor(Core::Duration{seconds}));
        EXPECT_TRUE(scope.isCurrent(generation));
    }
    Core::Status status;
    std::thread other([&] { status = scope.cancelAndJoinFor(Core::Duration{0.001}); });
    other.join();
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, RuntimeErrorCode::WrongOwnerThread);
    EXPECT_TRUE(scope.isCurrent(generation));
    EXPECT_TRUE(scope.cancelAndJoinFor(Core::Duration::zero()));
    EXPECT_FALSE(scope.isCurrent(generation));
}

} // namespace
} // namespace Tina::Tests
