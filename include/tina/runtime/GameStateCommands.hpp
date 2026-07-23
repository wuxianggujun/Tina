#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RuntimeErrors.hpp>

#include <array>
#include <memory>
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
};

class GameStateStack final {
public:
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool full() const noexcept { return m_size >= GameStatePendingCommands::MaxStackDepth; }

    [[nodiscard]] IGameState* top() noexcept
    {
        return empty() ? nullptr : m_entries[m_size - 1U].state.get();
    }

    [[nodiscard]] const IGameState* top() const noexcept
    {
        return empty() ? nullptr : m_entries[m_size - 1U].state.get();
    }

    [[nodiscard]] GameStatePolicy topPolicy() const noexcept
    {
        return empty() ? GameStatePolicy{} : m_entries[m_size - 1U].policy;
    }

    [[nodiscard]] Core::Status pushCommitted(std::unique_ptr<IGameState> state, GameStatePolicy policy) noexcept
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
        ++m_size;
        return Core::success();
    }

    std::unique_ptr<IGameState> popCommitted() noexcept
    {
        if (empty())
        {
            return {};
        }
        --m_size;
        auto state = std::move(m_entries[m_size].state);
        m_entries[m_size].policy = {};
        return state;
    }

    void setTopPolicy(GameStatePolicy policy) noexcept
    {
        if (!empty())
        {
            m_entries[m_size - 1U].policy = policy;
        }
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
