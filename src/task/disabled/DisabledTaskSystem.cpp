#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <tina/task/TaskErrors.hpp>

#include <cmath>
#include <memory>

namespace Tina::Task {
namespace {

class DisabledTaskSystem final : public ITaskSystem {
  public:
    [[nodiscard]] bool isIdle() const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool isStopping() const noexcept override
    {
        return m_stopped;
    }

    [[nodiscard]] Core::Status scheduleIo(TaskCallable work) override
    {
        static_cast<void>(work);
        return Core::failure(TaskErrorCode::NotSupported, "DisabledTaskSystem has no IO workers");
    }

    [[nodiscard]] Core::Status scheduleCpu(TaskCallable work) override
    {
        static_cast<void>(work);
        return Core::failure(TaskErrorCode::NotSupported, "DisabledTaskSystem has no CPU workers");
    }

    [[nodiscard]] Core::Status postMain(TaskCallable work) override
    {
        static_cast<void>(work);
        return Core::failure(TaskErrorCode::NotSupported, "DisabledTaskSystem has no main queue");
    }

    [[nodiscard]] Core::Result<Core::u32> pumpMain(Core::u32 budget) override
    {
        static_cast<void>(budget);
        return Core::u32{0};
    }

    void requestStop() noexcept override
    {
        m_stopped = true;
    }

    [[nodiscard]] Core::Status shutdownAndJoinFor(Core::Duration deadline) noexcept override
    {
        if (!std::isfinite(deadline.count()) || deadline <= Core::Duration::zero())
        {
            return Core::failure(TaskErrorCode::InvalidArgument,
                                 "shutdownAndJoinFor requires a finite positive deadline");
        }
        m_stopped = true;
        return Core::success();
    }

    void shutdownAndJoin() noexcept override
    {
        m_stopped = true;
    }

  private:
    bool m_stopped = false;
};

} // namespace

Core::Result<std::unique_ptr<ITaskSystem>> createDisabledTaskSystem(const TaskSystemCreateParams& params)
{
    static_cast<void>(params);
    std::unique_ptr<ITaskSystem> taskSystem = std::make_unique<DisabledTaskSystem>();
    return taskSystem;
}

} // namespace Tina::Task
