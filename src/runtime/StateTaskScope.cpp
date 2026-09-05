#include <tina/runtime/StateTaskScope.hpp>

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/task/TaskErrors.hpp>

#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace Tina {

StateTaskScope::StateTaskScope(Task::ITaskSystem& taskSystem, std::thread::id ownerThread,
                               Core::usize completionCapacity) noexcept
    : m_taskGroup(taskSystem), m_ownerThread(ownerThread), m_completionCapacity(completionCapacity)
{
}

StateTaskScope::~StateTaskScope() noexcept
{
    cancelAndJoin();
}

bool StateTaskScope::onOwnerThread() const noexcept
{
    return std::this_thread::get_id() == m_ownerThread;
}

void StateTaskScope::invalidateGeneration() noexcept
{
    m_active.store(false, std::memory_order_release);
    Core::u64 current = m_generation.load(std::memory_order_relaxed);
    while (current != (std::numeric_limits<Core::u64>::max)() &&
           !m_generation.compare_exchange_weak(current, current + 1U, std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
    {
    }
}

Core::Status StateTaskScope::scheduleCpu(Worker work)
{
    if (!work)
    {
        return Core::failure(Task::TaskErrorCode::InvalidArgument,
                             "StateTaskScope::scheduleCpu requires non-empty work");
    }
    if (!onOwnerThread())
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "StateTaskScope::scheduleCpu must execute on the state owner thread");
    }
    if (!m_active.load(std::memory_order_acquire) || m_cancellation.cancellationRequested())
    {
        return Core::failure(Task::TaskErrorCode::TaskSystemStopped,
                             "StateTaskScope is closed for new work");
    }

    const Core::u64 acceptedGeneration = generation();
    try
    {
        return m_taskGroup.add([this, acceptedGeneration, work = std::move(work)]() mutable {
            if (!m_active.load(std::memory_order_acquire) ||
                m_cancellation.cancellationRequested())
            {
                return;
            }
            if (work)
            {
                work(Core::CancellationToken{m_cancellation}, acceptedGeneration);
            }
        });
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "StateTaskScope::scheduleCpu allocation failed");
    } catch (const std::exception&)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "StateTaskScope::scheduleCpu failed while accepting work");
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "StateTaskScope::scheduleCpu failed while accepting work");
    }
}

Core::Status StateTaskScope::postCompletion(Core::u64 completionGeneration, Completion completion)
{
    if (!completion)
    {
        return Core::failure(Task::TaskErrorCode::InvalidArgument,
                             "StateTaskScope::postCompletion requires non-empty completion");
    }
    if (!isCurrent(completionGeneration))
    {
        return Core::failure(Task::TaskErrorCode::TaskSystemStopped,
                             "StateTaskScope completion generation is stale or closed");
    }

    try
    {
        std::scoped_lock lock(m_completionMutex);
        // Recheck after taking the lock: cancellation can race with a worker
        // that passed the first generation check.
        if (!isCurrent(completionGeneration))
        {
            return Core::failure(Task::TaskErrorCode::TaskSystemStopped,
                                 "StateTaskScope completion generation is stale or closed");
        }
        if (m_completions.size() >= m_completionCapacity)
        {
            return Core::failure(Task::TaskErrorCode::QueueFull,
                                 "StateTaskScope completion queue is full");
        }
        m_completions.push_back(QueuedCompletion{
            .generation = completionGeneration,
            .work = std::move(completion),
        });
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "StateTaskScope completion queue allocation failed");
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "StateTaskScope completion queue insertion failed");
    }
    return Core::success();
}

Core::Result<Core::u32> StateTaskScope::pumpCompletions(Core::u32 budget)
{
    if (!onOwnerThread())
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "StateTaskScope::pumpCompletions must execute on the state owner thread");
    }

    Core::u32 processed = 0;
    while (budget == 0 || processed < budget)
    {
        QueuedCompletion queued{};
        {
            std::scoped_lock lock(m_completionMutex);
            if (m_completions.empty())
            {
                break;
            }
            queued = std::move(m_completions.front());
            m_completions.pop_front();
        }

        if (!isCurrent(queued.generation) || !queued.work)
        {
            continue;
        }
        try
        {
            queued.work();
        } catch (const std::bad_alloc&)
        {
            std::scoped_lock lock(m_completionMutex);
            m_completions.clear();
            return Core::failure(Core::CoreErrorCode::OutOfMemory,
                                 "StateTaskScope completion threw std::bad_alloc");
        } catch (const std::exception&)
        {
            std::scoped_lock lock(m_completionMutex);
            m_completions.clear();
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "StateTaskScope completion threw an exception");
        } catch (...)
        {
            std::scoped_lock lock(m_completionMutex);
            m_completions.clear();
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "StateTaskScope completion threw a non-standard exception");
        }
        ++processed;
    }
    return processed;
}

void StateTaskScope::cancelAndJoin() noexcept
{
    // Invalidate first so a racing worker can never publish a callback under the
    // generation that is being torn down. The signal then asks cooperative work
    // to stop, and TaskGroup is the join barrier for every accepted worker item.
    invalidateGeneration();
    m_cancellation.requestCancellation();
    (void)m_taskGroup.waitIdle();
    std::scoped_lock lock(m_completionMutex);
    m_completions.clear();
}

} // namespace Tina
