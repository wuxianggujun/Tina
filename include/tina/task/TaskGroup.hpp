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
// - waitIdle() blocks until pending==0 (or system stop). Does not pump Main.
// - No detach; destruction waits for pending work.
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

    // Wait until pending==0. Returns TaskSystemStopped if the system is stopping with work left
    // un-schedulable, or success when idle.
    [[nodiscard]] Core::Status waitIdle();

    // Wait with timeout. Returns QueueFull? No — use a dedicated timeout code via InvalidArgument
    // path: on timeout returns failure with Core::Timeout if available, else Internal message.
    [[nodiscard]] Core::Status waitIdleFor(std::chrono::milliseconds timeout);

  private:
    void onWorkFinished() noexcept;

    ITaskSystem* m_system = nullptr;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<Core::u32> m_pending{0};
};

} // namespace Tina::Task
