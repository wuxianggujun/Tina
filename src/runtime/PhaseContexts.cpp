#include <tina/runtime/PhaseContexts.hpp>

namespace Tina {

GameStartupContext::GameStartupContext(const EngineConfig& config) noexcept
    : m_config(&config)
{
}

const EngineConfig& GameStartupContext::engineConfig() const noexcept
{
    return *m_config;
}

GameStateEnterContext::GameStateEnterContext(const EngineConfig& config) noexcept
    : m_config(&config)
{
}

const EngineConfig& GameStateEnterContext::engineConfig() const noexcept
{
    return *m_config;
}

FixedUpdateContext::FixedUpdateContext(
    const FrameTiming& frameTiming,
    const FixedUpdateTiming& fixedUpdateTiming) noexcept
    : m_frameTiming(&frameTiming), m_fixedUpdateTiming(&fixedUpdateTiming)
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

FrameUpdateContext::FrameUpdateContext(
    const FrameTiming& frameTiming,
    bool& exitRequested) noexcept
    : m_frameTiming(&frameTiming), m_exitRequested(&exitRequested)
{
}

const FrameTiming& FrameUpdateContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

void FrameUpdateContext::requestExitAfterFrame() noexcept
{
    *m_exitRequested = true;
}

RenderSceneExtractionContext::RenderSceneExtractionContext(
    const FrameTiming& frameTiming) noexcept
    : m_frameTiming(&frameTiming)
{
}

const FrameTiming& RenderSceneExtractionContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

UIUpdateContext::UIUpdateContext(const FrameTiming& frameTiming) noexcept
    : m_frameTiming(&frameTiming)
{
}

const FrameTiming& UIUpdateContext::frameTiming() const noexcept
{
    return *m_frameTiming;
}

GameStateExitContext::GameStateExitContext(
    RunStopCause stopCause,
    const Core::Error* runtimeFailure) noexcept
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

GameShutdownContext::GameShutdownContext(
    RunStopCause stopCause,
    const Core::Error* runtimeFailure) noexcept
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
