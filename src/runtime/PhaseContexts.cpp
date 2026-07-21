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
                                       bool& exitRequested, Audio::AudioEngine* audioEngine) noexcept
    : m_frameTiming(&frameTiming),
      m_frameActions(&frameActions),
      m_exitRequested(&exitRequested),
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
