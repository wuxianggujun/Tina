#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <memory>

namespace Tina::Task {
namespace {

class DisabledTaskSystem final : public ITaskSystem {
public:
    [[nodiscard]] bool isIdle() const noexcept override
    {
        return true;
    }

    void shutdownAndJoin() noexcept override
    {
        stopped_ = true;
    }

private:
    [[maybe_unused]] bool stopped_ = false;
};

} // namespace

Core::Result<std::unique_ptr<ITaskSystem>> createDisabledTaskSystem(
    const TaskSystemCreateParams& params)
{
    static_cast<void>(params);

    std::unique_ptr<ITaskSystem> taskSystem = std::make_unique<DisabledTaskSystem>();
    return taskSystem;
}

} // namespace Tina::Task
