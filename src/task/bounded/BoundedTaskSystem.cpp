#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <tina/task/TaskErrors.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Task {
namespace {

[[nodiscard]] Core::MonotonicTimePoint shutdownDeadlineFromNow(Core::Duration deadline) noexcept
{
    const Core::MonotonicTimePoint now = Core::MonotonicNativeClock::now();
    const Core::MonotonicDuration maximumRemaining = Core::MonotonicTimePoint::max() - now;
    constexpr long double ticksPerSecond =
        static_cast<long double>(Core::MonotonicDuration::period::den) /
        static_cast<long double>(Core::MonotonicDuration::period::num);
    const long double safeMaximumTicks =
        std::nextafter(static_cast<long double>(maximumRemaining.count()), 0.0L);
    const long double requestedSeconds = static_cast<long double>(deadline.count());
    if (!std::isfinite(requestedSeconds) || requestedSeconds >= safeMaximumTicks / ticksPerSecond)
    {
        return Core::MonotonicTimePoint::max();
    }
    const long double requestedTicks = requestedSeconds * ticksPerSecond;
    return now + Core::MonotonicDuration{static_cast<Core::MonotonicDuration::rep>(requestedTicks)};
}

class BoundedTaskSystem final : public ITaskSystem {
  public:
    BoundedTaskSystem(Core::u32 ioWorkerCount, Core::u32 cpuWorkerCount, Core::usize ioQueueCapacity,
                      Core::usize cpuQueueCapacity, Core::usize mainQueueCapacity)
        : m_ioQueueCapacity(ioQueueCapacity), m_cpuQueueCapacity(cpuQueueCapacity),
          m_mainQueueCapacity(mainQueueCapacity)
    {
        try
        {
            m_ioWorkers.reserve(ioWorkerCount);
            for (Core::u32 index = 0; index < ioWorkerCount; ++index)
            {
                m_ioWorkers.emplace_back([this] { ioWorkerLoop(); });
            }
            m_cpuWorkers.reserve(cpuWorkerCount);
            for (Core::u32 index = 0; index < cpuWorkerCount; ++index)
            {
                m_cpuWorkers.emplace_back([this] { cpuWorkerLoop(); });
            }
        } catch (...)
        {
            // A failed std::thread construction leaves earlier workers joinable. Roll them
            // back before vector destruction so the original construction failure can escape.
            shutdownAndJoin();
            throw;
        }
    }

    ~BoundedTaskSystem() override
    {
        shutdownAndJoin();
    }

    [[nodiscard]] bool isIdle() const noexcept override
    {
        std::scoped_lock lock(m_mutex);
        return m_ioQueue.empty() && m_cpuQueue.empty() && m_mainQueue.empty() && m_activeIo == 0U && m_activeCpu == 0U;
    }

    [[nodiscard]] bool isStopping() const noexcept override
    {
        return m_stopping.load(std::memory_order_acquire);
    }

    [[nodiscard]] Core::Status scheduleIo(TaskCallable work) override
    {
        if (!work)
        {
            return Core::failure(TaskErrorCode::InvalidArgument, "scheduleIo requires non-empty work");
        }
        {
            std::scoped_lock lock(m_mutex);
            if (m_stopping.load(std::memory_order_relaxed))
            {
                return Core::failure(TaskErrorCode::TaskSystemStopped, "task system is stopping");
            }
            if (m_ioWorkers.empty())
            {
                return Core::failure(TaskErrorCode::NotSupported, "no IO workers configured");
            }
            if (m_ioQueue.size() >= m_ioQueueCapacity)
            {
                return Core::failure(TaskErrorCode::QueueFull, "IO task queue is full");
            }
            m_ioQueue.push_back(std::move(work));
        }
        m_ioCv.notify_one();
        return Core::success();
    }

    [[nodiscard]] Core::Status scheduleCpu(TaskCallable work) override
    {
        if (!work)
        {
            return Core::failure(TaskErrorCode::InvalidArgument, "scheduleCpu requires non-empty work");
        }
        {
            std::scoped_lock lock(m_mutex);
            if (m_stopping.load(std::memory_order_relaxed))
            {
                return Core::failure(TaskErrorCode::TaskSystemStopped, "task system is stopping");
            }
            if (m_cpuWorkers.empty())
            {
                return Core::failure(TaskErrorCode::NotSupported, "no CPU workers configured");
            }
            if (m_cpuQueue.size() >= m_cpuQueueCapacity)
            {
                return Core::failure(TaskErrorCode::QueueFull, "CPU task queue is full");
            }
            m_cpuQueue.push_back(std::move(work));
        }
        m_cpuCv.notify_one();
        return Core::success();
    }

    [[nodiscard]] Core::Status postMain(TaskCallable work) override
    {
        if (!work)
        {
            return Core::failure(TaskErrorCode::InvalidArgument, "postMain requires non-empty work");
        }
        std::scoped_lock lock(m_mutex);
        if (m_stopping.load(std::memory_order_relaxed))
        {
            return Core::failure(TaskErrorCode::TaskSystemStopped, "task system is stopping");
        }
        if (m_mainQueue.size() >= m_mainQueueCapacity)
        {
            return Core::failure(TaskErrorCode::QueueFull, "main completion queue is full");
        }
        m_mainQueue.push_back(std::move(work));
        return Core::success();
    }

    [[nodiscard]] Core::Result<Core::u32> pumpMain(Core::u32 budget) override
    {
        Core::u32 processed = 0;
        while (true)
        {
            TaskCallable work;
            {
                std::scoped_lock lock(m_mutex);
                if (m_mainQueue.empty())
                {
                    break;
                }
                if (budget != 0U && processed >= budget)
                {
                    break;
                }
                work = std::move(m_mainQueue.front());
                m_mainQueue.pop_front();
            }
            if (work)
            {
                work();
            }
            ++processed;
        }
        return processed;
    }

    void requestStop() noexcept override
    {
        {
            std::scoped_lock lock(m_mutex);
            m_stopping.store(true, std::memory_order_release);
        }
        m_ioCv.notify_all();
        m_cpuCv.notify_all();
    }

    [[nodiscard]] Core::Status shutdownAndJoinFor(Core::Duration deadline) noexcept override
    {
        if (!std::isfinite(deadline.count()) || deadline <= Core::Duration::zero())
        {
            return Core::failure(TaskErrorCode::InvalidArgument,
                                 "shutdownAndJoinFor requires a finite positive deadline");
        }

        const Core::MonotonicTimePoint shutdownDeadline = shutdownDeadlineFromNow(deadline);
        std::unique_lock<std::timed_mutex> shutdownLock(m_shutdownMutex, std::defer_lock);
        if (!shutdownLock.try_lock_until(shutdownDeadline))
        {
            return Core::failure(TaskErrorCode::WaitTimeout, "task system shutdown deadline exceeded");
        }
        requestStop();
        std::unique_lock lock(m_mutex);
        if (!m_workerExitCv.wait_until(lock, shutdownDeadline, [this] { return allWorkersExitedLocked(); }))
        {
            return Core::failure(TaskErrorCode::WaitTimeout, "task system shutdown deadline exceeded");
        }
        lock.unlock();
        finishShutdown();
        return Core::success();
    }

    void shutdownAndJoin() noexcept override
    {
        std::scoped_lock shutdownLock(m_shutdownMutex);
        requestStop();
        std::unique_lock lock(m_mutex);
        m_workerExitCv.wait(lock, [this] { return allWorkersExitedLocked(); });
        lock.unlock();
        finishShutdown();
    }

  private:
    [[nodiscard]] bool allWorkersExitedLocked() const noexcept
    {
        return m_shutdownJoined ||
               m_exitedWorkerCount == static_cast<Core::u32>(m_ioWorkers.size() + m_cpuWorkers.size());
    }

    void finishShutdown() noexcept
    {
        if (m_shutdownJoined)
        {
            return;
        }
        for (auto& worker : m_ioWorkers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_ioWorkers.clear();
        for (auto& worker : m_cpuWorkers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_cpuWorkers.clear();
        std::scoped_lock lock(m_mutex);
        m_ioQueue.clear();
        m_cpuQueue.clear();
        m_mainQueue.clear();
        m_activeIo = 0;
        m_activeCpu = 0;
        m_shutdownJoined = true;
    }

    void ioWorkerLoop()
    {
        workerLoop(m_ioQueue, m_ioCv, m_activeIo);
    }

    void cpuWorkerLoop()
    {
        workerLoop(m_cpuQueue, m_cpuCv, m_activeCpu);
    }

    void workerLoop(std::deque<TaskCallable>& queue, std::condition_variable& cv, Core::u32& activeCounter)
    {
        while (true)
        {
            TaskCallable work;
            {
                std::unique_lock lock(m_mutex);
                cv.wait(lock, [&] { return m_stopping.load(std::memory_order_relaxed) || !queue.empty(); });
                if (m_stopping.load(std::memory_order_relaxed) && queue.empty())
                {
                    break;
                }
                work = std::move(queue.front());
                queue.pop_front();
                ++activeCounter;
            }
            try
            {
                if (work)
                {
                    work();
                }
            } catch (...)
            {
                // Exceptions must not escape worker threads.
            }
            {
                std::scoped_lock lock(m_mutex);
                if (activeCounter > 0U)
                {
                    --activeCounter;
                }
            }
        }
        {
            std::scoped_lock lock(m_mutex);
            ++m_exitedWorkerCount;
        }
        m_workerExitCv.notify_all();
    }

    const Core::usize m_ioQueueCapacity;
    const Core::usize m_cpuQueueCapacity;
    const Core::usize m_mainQueueCapacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_ioCv;
    std::condition_variable m_cpuCv;
    std::condition_variable m_workerExitCv;
    std::deque<TaskCallable> m_ioQueue;
    std::deque<TaskCallable> m_cpuQueue;
    std::deque<TaskCallable> m_mainQueue;
    std::vector<std::thread> m_ioWorkers;
    std::vector<std::thread> m_cpuWorkers;
    Core::u32 m_activeIo = 0;
    Core::u32 m_activeCpu = 0;
    Core::u32 m_exitedWorkerCount = 0;
    bool m_shutdownJoined = false;
    std::timed_mutex m_shutdownMutex;
    std::atomic<bool> m_stopping{false};
};

} // namespace

Core::Result<std::unique_ptr<ITaskSystem>> createBoundedTaskSystem(const TaskSystemCreateParams& params)
{
    if (params.ioWorkerCount == 0 || params.ioQueueCapacity == 0 || params.mainQueueCapacity == 0)
    {
        return Core::failure(TaskErrorCode::InvalidArgument,
                             "bounded task system requires non-zero IO workers and queue capacities");
    }
    if (params.cpuWorkerCount > 0 && params.cpuQueueCapacity == 0)
    {
        return Core::failure(TaskErrorCode::InvalidArgument, "cpuQueueCapacity must be non-zero when CPU workers > 0");
    }
    if (params.ioWorkerCount > 16U || params.cpuWorkerCount > 32U)
    {
        return Core::failure(TaskErrorCode::InvalidArgument, "worker count exceeds first-slice limits");
    }
    try
    {
        std::unique_ptr<ITaskSystem> system = std::make_unique<BoundedTaskSystem>(
            params.ioWorkerCount, params.cpuWorkerCount, params.ioQueueCapacity, params.cpuQueueCapacity,
            params.mainQueueCapacity);
        return system;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "bounded task system allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(Core::CoreErrorCode::Internal, exception.what());
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "bounded task system construction failed");
    }
}

} // namespace Tina::Task
