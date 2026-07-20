#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <functional>
#include <memory>

namespace Tina::Task {

struct TaskSystemCreateParams final {
    Core::u32 ioWorkerCount = 1;
    Core::usize ioQueueCapacity = 64;
    Core::usize mainQueueCapacity = 64;
};

// Owning, type-erased work item. Prefer small captures; no automatic heap fallback contract.
using TaskCallable = std::move_only_function<void()>;

class ITaskSystem {
  public:
    virtual ~ITaskSystem() = default;

    [[nodiscard]] virtual bool isIdle() const noexcept = 0;
    [[nodiscard]] virtual bool isStopping() const noexcept = 0;

    // Blocking IO domain. Runs on IO worker thread(s). Must not touch World/UI/RenderDevice.
    [[nodiscard]] virtual Core::Status scheduleIo(TaskCallable work) = 0;

    // Main-thread completion domain. Only drained by pumpMain() on the owner thread.
    [[nodiscard]] virtual Core::Status postMain(TaskCallable work) = 0;

    // Drain up to budget main-thread tasks. budget==0 drains all currently queued.
    // Returns number of tasks executed.
    [[nodiscard]] virtual Core::Result<Core::u32> pumpMain(Core::u32 budget = 0) = 0;

    virtual void requestStop() noexcept = 0;
    virtual void shutdownAndJoin() noexcept = 0;
};

using TaskSystemFactory =
    std::move_only_function<Core::Result<std::unique_ptr<ITaskSystem>>(const TaskSystemCreateParams&)>;

} // namespace Tina::Task
