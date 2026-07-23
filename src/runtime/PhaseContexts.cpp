#include <tina/runtime/PhaseContexts.hpp>

#include "ui/PrimaryWindowUICapabilityState.hpp"

namespace Tina {

GameStartupContext::GameStartupContext(const EngineConfig& config, PlatformEventDispatcher& platformEvents) noexcept
    : m_config(&config), m_platformEventSubscriptions(platformEvents)
{
}

const EngineConfig& GameStartupContext::engineConfig() const noexcept
{
    return *m_config;
}

PlatformEventSubscriptions& GameStartupContext::platformEventSubscriptions() noexcept
{
    return m_platformEventSubscriptions;
}

GameStateEnterContext::GameStateEnterContext(const EngineConfig& config, PlatformEventDispatcher& platformEvents,
                                             Runtime::Detail::PrimaryWindowUICapabilityState& primaryWindowUI,
                                             u64 uiEpoch) noexcept
    : m_config(&config), m_platformEventSubscriptions(platformEvents), m_primaryWindowUI(&primaryWindowUI),
      m_uiEpoch(uiEpoch)
{
}

PlatformEventSubscriptions& GameStateEnterContext::platformEventSubscriptions() noexcept
{
    return m_platformEventSubscriptions;
}

const EngineConfig& GameStateEnterContext::engineConfig() const noexcept
{
    return *m_config;
}

bool GameStateEnterContext::hasPrimaryWindowUI() const noexcept
{
    return m_primaryWindowUI != nullptr &&
           m_primaryWindowUI->hasPrimaryWindowUI(m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter);
}

Core::Result<PrimaryWindowUIRootBuilder> GameStateEnterContext::primaryWindowUIRootBuilder()
{
    return m_primaryWindowUI->rootBuilder(m_uiEpoch);
}

FixedUpdateContext::FixedUpdateContext(const FrameTiming& frameTiming, const FixedUpdateTiming& fixedUpdateTiming,
                                       const SimulationActionSnapshot& simulationActions,
                                       Audio::AudioEngine* audioEngine) noexcept
    : m_frameTiming(&frameTiming),
      m_fixedUpdateTiming(&fixedUpdateTiming),
      m_simulationActions(&simulationActions),
      m_audioEngine(audioEngine)
{
}

const FrameTiming& FixedUpdateContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

const FixedUpdateTiming& FixedUpdateContext::fixedUpdateTiming() const noexcept
{
    return *m_fixedUpdateTiming;
}

const SimulationActionSnapshot& FixedUpdateContext::simulationActions() const noexcept
{
    return *m_simulationActions;
}

Audio::AudioEngine* FixedUpdateContext::audioEngine() const noexcept
{
    return m_audioEngine;
}

FrameUpdateContext::FrameUpdateContext(const FrameTiming& frameTiming, const FrameActionSnapshot& frameActions,
                                       bool& exitRequested, GameStatePendingCommands* pendingCommands,
                                       Audio::AudioEngine* audioEngine) noexcept
    : m_frameTiming(&frameTiming),
      m_frameActions(&frameActions),
      m_exitRequested(&exitRequested),
      m_pendingCommands(pendingCommands),
      m_audioEngine(audioEngine)
{
}

const FrameActionSnapshot& FrameUpdateContext::frameActions() const noexcept
{
    return *m_frameActions;
}

const FrameTiming& FrameUpdateContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

Audio::AudioEngine* FrameUpdateContext::audioEngine() const noexcept
{
    return m_audioEngine;
}

void FrameUpdateContext::requestExitAfterFrame() noexcept
{
    *m_exitRequested = true;
}

Core::Status FrameUpdateContext::requestPush(std::unique_ptr<IGameState> state)
{
    if (m_pendingCommands == nullptr)
    {
        return Core::failure(RuntimeErrorCode::GameStateCommandRejected, "GameState commands unavailable");
    }
    if (state == nullptr)
    {
        return Core::failure(RuntimeErrorCode::InitialGameStateWasNull, "requestPush requires non-null IGameState");
    }
    if (m_pendingCommands->hasStructural())
    {
        return Core::failure(RuntimeErrorCode::GameStateCommandAlreadyQueued,
                             "only one structural GameState command is allowed per frame");
    }
    m_pendingCommands->structural = GameStateStructuralCommandKind::Push;
    m_pendingCommands->candidate = std::move(state);
    ++m_pendingCommands->structuralSequence;
    return Core::success();
}

Core::Status FrameUpdateContext::requestPop()
{
    if (m_pendingCommands == nullptr)
    {
        return Core::failure(RuntimeErrorCode::GameStateCommandRejected, "GameState commands unavailable");
    }
    if (m_pendingCommands->hasStructural())
    {
        return Core::failure(RuntimeErrorCode::GameStateCommandAlreadyQueued,
                             "only one structural GameState command is allowed per frame");
    }
    m_pendingCommands->structural = GameStateStructuralCommandKind::Pop;
    m_pendingCommands->candidate.reset();
    ++m_pendingCommands->structuralSequence;
    return Core::success();
}

Core::Status FrameUpdateContext::requestReplace(std::unique_ptr<IGameState> state)
{
    if (m_pendingCommands == nullptr)
    {
        return Core::failure(RuntimeErrorCode::GameStateCommandRejected, "GameState commands unavailable");
    }
    if (state == nullptr)
    {
        return Core::failure(RuntimeErrorCode::InitialGameStateWasNull, "requestReplace requires non-null IGameState");
    }
    if (m_pendingCommands->hasStructural())
    {
        return Core::failure(RuntimeErrorCode::GameStateCommandAlreadyQueued,
                             "only one structural GameState command is allowed per frame");
    }
    m_pendingCommands->structural = GameStateStructuralCommandKind::Replace;
    m_pendingCommands->candidate = std::move(state);
    ++m_pendingCommands->structuralSequence;
    return Core::success();
}

Core::Status FrameUpdateContext::requestPolicyChange(GameStatePolicy policy)
{
    if (m_pendingCommands == nullptr)
    {
        return Core::failure(RuntimeErrorCode::GameStateCommandRejected, "GameState commands unavailable");
    }
    m_pendingCommands->policyChangeRequested = true;
    m_pendingCommands->requestedPolicy = policy;
    ++m_pendingCommands->policySequence;
    return Core::success();
}

RenderSceneExtractionContext::RenderSceneExtractionContext(
    const FrameTiming& frameTiming, Render::RenderSceneWriter& renderSceneWriter) noexcept
    : m_frameTiming(&frameTiming), m_renderSceneWriter(&renderSceneWriter)
{
}

const FrameTiming& RenderSceneExtractionContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

Render::RenderSceneWriter& RenderSceneExtractionContext::renderSceneWriter() noexcept
{
    return *m_renderSceneWriter;
}

UIUpdateContext::UIUpdateContext(const FrameTiming& frameTiming,
                                 Runtime::Detail::PrimaryWindowUICapabilityState& primaryWindowUI, u64 uiEpoch) noexcept
    : m_frameTiming(&frameTiming), m_primaryWindowUI(&primaryWindowUI), m_uiEpoch(uiEpoch)
{
}

const FrameTiming& UIUpdateContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

bool UIUpdateContext::hasPrimaryWindowUI() const noexcept
{
    return m_primaryWindowUI != nullptr &&
           m_primaryWindowUI->hasPrimaryWindowUI(m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
}

Core::Result<PrimaryWindowUITreeUpdater> UIUpdateContext::primaryWindowUITreeUpdater(UI::UIRootOwner& rootOwner)
{
    return m_primaryWindowUI->treeUpdater(m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate, rootOwner);
}

Core::Result<UI::UICommittedSemanticsView> UIUpdateContext::committedSemantics() const
{
    if (m_primaryWindowUI == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PrimaryWindowUIUnavailable,
                             "The active Runtime phase has no primary-window UI");
    }
    return m_primaryWindowUI->committedSemantics(m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
}

GameStateExitContext::GameStateExitContext(RunStopCause stopCause, const Core::Error* runtimeFailure) noexcept
    : m_stopCause(stopCause), m_runtimeFailure(runtimeFailure)
{
}

RunStopCause GameStateExitContext::stopCause() const noexcept
{
    return m_stopCause;
}

const Core::Error* GameStateExitContext::runtimeFailure() const noexcept
{
    return m_runtimeFailure;
}

GameShutdownContext::GameShutdownContext(RunStopCause stopCause, const Core::Error* runtimeFailure) noexcept
    : m_stopCause(stopCause), m_runtimeFailure(runtimeFailure)
{
}

RunStopCause GameShutdownContext::stopCause() const noexcept
{
    return m_stopCause;
}

const Core::Error* GameShutdownContext::runtimeFailure() const noexcept
{
    return m_runtimeFailure;
}

} // namespace Tina
