#include <tina/task/TaskGroup.hpp>

#include <tina/task/TaskErrors.hpp>

#include <utility>

namespace Tina::Task {
namespace {

// How often an unbounded wait re-checks the system stop flag. Completions notify
// the condition variable directly, so this never delays the normal path; it only
// bounds how long a wait can outlive a task system that stopped with work queued.
constexpr std::chrono::milliseconds StopPollInterval{20};

} // namespace

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

    m_pending.fetch_add(1U, std::memory_order_acq_rel);
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
        // The mutex must be held across the decrement and the notify, exactly as
        // onWorkFinished does it. A waiter evaluates the predicate under the mutex,
        // so an unlocked notify can land between that evaluation and the waiter
        // registering on the condition variable -- the count reaches zero, the
        // wakeup is lost, and nothing will notify again because no work is left.
        {
            std::scoped_lock lock(m_mutex);
            m_pending.fetch_sub(1U, std::memory_order_acq_rel);
        }
        m_cv.notify_all();
        return status;
    }
    return Core::success();
}

bool TaskGroup::isIdle() const noexcept
{
    return m_pending.load(std::memory_order_acquire) == 0U;
}

Core::u32 TaskGroup::pending() const noexcept
{
    return m_pending.load(std::memory_order_acquire);
}

bool TaskGroup::waitSatisfied() const noexcept
{
    if (m_pending.load(std::memory_order_acquire) == 0U)
    {
        return true;
    }
    // A stopping system will not run what is still queued, so continuing to wait
    // for a completion that cannot arrive is an unbounded hang. This is the second
    // exit the header documents; it used to be written as
    // `pending == 0 || (isStopping() && pending == 0)`, which is just `pending == 0`
    // -- so the stop exit did not exist and the failure below was unreachable.
    return m_system != nullptr && m_system->isStopping();
}

Core::Status TaskGroup::waitIdle()
{
    std::unique_lock lock(m_mutex);
    // Re-checked on a timer rather than waited on outright: the task system has no
    // way to notify this group's condition variable when it begins stopping, so a
    // plain wait would only re-evaluate the stop condition if some unrelated
    // completion happened to wake it. Work completing still notifies immediately,
    // so this interval bounds only the stopping case, never the normal one.
    while (!m_cv.wait_for(lock, StopPollInterval, [this] { return waitSatisfied(); }))
    {
    }
    if (m_pending.load(std::memory_order_acquire) != 0U)
    {
        return Core::failure(TaskErrorCode::TaskSystemStopped, "TaskGroup wait interrupted while pending");
    }
    return Core::success();
}

Core::Status TaskGroup::waitIdleFor(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    const bool satisfied = m_cv.wait_for(lock, timeout, [this] { return waitSatisfied(); });
    if (!satisfied)
    {
        return Core::failure(TaskErrorCode::WaitTimeout, "TaskGroup waitIdle timed out");
    }
    // Woken by the stop condition rather than by completion: the work is still
    // counted and will not run, which is a different outcome from a timeout.
    if (m_pending.load(std::memory_order_acquire) != 0U)
    {
        return Core::failure(TaskErrorCode::TaskSystemStopped, "TaskGroup wait interrupted while pending");
    }
    return Core::success();
}

void TaskGroup::onWorkFinished() noexcept
{
    const auto previous = m_pending.fetch_sub(1U, std::memory_order_acq_rel);
    if (previous == 1U)
    {
        std::scoped_lock lock(m_mutex);
        m_cv.notify_all();
    } else if (previous == 0U)
    {
        // Underflow guard: restore zero.
        m_pending.store(0U, std::memory_order_release);
    }
}

} // namespace Tina::Task
