#pragma once

#include <tina/ai/Blackboard.hpp>

#include <memory>
#include <memory_resource>
#include <span>

namespace Tina::AI {

enum class StateMachineState : Core::u8 { Idle, Running, Succeeded, Failed, Cancelled, Faulted };
enum class StateDecisionKind : Core::u8 { Stay, Transition, Succeed, Fail };

struct StateDecision final {
    StateDecisionKind kind = StateDecisionKind::Stay;
    Core::u32 target = 0;
    static constexpr StateDecision stay() noexcept { return {}; }
    static constexpr StateDecision transition(Core::u32 state) noexcept
    { return {.kind = StateDecisionKind::Transition, .target = state}; }
    static constexpr StateDecision succeed() noexcept
    { return {.kind = StateDecisionKind::Succeed}; }
    static constexpr StateDecision fail() noexcept
    { return {.kind = StateDecisionKind::Fail}; }
};

using StateEnter = Core::Status (*)(Blackboard&, void* userData);
using StateTick = Core::Result<StateDecision> (*)(Blackboard&, double deltaSeconds, void* userData);
using StateExit = void (*)(Blackboard&, void* userData) noexcept;

struct StateDesc final {
    StateEnter enter = nullptr;
    StateTick tick = nullptr;
    StateExit exit = nullptr;
    void* userData = nullptr;
};

struct StateMachineTickResult final {
    StateMachineState state = StateMachineState::Idle;
    Core::u32 activeState = 0;
    Core::usize transitions = 0;
};

class StateMachine final {
public:
    [[nodiscard]] static Core::Result<StateMachine> Create(
        std::span<const StateDesc> states, Core::u32 initialState = 0,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
    ~StateMachine() noexcept;
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;
    StateMachine(StateMachine&& other) noexcept;
    StateMachine& operator=(StateMachine&&) = delete;

    [[nodiscard]] Core::Result<StateMachineTickResult> tick(
        Blackboard& blackboard, double deltaSeconds, Core::usize transitionBudget = 8);
    [[nodiscard]] Core::Status cancel();
    [[nodiscard]] Core::Status reset();
    [[nodiscard]] StateMachineState state() const noexcept { return m_state; }
    [[nodiscard]] Core::u32 activeState() const noexcept { return m_activeState; }
    [[nodiscard]] Core::usize stateCount() const noexcept;

private:
    struct Storage;
    static void destroyStorage(Storage*) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    StateMachine(StorageOwner, Core::u32 initial) noexcept;
    void fault() noexcept;
    StorageOwner m_storage{nullptr, &destroyStorage};
    Blackboard* m_blackboard = nullptr;
    Core::u32 m_initialState = 0;
    Core::u32 m_activeState = 0;
    StateMachineState m_state = StateMachineState::Idle;
    bool m_dispatching = false;
};

} // namespace Tina::AI
