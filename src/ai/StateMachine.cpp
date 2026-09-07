#include <tina/ai/StateMachine.hpp>
#include "DispatchGuard.hpp"
#include <exception>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::AI {
struct StateMachine::Storage final {
    Storage(std::span<const StateDesc> source, std::pmr::memory_resource& memory)
        : resource(&memory), states(source.begin(), source.end(), &memory) {}
    std::pmr::memory_resource* resource;
    std::pmr::vector<StateDesc> states;
};
void StateMachine::destroyStorage(Storage* storage) noexcept {
    if (storage != nullptr) std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}
StateMachine::StateMachine(StorageOwner storage, Core::u32 initial) noexcept
    : m_storage(std::move(storage)), m_initialState(initial), m_activeState(initial) {}
StateMachine::StateMachine(StateMachine&& other) noexcept
    : m_storage(std::move(other.m_storage)), m_blackboard(std::exchange(other.m_blackboard, nullptr)),
      m_initialState(other.m_initialState), m_activeState(other.m_activeState),
      m_state(std::exchange(other.m_state, StateMachineState::Idle)) {
    if (other.m_dispatching) std::terminate();
}
StateMachine::~StateMachine() noexcept {
    if (m_dispatching) std::terminate();
    if (m_state == StateMachineState::Running && m_storage && m_activeState < m_storage->states.size() && m_storage->states[m_activeState].exit && m_blackboard)
        m_storage->states[m_activeState].exit(*m_blackboard, m_storage->states[m_activeState].userData);
}
Core::Result<StateMachine> StateMachine::Create(std::span<const StateDesc> states, Core::u32 initial,
                                                std::pmr::memory_resource& resource) {
    if (states.empty() || states.size() > 4096 || initial >= states.size())
        return Core::failure(AIErrorCode::InvalidTree, "state machine requires 1..4096 states and an in-range initial state");
    for (const auto& state : states) if (state.tick == nullptr)
        return Core::failure(AIErrorCode::InvalidTree, "state machine state requires a tick callback");
    try {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(states, resource), &destroyStorage};
        return StateMachine(std::move(storage), initial);
    } catch (const std::bad_alloc&) {
        return Core::failure(AIErrorCode::AllocationFailed, "state machine storage allocation failed");
    } catch (const std::exception& exception) {
        return Core::failure(AIErrorCode::AllocationFailed, exception.what());
    }
}
Core::usize StateMachine::stateCount() const noexcept { return m_storage ? m_storage->states.size() : 0; }
void StateMachine::fault() noexcept {
    if (m_state == StateMachineState::Running && m_storage && m_activeState < m_storage->states.size() && m_blackboard) {
        const auto& state = m_storage->states[m_activeState];
        if (state.exit) state.exit(*m_blackboard, state.userData);
    }
    m_blackboard = nullptr; m_state = StateMachineState::Faulted;
}
Core::Result<StateMachineTickResult> StateMachine::tick(Blackboard& blackboard, double delta, Core::usize budget) {
    if (m_dispatching) return Core::failure(AIErrorCode::ReentrantDispatch, "state machine dispatch cannot be reentered");
    if (!m_storage || !blackboard || !std::isfinite(delta) || delta < 0.0 || budget == 0)
        return Core::failure(AIErrorCode::InvalidConfig, "state machine tick requires live owners, finite delta and positive budget");
    if (m_blackboard && m_blackboard != &blackboard) return Core::failure(AIErrorCode::InvalidState, "state machine belongs to another blackboard");
    if (m_state == StateMachineState::Succeeded || m_state == StateMachineState::Failed || m_state == StateMachineState::Cancelled || m_state == StateMachineState::Faulted)
        return StateMachineTickResult{m_state, m_activeState, 0};
    Detail::DispatchGuard guard{m_dispatching};
    try {
        if (m_state == StateMachineState::Idle) {
            m_blackboard = &blackboard; m_state = StateMachineState::Running;
            const auto& state = m_storage->states[m_activeState];
            if (state.enter) { auto entered = state.enter(blackboard, state.userData); if (!entered) { fault(); return Core::failure(std::move(entered.error())); } }
        }
        Core::usize transitions = 0;
        while (transitions < budget) {
            const auto& state = m_storage->states[m_activeState];
            auto decision = state.tick(blackboard, delta, state.userData);
            if (!decision) { fault(); return Core::failure(std::move(decision.error()).withContext("StateMachine::tick", "state callback")); }
            switch (decision->kind) {
            case StateDecisionKind::Stay: return StateMachineTickResult{m_state, m_activeState, transitions};
            case StateDecisionKind::Succeed: if (state.exit) state.exit(blackboard, state.userData); m_blackboard = nullptr; m_state = StateMachineState::Succeeded; return StateMachineTickResult{m_state, m_activeState, transitions + 1};
            case StateDecisionKind::Fail: if (state.exit) state.exit(blackboard, state.userData); m_blackboard = nullptr; m_state = StateMachineState::Failed; return StateMachineTickResult{m_state, m_activeState, transitions + 1};
            case StateDecisionKind::Transition:
                if (decision->target >= m_storage->states.size()) { fault(); return Core::failure(AIErrorCode::InvalidState, "state transition target is out of range"); }
                if (state.exit) state.exit(blackboard, state.userData);
                m_activeState = decision->target; ++transitions;
                if (const auto& next = m_storage->states[m_activeState]; next.enter) { auto entered = next.enter(blackboard, next.userData); if (!entered) { fault(); return Core::failure(std::move(entered.error())); } }
                break;
            default:
                fault();
                return Core::failure(AIErrorCode::CallbackFailed, "state callback returned an invalid decision kind");
            }
        }
        return StateMachineTickResult{m_state, m_activeState, transitions};
    } catch (const std::bad_alloc&) { fault(); return Core::failure(AIErrorCode::AllocationFailed, "state machine callback allocation failed"); }
      catch (...) { fault(); return Core::failure(AIErrorCode::CallbackFailed, "state machine callback threw an exception"); }
}
Core::Status StateMachine::cancel() {
    if (m_dispatching) return Core::failure(AIErrorCode::ReentrantDispatch, "state machine cancellation cannot reenter dispatch");
    Detail::DispatchGuard guard{m_dispatching};
    if (m_state == StateMachineState::Running && m_storage && m_activeState < m_storage->states.size() && m_blackboard) { const auto& s=m_storage->states[m_activeState]; if(s.exit) s.exit(*m_blackboard,s.userData); }
    m_blackboard=nullptr; m_state=StateMachineState::Cancelled; return Core::success();
}
Core::Status StateMachine::reset() {
    if (m_dispatching) return Core::failure(AIErrorCode::ReentrantDispatch, "state machine reset cannot reenter dispatch");
    Detail::DispatchGuard guard{m_dispatching};
    if (m_state == StateMachineState::Running && m_storage && m_activeState < m_storage->states.size() && m_blackboard) { const auto& s=m_storage->states[m_activeState]; if(s.exit) s.exit(*m_blackboard,s.userData); }
    m_blackboard=nullptr; m_activeState=m_initialState; m_state=StateMachineState::Idle; return Core::success();
}
} // namespace Tina::AI
