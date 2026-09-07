#include <tina/task/TaskGroup.hpp>

#include <tina/task/TaskErrors.hpp>
#include <tina/core/base/ScopeExit.hpp>

#include <limits>
#include <cmath>
#include <memory>
#include <new>
#include <utility>

namespace Tina::Task {
namespace {
struct TaskCallableDeleter final {
    std::pmr::memory_resource* resource;
    void operator()(TaskCallable* callable) const noexcept
    {
        std::pmr::polymorphic_allocator<TaskCallable>{resource}.delete_object(callable);
    }
};
}

TaskGroup::TaskGroup(ITaskSystem& system, std::pmr::memory_resource& wrapperResource) noexcept
    : m_system(&system), m_wrapperResource(&wrapperResource) {}

TaskGroup::~TaskGroup() noexcept
{
    (void)waitIdle();
}

Core::Status TaskGroup::add(TaskCallable work)
{
    if (!work)
    {
        return Core::failure(TaskErrorCode::InvalidArgument, "TaskGroup::add requires non-empty work");
    }
    if (m_system == nullptr)
    {
        return Core::failure(TaskErrorCode::InvalidArgument, "TaskGroup has no task system");
    }

    // Build the wrapper before publishing pending. MoveOnlyFunction may allocate
    // for a large capture; an exception here must not leave a phantom pending item.
    TaskCallable scheduledWork;
    try
    {
        std::unique_ptr<TaskCallable, TaskCallableDeleter> ownedWork{
            std::pmr::polymorphic_allocator<TaskCallable>{m_wrapperResource}.new_object<TaskCallable>(std::move(work)),
            TaskCallableDeleter{m_wrapperResource}};
        scheduledWork = [this, ownedWork = std::move(ownedWork)]() mutable {
            try
            {
                if (*ownedWork) { (*ownedWork)(); }
            } catch (...)
            {
                // Keep worker/group alive; surface errors via host diagnostics later.
            }
            // A completion barrier also covers captured-resource destruction.
            ownedWork.reset();
            onWorkFinished();
        };
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "TaskGroup::add could not allocate its worker wrapper");
    }
    catch (...)
    {
        return Core::failure(TaskErrorCode::InvalidArgument,
                             "TaskGroup::add could not construct its worker wrapper");
    }

    // The increment must be published under the same mutex a waiter evaluates its
    // predicate under. Incrementing outside the lock lets this interleaving happen:
    // the waiter reads pending==0, and before it registers on the condition
    // variable this thread increments, schedules, and the worker completes and
    // notifies. The notify reaches nobody, the waiter then blocks on a count that
    // is already back to zero, and no further work exists to notify it again.
    {
        std::scoped_lock lock(m_mutex);
        const auto current = m_pending.load(std::memory_order_relaxed);
        if (current == (std::numeric_limits<Core::u32>::max)())
        {
            // Refuse rather than wrap. A wrapped count reads as fewer outstanding
            // callbacks than exist, so waitIdle would return while workers still
            // hold this group, and destruction would run under them.
            return Core::failure(TaskErrorCode::QueueFull,
                                 "TaskGroup pending work count is exhausted");
        }
        m_pending.store(current + 1U, std::memory_order_release);
    }

    auto rejectionGuard = Core::makeScopeExit([this]() noexcept { onWorkFinished(); });
    Core::Status status;
    try
    {
        status = m_system->scheduleCpu(std::move(scheduledWork));
    }
    catch (const std::bad_alloc&)
    {
        status = Core::failure(Core::CoreErrorCode::OutOfMemory,
                               "TaskGroup::add schedule allocation failed");
    }
    catch (...)
    {
        status = Core::failure(TaskErrorCode::InvalidArgument,
                               "TaskGroup::add schedule threw an exception");
    }
    if (!status)
    {
        return status;
    }
    rejectionGuard.release();
    return Core::success();
}

bool TaskGroup::isIdle() const noexcept
{
    return pending() == 0U;
}

Core::u32 TaskGroup::pending() const noexcept
{
    // Deliberately lock-free. Locking here would buy nothing and cost something:
    // add() releases the mutex between publishing its increment and rolling that
    // increment back for a rejected schedule, so a locked reader can land in the
    // same window and observe the same transient count. The atomic already rules
    // out a torn read, which is the only thing a lock could add. Meanwhile these
    // observers are noexcept, and a throwing mutex acquisition inside them would
    // terminate the process.
    //
    // These are advisory: a caller needing "no work outstanding" as a guarantee
    // must use waitIdle(), which evaluates the count under the mutex.
    return m_pending.load(std::memory_order_acquire);
}

Core::Status TaskGroup::waitIdle()
{
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this] { return m_pending.load(std::memory_order_relaxed) == 0U; });
    return Core::success();
}

Core::Status TaskGroup::waitIdleFor(Core::Duration timeout)
{
    if (!std::isfinite(timeout.count()) || timeout < Core::Duration::zero())
    {
        return Core::failure(TaskErrorCode::InvalidArgument, "TaskGroup wait requires a finite nonnegative timeout");
    }
    const auto now = Core::MonotonicNativeClock::now();
    const auto remaining = Core::MonotonicTimePoint::max() - now;
    const auto deadline = timeout >= Core::Duration{remaining}
        ? Core::MonotonicTimePoint::max()
        : now + std::chrono::duration_cast<Core::MonotonicNativeClock::duration>(timeout);
    std::unique_lock lock(m_mutex);
    const bool satisfied = m_cv.wait_until(
        lock, deadline, [this] { return m_pending.load(std::memory_order_relaxed) == 0U; });
    if (!satisfied)
    {
        return Core::failure(TaskErrorCode::WaitTimeout, "TaskGroup waitIdle timed out");
    }
    return Core::success();
}

void TaskGroup::onWorkFinished() noexcept
{
    // Decrementing before taking the mutex is the lost-wakeup window: the count
    // can reach zero while a waiter is between evaluating its predicate and
    // registering on the condition variable, and the notify that follows then has
    // no waiter to reach. Publish the new count inside the same critical section
    // the predicate reads it in.
    {
        std::scoped_lock lock(m_mutex);
        const auto current = m_pending.load(std::memory_order_relaxed);
        if (current == 0U)
        {
            // Underflow means a completion arrived without a matching add. Leave
            // the count at zero rather than wrapping to UINT32_MAX, which would
            // hang every subsequent waitIdle forever.
            return;
        }
        m_pending.store(current - 1U, std::memory_order_release);
        if (current == 1U)
        {
            // A waiter may destroy the group once idle. Notify before unlocking
            // so this is the final access to the group's condition variable.
            m_cv.notify_all();
        }
    }
}

} // namespace Tina::Task
