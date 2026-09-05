#pragma once

#include <tina/core/base/CancellationSignal.hpp>
#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/task/TaskGroup.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace Tina {

// State-owned structured task capability.
//
// A scope is created with a GameState and is destroyed with that state. CPU work
// receives a non-owning cancellation token and the generation it was accepted
// under. Completions never enter the global TaskSystem main queue: they remain in
// this scope until the owner thread pumps them, which makes the scope the lifetime
// barrier for callbacks that capture state-owned data.
class StateTaskScope final {
  public:
    using Worker = Core::MoveOnlyFunction<void(Core::CancellationToken, Core::u64)>;
    using Completion = Core::MoveOnlyFunction<void()>;

    static constexpr Core::usize DefaultCompletionCapacity = 64;

    StateTaskScope(Task::ITaskSystem& taskSystem, std::thread::id ownerThread,
                   Core::usize completionCapacity = DefaultCompletionCapacity) noexcept;
    ~StateTaskScope() noexcept;

    StateTaskScope(const StateTaskScope&) = delete;
    StateTaskScope& operator=(const StateTaskScope&) = delete;
    StateTaskScope(StateTaskScope&&) = delete;
    StateTaskScope& operator=(StateTaskScope&&) = delete;

    // Schedules CPU work under the current state generation. Only the owner
    // thread may accept new work. The worker must poll the supplied token.
    [[nodiscard]] Core::Status scheduleCpu(Worker work);

    // Queues an owner-thread callback if generation is still current. This may
    // be called by a worker or by the owner thread. A stale/closed generation is
    // rejected and must be treated as a normal cancellation result.
    [[nodiscard]] Core::Status postCompletion(Core::u64 generation, Completion completion);

    // Executes queued callbacks on the owner thread. Each callback is checked
    // against the generation again immediately before invocation.
    [[nodiscard]] Core::Result<Core::u32> pumpCompletions(Core::u32 budget = 0);

    [[nodiscard]] Core::CancellationToken cancellationToken() const noexcept
    {
        return Core::CancellationToken{m_cancellation};
    }

    [[nodiscard]] Core::u64 generation() const noexcept
    {
        return m_generation.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isCurrent(Core::u64 generationValue) const noexcept
    {
        return m_active.load(std::memory_order_acquire) &&
               generationValue != 0 && generationValue == generation();
    }

    [[nodiscard]] bool cancellationRequested() const noexcept
    {
        return m_cancellation.cancellationRequested();
    }

    // Lifecycle-only operation used by EngineHost before onExit/destruction.
    // It is idempotent and never detaches accepted work.
    void cancelAndJoin() noexcept;

  private:
    struct QueuedCompletion final {
        Core::u64 generation = 0;
        Completion work{};
    };

    [[nodiscard]] bool onOwnerThread() const noexcept;
    void invalidateGeneration() noexcept;

    Task::TaskGroup m_taskGroup;
    Core::CancellationSignal m_cancellation;
    std::thread::id m_ownerThread{};
    const Core::usize m_completionCapacity;
    std::atomic<Core::u64> m_generation{1};
    std::atomic<bool> m_active{true};
    mutable std::mutex m_completionMutex;
    std::deque<QueuedCompletion> m_completions;
};

} // namespace Tina
