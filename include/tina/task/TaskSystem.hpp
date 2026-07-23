#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <functional>
#include <memory>

namespace Tina::Task {

struct TaskSystemCreateParams final {
    Core::u32 ioWorkerCount = 1;
    // 0 disables the CPU worker pool (scheduleCpu → NotSupported). Interactive Desktop
    // (CreateEngine) replaces 0 with interactiveCpuWorkerCount(hardware_concurrency).
    // IO-only slices keep 0 by calling createBoundedTaskSystem directly.
    Core::u32 cpuWorkerCount = 0;
    Core::usize ioQueueCapacity = 64;
    Core::usize cpuQueueCapacity = 64;
    Core::usize mainQueueCapacity = 64;
};

// ADR 0017 interactive default: reserve one hardware thread for Main when possible.
[[nodiscard]] constexpr Core::u32 interactiveCpuWorkerCount(Core::u32 hardwareConcurrency) noexcept
{
    return hardwareConcurrency <= 1U ? 1U : hardwareConcurrency - 1U;
}

// Applies Desktop host defaults to a factory param snapshot (IO/Main queues + interactive CPU).
// Explicit non-zero cpuWorkerCount is preserved; 0 becomes interactiveCpuWorkerCount(...).
// Does not force CPU workers when callers bypass Desktop and createBoundedTaskSystem themselves.
[[nodiscard]] constexpr TaskSystemCreateParams resolveDesktopTaskSystemParams(
    const TaskSystemCreateParams& params,
    Core::u32 hardwareConcurrency) noexcept
{
    TaskSystemCreateParams effective = params;
    if (effective.ioWorkerCount == 0)
    {
        effective.ioWorkerCount = 1;
    }
    if (effective.ioQueueCapacity == 0)
    {
        effective.ioQueueCapacity = 64;
    }
    if (effective.mainQueueCapacity == 0)
    {
        effective.mainQueueCapacity = 64;
    }
    if (effective.cpuWorkerCount == 0)
    {
        effective.cpuWorkerCount = interactiveCpuWorkerCount(hardwareConcurrency);
    }
    if (effective.cpuQueueCapacity == 0)
    {
        effective.cpuQueueCapacity = 64;
    }
    return effective;
}

// Owning, type-erased work item. Prefer small captures; no automatic heap fallback contract.
using TaskCallable = std::move_only_function<void()>;

class ITaskSystem {
  public:
    virtual ~ITaskSystem() = default;

    [[nodiscard]] virtual bool isIdle() const noexcept = 0;
    [[nodiscard]] virtual bool isStopping() const noexcept = 0;

    // Blocking IO domain. Runs on IO worker thread(s). Must not touch World/UI/RenderDevice.
    [[nodiscard]] virtual Core::Status scheduleIo(TaskCallable work) = 0;

    // CPU domain. Runs on CPU worker thread(s). Must not block on disk or touch World/UI/RenderDevice.
    // When the system was created with cpuWorkerCount==0, returns NotSupported.
    [[nodiscard]] virtual Core::Status scheduleCpu(TaskCallable work) = 0;

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
