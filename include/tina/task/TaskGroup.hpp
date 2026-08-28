#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/task/TaskSystem.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Tina::Task {

// Minimal structured group over the CPU domain (ADR 0017).
// - add() schedules work on ITaskSystem::scheduleCpu and tracks pending count.
// - waitIdle() blocks until pending==0 or the system stops. Does not pump Main.
// - No detach; destruction waits for pending work.
//
// add() and the wait functions may be called from different threads; the pending
// count and the condition variable are the synchronisation point.
class TaskGroup final {
  public:
    explicit TaskGroup(ITaskSystem& system) noexcept;
    ~TaskGroup() noexcept;

    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;
    TaskGroup(TaskGroup&&) = delete;
    TaskGroup& operator=(TaskGroup&&) = delete;

    // Schedule CPU work that is counted by this group.
    [[nodiscard]] Core::Status add(TaskCallable work);

    [[nodiscard]] bool isIdle() const noexcept;
    [[nodiscard]] Core::u32 pending() const noexcept;

    // Waits until pending==0, or until the system begins stopping with work that
    // can no longer complete — the latter returns TaskSystemStopped rather than
    // blocking forever. A stopping system is the only reason pending work becomes
    // un-runnable, so without that second exit this call had no way out.
    [[nodiscard]] Core::Status waitIdle();

    // Bounded wait. Returns WaitTimeout if pending work remains when the timeout
    // elapses, TaskSystemStopped if the system stopped with work outstanding, and
    // success when idle.
    //
    // A timeout leaves the group non-idle, and there is no detach (ADR 0017), so
    // the only recovery is to keep waiting or to destroy the group — destruction
    // itself waits, bounded by the same stop condition as waitIdle().
    [[nodiscard]] Core::Status waitIdleFor(std::chrono::milliseconds timeout);

  private:
    void onWorkFinished() noexcept;
    // True once no further completion can arrive: either everything finished, or
    // the system is stopping and whatever is still pending will never run.
    [[nodiscard]] bool waitSatisfied() const noexcept;

    ITaskSystem* m_system = nullptr;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<Core::u32> m_pending{0};
};

} // namespace Tina::Task
