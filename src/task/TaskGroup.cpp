#include <tina/task/TaskGroup.hpp>

#include <tina/task/TaskErrors.hpp>

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
        m_pending.fetch_sub(1U, std::memory_order_acq_rel);
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

Core::Status TaskGroup::waitIdle()
{
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] {
        return m_pending.load(std::memory_order_acquire) == 0U ||
               (m_system != nullptr && m_system->isStopping() && m_pending.load(std::memory_order_acquire) == 0U);
    });
    if (m_pending.load(std::memory_order_acquire) != 0U)
    {
        return Core::failure(TaskErrorCode::TaskSystemStopped, "TaskGroup wait interrupted while pending");
    }
    return Core::success();
}

Core::Status TaskGroup::waitIdleFor(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    const bool done = m_cv.wait_for(lock, timeout, [&] {
        return m_pending.load(std::memory_order_acquire) == 0U;
    });
    if (!done)
    {
        return Core::failure(TaskErrorCode::WaitTimeout, "TaskGroup waitIdle timed out");
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
