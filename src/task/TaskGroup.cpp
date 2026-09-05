#include <tina/task/TaskGroup.hpp>

#include <tina/task/TaskErrors.hpp>

#include <limits>
#include <utility>

namespace Tina::Task {
TaskGroup::TaskGroup(ITaskSystem& system) noexcept : m_system(&system) {}

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

    auto status = m_system->scheduleCpu([this, work = std::move(work)]() mutable {
        try
        {
            if (work)
            {
                work();
            }
        } catch (...)
        {
            // Keep worker/group alive; surface errors via host diagnostics later.
        }
        onWorkFinished();
    });
    if (!status)
    {
        // Rejected work never reaches onWorkFinished, so this call owns the
        // rollback. Same protocol as completion: mutate under the lock, notify
        // after releasing it.
        bool becameIdle = false;
        {
            std::scoped_lock lock(m_mutex);
            const auto current = m_pending.load(std::memory_order_relaxed);
            if (current != 0U)
            {
                m_pending.store(current - 1U, std::memory_order_release);
                becameIdle = current == 1U;
            }
        }
        if (becameIdle)
        {
            m_cv.notify_all();
        }
        return status;
    }
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

Core::Status TaskGroup::waitIdleFor(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    const bool satisfied = m_cv.wait_for(
        lock, timeout, [this] { return m_pending.load(std::memory_order_relaxed) == 0U; });
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
    bool becameIdle = false;
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
        becameIdle = current == 1U;
    }
    // Notify with the mutex released: a waiter woken while this thread still held
    // it would immediately block again on reacquiring it.
    if (becameIdle)
    {
        m_cv.notify_all();
    }
}

} // namespace Tina::Task
