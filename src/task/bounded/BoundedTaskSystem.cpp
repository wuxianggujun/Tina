#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <tina/task/TaskErrors.hpp>

#include <atomic>
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

class BoundedTaskSystem final : public ITaskSystem {
  public:
    BoundedTaskSystem(Core::u32 ioWorkerCount, Core::usize ioQueueCapacity, Core::usize mainQueueCapacity)
        : m_ioQueueCapacity(ioQueueCapacity), m_mainQueueCapacity(mainQueueCapacity)
    {
        m_workers.reserve(ioWorkerCount);
        for (Core::u32 index = 0; index < ioWorkerCount; ++index)
        {
            m_workers.emplace_back([this] { workerLoop(); });
        }
    }

    ~BoundedTaskSystem() override
    {
        shutdownAndJoin();
    }

    [[nodiscard]] bool isIdle() const noexcept override
    {
        std::scoped_lock lock(m_mutex);
        return m_ioQueue.empty() && m_mainQueue.empty() && m_activeIo == 0U;
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
            if (m_ioQueue.size() >= m_ioQueueCapacity)
            {
                return Core::failure(TaskErrorCode::QueueFull, "IO task queue is full");
            }
            m_ioQueue.push_back(std::move(work));
        }
        m_ioCv.notify_one();
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
    }

    void shutdownAndJoin() noexcept override
    {
        requestStop();
        for (auto& worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_workers.clear();
        std::scoped_lock lock(m_mutex);
        m_ioQueue.clear();
        m_mainQueue.clear();
        m_activeIo = 0;
    }

  private:
    void workerLoop()
    {
        while (true)
        {
            TaskCallable work;
            {
                std::unique_lock lock(m_mutex);
                m_ioCv.wait(lock, [&] { return m_stopping.load(std::memory_order_relaxed) || !m_ioQueue.empty(); });
                if (m_stopping.load(std::memory_order_relaxed) && m_ioQueue.empty())
                {
                    return;
                }
                work = std::move(m_ioQueue.front());
                m_ioQueue.pop_front();
                ++m_activeIo;
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
                if (m_activeIo > 0U)
                {
                    --m_activeIo;
                }
            }
        }
    }

    const Core::usize m_ioQueueCapacity;
    const Core::usize m_mainQueueCapacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_ioCv;
    std::deque<TaskCallable> m_ioQueue;
    std::deque<TaskCallable> m_mainQueue;
    std::vector<std::thread> m_workers;
    Core::u32 m_activeIo = 0;
    std::atomic<bool> m_stopping{false};
};

} // namespace

Core::Result<std::unique_ptr<ITaskSystem>> createBoundedTaskSystem(const TaskSystemCreateParams& params)
{
    if (params.ioWorkerCount == 0 || params.ioQueueCapacity == 0 || params.mainQueueCapacity == 0)
    {
        return Core::failure(TaskErrorCode::InvalidArgument,
                             "bounded task system requires non-zero workers and queue capacities");
    }
    if (params.ioWorkerCount > 16U)
    {
        return Core::failure(TaskErrorCode::InvalidArgument, "ioWorkerCount exceeds first-slice limit of 16");
    }
    try
    {
        std::unique_ptr<ITaskSystem> system = std::make_unique<BoundedTaskSystem>(
            params.ioWorkerCount, params.ioQueueCapacity, params.mainQueueCapacity);
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
