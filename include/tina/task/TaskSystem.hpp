#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>

#include <memory>

namespace Tina::Task {

struct TaskSystemCreateParams final {
    Core::u32 ioWorkerCount = 1;
    // 0 selects the interactive CPU default at factory creation time. This
    // prevents a direct createBoundedTaskSystem() call from silently producing
    // an IO-only system that later rejects every scheduleCpu() request.
    Core::u32 cpuWorkerCount = 0;
    Core::usize ioQueueCapacity = 64;
    Core::usize cpuQueueCapacity = 64;
    Core::usize mainQueueCapacity = 64;
    // Explicit opt-out for an IO/Main-only graph. This is intentionally
    // separate from cpuWorkerCount==0 so a missing worker count is useful by
    // default rather than a latent NotSupported failure.
    bool disableCpuWorkers = false;
};

// ADR 0017 interactive default: reserve one hardware thread for Main when possible.
[[nodiscard]] constexpr Core::u32 interactiveCpuWorkerCount(Core::u32 hardwareConcurrency) noexcept
{
    return hardwareConcurrency <= 1U ? 1U : hardwareConcurrency - 1U;
}

// Applies Desktop host defaults to a factory param snapshot (IO/Main queues + interactive CPU).
// Explicit non-zero cpuWorkerCount is preserved; 0 becomes interactiveCpuWorkerCount(...).
// disableCpuWorkers is the only way to request an IO/Main-only system.
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
    if (effective.disableCpuWorkers)
    {
        effective.cpuWorkerCount = 0;
    }
    else if (effective.cpuWorkerCount == 0)
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
using TaskCallable = Core::MoveOnlyFunction<void()>;

class ITaskSystem {
  public:
    virtual ~ITaskSystem() = default;

    [[nodiscard]] virtual bool isIdle() const noexcept = 0;
    [[nodiscard]] virtual bool isStopping() const noexcept = 0;

    // Blocking IO domain. Runs on IO worker thread(s). Must not touch World/UI/RenderDevice.
    [[nodiscard]] virtual Core::Status scheduleIo(TaskCallable work) = 0;

    // CPU domain. Runs on CPU worker thread(s). Must not block on disk or touch World/UI/RenderDevice.
    // Returns NotSupported only when TaskSystemCreateParams::disableCpuWorkers
    // explicitly requested an IO/Main-only system.
    [[nodiscard]] virtual Core::Status scheduleCpu(TaskCallable work) = 0;

    // Main-thread completion domain. Only drained by pumpMain() on the owner thread.
    [[nodiscard]] virtual Core::Status postMain(TaskCallable work) = 0;

    // Drain up to budget main-thread tasks. budget==0 drains all currently queued.
    // Returns number of tasks executed.
    [[nodiscard]] virtual Core::Result<Core::u32> pumpMain(Core::u32 budget = 0) = 0;

    virtual void requestStop() noexcept = 0;

    // deadline must be finite and greater than zero. Success means every worker exited,
    // was joined, and all queues were cleared. WaitTimeout leaves the system stopping
    // with its workers and queues alive so the owner can retry; implementations never
    // detach or forcibly terminate workers.
    [[nodiscard]] virtual Core::Status shutdownAndJoinFor(Core::Duration deadline) noexcept = 0;

    // Indefinite shutdown using the same worker-exit observation and finalization path.
    virtual void shutdownAndJoin() noexcept = 0;
};

using TaskSystemFactory =
    Core::MoveOnlyFunction<Core::Result<std::unique_ptr<ITaskSystem>>(const TaskSystemCreateParams&)>;

} // namespace Tina::Task
