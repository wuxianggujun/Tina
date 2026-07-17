#include <tina/runtime/PhaseContexts.hpp>

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

GameStateEnterContext::GameStateEnterContext(const EngineConfig& config,
                                             PlatformEventDispatcher& platformEvents) noexcept
    : m_config(&config), m_platformEventSubscriptions(platformEvents)
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

FixedUpdateContext::FixedUpdateContext(const FrameTiming& frameTiming, const FixedUpdateTiming& fixedUpdateTiming,
                                       const SimulationActionSnapshot& simulationActions) noexcept
    : m_frameTiming(&frameTiming), m_fixedUpdateTiming(&fixedUpdateTiming), m_simulationActions(&simulationActions)
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

FrameUpdateContext::FrameUpdateContext(const FrameTiming& frameTiming, const FrameActionSnapshot& frameActions,
                                       bool& exitRequested) noexcept
    : m_frameTiming(&frameTiming), m_frameActions(&frameActions), m_exitRequested(&exitRequested)
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

void FrameUpdateContext::requestExitAfterFrame() noexcept
{
    *m_exitRequested = true;
}

RenderSceneExtractionContext::RenderSceneExtractionContext(const FrameTiming& frameTiming) noexcept
    : m_frameTiming(&frameTiming)
{
}

const FrameTiming& RenderSceneExtractionContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

UIUpdateContext::UIUpdateContext(const FrameTiming& frameTiming) noexcept : m_frameTiming(&frameTiming)
{
}

const FrameTiming& UIUpdateContext::frameTiming() const noexcept
{
    return *m_frameTiming;
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
