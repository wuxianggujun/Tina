#include <tina/runtime/PhaseContexts.hpp>

#include "input/ActionMapper.hpp"
#include "ui/PrimaryWindowUICapabilityState.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <cmath>
#include <utility>

namespace Tina {

Core::Result<RebindTransaction>
InputActionRebinding::begin(InputBindingId binding,
                            std::optional<Platform::GamepadId> capturedGamepad)
{
    if (mapper_ == nullptr)
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Input Action rebinding is unavailable in this Runtime phase");
    }
    return mapper_->beginRebind(binding, capturedGamepad);
}

Core::Result<RebindCommitResult>
InputActionRebinding::commit(RebindTransaction transaction, ActionBindingPattern replacement,
                             RebindConflictPolicy conflictPolicy)
{
    if (mapper_ == nullptr)
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Input Action rebinding is unavailable in this Runtime phase");
    }
    return mapper_->commitRebind(transaction, std::move(replacement), conflictPolicy);
}

Core::Status InputActionRebinding::cancel(RebindTransaction transaction) noexcept
{
    return mapper_ == nullptr
               ? Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                               "Input Action rebinding is unavailable in this Runtime phase")
               : mapper_->cancelRebind(transaction);
}

RebindStateView InputActionRebinding::state() const noexcept
{
    return mapper_ == nullptr ? RebindStateView{} : mapper_->rebindState();
}

std::span<const InputActionBinding> InputActionRebinding::bindings() const noexcept
{
    return mapper_ == nullptr ? std::span<const InputActionBinding>{} : mapper_->bindings();
}

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
                                       Audio::AudioEngine* audioEngine,
                                       Runtime::Input::ActionMapper* actionMapper,
                                       Render::IRenderDevice* renderDevice,
                                       double* gameplayTimeScale,
                                       Platform::IPlatformBackend* platformBackend,
                                       Platform::PointerCaptureMode* pointerCaptureMode) noexcept
    : m_frameTiming(&frameTiming),
      m_frameActions(&frameActions),
      m_exitRequested(&exitRequested),
      m_pendingCommands(pendingCommands),
      m_audioEngine(audioEngine),
      m_renderDevice(renderDevice),
      m_gameplayTimeScale(gameplayTimeScale),
      m_platformBackend(platformBackend),
      m_pointerCaptureMode(pointerCaptureMode),
      m_inputActionRebinding(actionMapper),
      m_rebindingAvailable(actionMapper != nullptr)
{
}

Core::Status PointerCaptureSettings::setMode(Platform::PointerCaptureMode mode) const noexcept
{
    if (m_backend == nullptr || m_mode == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PhaseCapabilityUnavailable,
                             "Pointer capture is not available in this phase");
    }
    if (mode == *m_mode)
    {
        return Core::success();
    }
    if (auto status = m_backend->setPointerCaptureMode(mode); !status)
    {
        return status;
    }
    // Recorded only after the backend accepted it, so mode() never reports a mode the
    // window is not actually in.
    *m_mode = mode;
    return Core::success();
}

Core::Status TimeScaleSettings::setTimeScale(double timeScale) const noexcept
{
    if (m_timeScale == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PhaseCapabilityUnavailable,
                             "Gameplay time scale is not available in this phase");
    }
    if (!std::isfinite(timeScale) || timeScale < 0.0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "gameplayTimeScale must be finite and non-negative");
    }
    *m_timeScale = timeScale;
    return Core::success();
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

DisplaySettings FrameUpdateContext::displaySettings() const noexcept
{
    return DisplaySettings{m_renderDevice};
}

TimeScaleSettings FrameUpdateContext::timeScaleSettings() const noexcept
{
    return TimeScaleSettings{m_gameplayTimeScale};
}

PointerCaptureSettings FrameUpdateContext::pointerCaptureSettings() const noexcept
{
    return PointerCaptureSettings{m_platformBackend, m_pointerCaptureMode};
}

InputActionRebinding* FrameUpdateContext::inputActionRebinding() noexcept
{
    return m_rebindingAvailable ? &m_inputActionRebinding : nullptr;
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
    const FrameTiming& frameTiming, Render::RenderSceneWriter& renderSceneWriter,
    Render::FrameResourceSink& frameResourceSink) noexcept
    : m_frameTiming(&frameTiming), m_renderSceneWriter(&renderSceneWriter),
      m_frameResourceSink(&frameResourceSink)
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

Render::FrameResourceSink& RenderSceneExtractionContext::frameResourceSink() noexcept
{
    return *m_frameResourceSink;
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
    return m_primaryWindowUI->committedSemantics(
        m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
}

Core::Result<bool> UIUpdateContext::imeCompositionActive() const
{
    if (m_primaryWindowUI == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PrimaryWindowUIUnavailable,
                             "The active Runtime phase has no primary-window UI");
    }
    return m_primaryWindowUI->imeCompositionActive(
        m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
}

Core::Result<UI::UIContextStatistics> UIUpdateContext::primaryWindowUIStatistics() const
{
    if (m_primaryWindowUI == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PrimaryWindowUIUnavailable,
                             "The active Runtime phase has no primary-window UI");
    }
    return m_primaryWindowUI->statistics(m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
}

Core::Result<UI::UILayoutDebugOptions> UIUpdateContext::primaryWindowUILayoutDebugOptions() const
{
    if (m_primaryWindowUI == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PrimaryWindowUIUnavailable,
                             "The active Runtime phase has no primary-window UI");
    }
    return m_primaryWindowUI->layoutDebugOptions(
        m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
}

Core::Status UIUpdateContext::setPrimaryWindowUILayoutDebugOptions(UI::UILayoutDebugOptions options)
{
    if (m_primaryWindowUI == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PrimaryWindowUIUnavailable,
                             "The active Runtime phase has no primary-window UI");
    }
    return m_primaryWindowUI->setLayoutDebugOptions(
        m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate, options);
}

Core::Result<UI::UILayoutDebugSnapshotView> UIUpdateContext::committedLayoutDebugSnapshot() const
{
    if (m_primaryWindowUI == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PrimaryWindowUIUnavailable,
                             "The active Runtime phase has no primary-window UI");
    }
    return m_primaryWindowUI->committedLayoutDebugSnapshot(
        m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
}

Core::Result<UI::UIPointerHitQueryResult>
UIUpdateContext::queryCommittedPrimaryWindowUIPointerHit(UI::UILogicalPoint point) const
{
    if (m_primaryWindowUI == nullptr)
    {
        return Core::failure(RuntimeErrorCode::PrimaryWindowUIUnavailable,
                             "The active Runtime phase has no primary-window UI");
    }
    return m_primaryWindowUI->queryCommittedPointerHit(
        m_uiEpoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate, point);
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
