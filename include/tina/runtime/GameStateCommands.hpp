#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/StateTaskScope.hpp>

#include <array>
#include <memory>
#include <span>
#include <utility>

namespace Tina {

// Structural transition kinds (ADR 0014). At most one structural command is accepted
// per Frame Update; it commits after updateFrame and before extractRenderScene.
enum class GameStateStructuralCommandKind : u8 {
    None = 0,
    Push = 1,
    Pop = 2,
    Replace = 3,
};

// Runtime-private pending slot filled during FrameUpdateContext. Game code only
// queues through FrameUpdateContext; it never mutates the stack directly.
struct GameStatePendingCommands final {
    static constexpr usize MaxStackDepth = 8;

    GameStateStructuralCommandKind structural = GameStateStructuralCommandKind::None;
    std::unique_ptr<IGameState> candidate{};
    bool policyChangeRequested = false;
    GameStatePolicy requestedPolicy{};
    u64 structuralSequence = 0;
    u64 policySequence = 0;

    void clearStructural() noexcept
    {
        structural = GameStateStructuralCommandKind::None;
        candidate.reset();
    }

    void clearPolicy() noexcept
    {
        policyChangeRequested = false;
        requestedPolicy = {};
    }

    void clearAll() noexcept
    {
        clearStructural();
        clearPolicy();
    }

    [[nodiscard]] bool hasStructural() const noexcept
    {
        return structural != GameStateStructuralCommandKind::None;
    }
};

// Fixed-capacity private stack entry (state + committed policy sampled after enter).
struct GameStateStackEntry final {
    std::unique_ptr<IGameState> state{};
    GameStatePolicy policy{};
    std::unique_ptr<StateTaskScope> taskScope{};
};

// Which frame phase is being dispatched top-down (ADR 0014 policy propagation).
enum class GameStateDispatchPhase : u8 {
    FixedUpdate = 0,
    FrameUpdate = 1,
    RenderExtract = 2,
    UIUpdate = 3,
};

[[nodiscard]] constexpr bool policyBlocksBelow(const GameStatePolicy& policy,
                                               GameStateDispatchPhase phase) noexcept
{
    switch (phase)
    {
    case GameStateDispatchPhase::FixedUpdate:
        return policy.blocksFixedUpdateBelow;
    case GameStateDispatchPhase::FrameUpdate:
        return policy.blocksFrameUpdateBelow;
    case GameStateDispatchPhase::RenderExtract:
        return policy.blocksRenderBelow;
    case GameStateDispatchPhase::UIUpdate:
        return policy.blocksUIUpdateBelow;
    }
    return false;
}

class GameStateStack final {
public:
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool full() const noexcept { return m_size >= GameStatePendingCommands::MaxStackDepth; }

    [[nodiscard]] IGameState* top() noexcept
    {
        return empty() ? nullptr : m_entries[m_size - 1U].state.get();
    }

    [[nodiscard]] StateTaskScope* taskScopeForDepth(usize depthFromTop) noexcept
    {
        if (depthFromTop >= m_size)
        {
            return nullptr;
        }
        return m_entries[m_size - 1U - depthFromTop].taskScope.get();
    }

    [[nodiscard]] const StateTaskScope* taskScopeForDepth(usize depthFromTop) const noexcept
    {
        if (depthFromTop >= m_size)
        {
            return nullptr;
        }
        return m_entries[m_size - 1U - depthFromTop].taskScope.get();
    }

    [[nodiscard]] const IGameState* top() const noexcept
    {
        return empty() ? nullptr : m_entries[m_size - 1U].state.get();
    }

    [[nodiscard]] GameStatePolicy topPolicy() const noexcept
    {
        return empty() ? GameStatePolicy{} : m_entries[m_size - 1U].policy;
    }

    // True when any state strictly above depthFromTop sets blocksGameplayInputBelow.
    // depthFromTop==0 (stack top) is never suppressed by this helper.
    [[nodiscard]] bool gameplayInputBlockedForDepth(usize depthFromTop) const noexcept
    {
        if (empty() || depthFromTop == 0)
        {
            return false;
        }
        const usize limit = (std::min)(depthFromTop, m_size);
        for (usize d = 0; d < limit; ++d)
        {
            if (m_entries[m_size - 1U - d].policy.blocksGameplayInputBelow)
            {
                return true;
            }
        }
        return false;
    }

    // Top-down visit until a state whose policy blocks this phase below.
    // visitor(state, policy, depthFromTop) where depthFromTop==0 is the stack top.
    // Stops early if visitor returns a failure Status.
    template <typename Visitor>
    [[nodiscard]] Core::Status forEachDispatch(GameStateDispatchPhase phase, Visitor&& visitor)
    {
        if (empty())
        {
            return Core::success();
        }
        for (usize depthFromTop = 0; depthFromTop < m_size; ++depthFromTop)
        {
            const usize index = m_size - 1U - depthFromTop;
            GameStateStackEntry& entry = m_entries[index];
            if (entry.state == nullptr)
            {
                return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                                     "GameStateStack entry is null during dispatch");
            }
            if (Core::Status status = visitor(*entry.state, entry.policy, depthFromTop); !status)
            {
                return status;
            }
            if (policyBlocksBelow(entry.policy, phase))
            {
                break;
            }
        }
        return Core::success();
    }

    // Collect which stack indices (0=bottom) participate in a phase (for tests).
    [[nodiscard]] usize collectDispatchIndices(GameStateDispatchPhase phase,
                                               std::span<usize> outIndices) const noexcept
    {
        usize written = 0;
        if (empty() || outIndices.empty())
        {
            return 0;
        }
        for (usize depthFromTop = 0; depthFromTop < m_size; ++depthFromTop)
        {
            const usize index = m_size - 1U - depthFromTop;
            if (written < outIndices.size())
            {
                outIndices[written] = index;
                ++written;
            }
            if (policyBlocksBelow(m_entries[index].policy, phase))
            {
                break;
            }
        }
        return written;
    }

    [[nodiscard]] Core::Status pushCommitted(std::unique_ptr<IGameState> state, GameStatePolicy policy,
                                             std::unique_ptr<StateTaskScope> taskScope) noexcept
    {
        if (state == nullptr)
        {
            return Core::failure(RuntimeErrorCode::InitialGameStateWasNull, "GameStateStack push requires non-null state");
        }
        if (full())
        {
            return Core::failure(RuntimeErrorCode::GameStateStackCapacityExceeded, "GameStateStack depth capacity exhausted");
        }
        m_entries[m_size].state = std::move(state);
        m_entries[m_size].policy = policy;
        m_entries[m_size].taskScope = std::move(taskScope);
        ++m_size;
        return Core::success();
    }

    [[nodiscard]] Core::Status pushCommitted(std::unique_ptr<IGameState> state, GameStatePolicy policy) noexcept
    {
        return pushCommitted(std::move(state), policy, nullptr);
    }

    [[nodiscard]] GameStateStackEntry popCommittedEntry() noexcept
    {
        if (empty())
        {
            return {};
        }
        --m_size;
        GameStateStackEntry entry = std::move(m_entries[m_size]);
        m_entries[m_size] = {};
        return entry;
    }

    // Compatibility-free convenience for stack-only users that do not need the
    // task scope. Runtime teardown uses popCommittedEntry() so it can perform the
    // scope cancellation barrier before onExit().
    std::unique_ptr<IGameState> popCommitted() noexcept
    {
        GameStateStackEntry entry = popCommittedEntry();
        if (entry.taskScope != nullptr)
        {
            entry.taskScope->cancelAndJoin();
        }
        auto state = std::move(entry.state);
        return state;
    }

    [[nodiscard]] Core::Status pumpTaskCompletions(Core::u32 budgetPerState = 0)
    {
        for (usize depthFromTop = 0; depthFromTop < m_size; ++depthFromTop)
        {
            StateTaskScope* scope = taskScopeForDepth(depthFromTop);
            if (scope == nullptr)
            {
                continue;
            }
            auto result = scope->pumpCompletions(budgetPerState);
            if (!result)
            {
                return Core::failure(std::move(result.error()));
            }
        }
        return Core::success();
    }

    void setTopPolicy(GameStatePolicy policy) noexcept
    {
        if (!empty())
        {
            m_entries[m_size - 1U].policy = policy;
        }
    }

    // Updates a committed entry without exposing the backing array.  This is
    // used when a top state changes its policy in the same frame that it pushes
    // an overlay: after the push, the requesting state is one level below the
    // new top and must retain the requested propagation rules.
    [[nodiscard]] bool setPolicyAtDepthFromTop(usize depthFromTop, GameStatePolicy policy) noexcept
    {
        if (depthFromTop >= m_size)
        {
            return false;
        }
        m_entries[m_size - 1U - depthFromTop].policy = policy;
        return true;
    }

    void clear() noexcept
    {
        while (!empty())
        {
            (void)popCommitted();
        }
    }

private:
    std::array<GameStateStackEntry, GameStatePendingCommands::MaxStackDepth> m_entries{};
    usize m_size = 0;
};

} // namespace Tina
