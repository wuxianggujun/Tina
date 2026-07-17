#pragma once

#include <tina/core/error/Error.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/FrameTiming.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/runtime/RunExitReason.hpp>

namespace Tina::Detail {
class EngineHostImplementation;
}

namespace Tina {

class GameStartupContext final {
  public:
    GameStartupContext(const GameStartupContext&) = delete;
    GameStartupContext& operator=(const GameStartupContext&) = delete;
    GameStartupContext(GameStartupContext&&) = delete;
    GameStartupContext& operator=(GameStartupContext&&) = delete;

    [[nodiscard]] const EngineConfig& engineConfig() const noexcept;
    // Callback-only facade. Store returned subscription tokens, never this address.
    [[nodiscard]] PlatformEventSubscriptions& platformEventSubscriptions() noexcept;

  private:
    GameStartupContext(const EngineConfig& config, PlatformEventDispatcher& platformEvents) noexcept;

    const EngineConfig* m_config = nullptr;
    PlatformEventSubscriptions m_platformEventSubscriptions;

    friend class Detail::EngineHostImplementation;
};

class GameStateEnterContext final {
  public:
    GameStateEnterContext(const GameStateEnterContext&) = delete;
    GameStateEnterContext& operator=(const GameStateEnterContext&) = delete;
    GameStateEnterContext(GameStateEnterContext&&) = delete;
    GameStateEnterContext& operator=(GameStateEnterContext&&) = delete;

    [[nodiscard]] const EngineConfig& engineConfig() const noexcept;
    // Callback-only facade. Store returned subscription tokens, never this address.
    [[nodiscard]] PlatformEventSubscriptions& platformEventSubscriptions() noexcept;

  private:
    GameStateEnterContext(const EngineConfig& config, PlatformEventDispatcher& platformEvents) noexcept;

    const EngineConfig* m_config = nullptr;
    PlatformEventSubscriptions m_platformEventSubscriptions;

    friend class Detail::EngineHostImplementation;
};

class FixedUpdateContext final {
  public:
    FixedUpdateContext(const FixedUpdateContext&) = delete;
    FixedUpdateContext& operator=(const FixedUpdateContext&) = delete;
    FixedUpdateContext(FixedUpdateContext&&) = delete;
    FixedUpdateContext& operator=(FixedUpdateContext&&) = delete;

    [[nodiscard]] const FrameTiming& frameTiming() const noexcept;
    [[nodiscard]] const FixedUpdateTiming& fixedUpdateTiming() const noexcept;
    [[nodiscard]] const SimulationActionSnapshot& simulationActions() const noexcept;

  private:
    FixedUpdateContext(const FrameTiming& frameTiming, const FixedUpdateTiming& fixedUpdateTiming,
                       const SimulationActionSnapshot& simulationActions) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    const FixedUpdateTiming* m_fixedUpdateTiming = nullptr;
    const SimulationActionSnapshot* m_simulationActions = nullptr;

    friend class Detail::EngineHostImplementation;
};

class FrameUpdateContext final {
  public:
    FrameUpdateContext(const FrameUpdateContext&) = delete;
    FrameUpdateContext& operator=(const FrameUpdateContext&) = delete;
    FrameUpdateContext(FrameUpdateContext&&) = delete;
    FrameUpdateContext& operator=(FrameUpdateContext&&) = delete;

    [[nodiscard]] const FrameTiming& frameTiming() const noexcept;
    [[nodiscard]] const FrameActionSnapshot& frameActions() const noexcept;
    void requestExitAfterFrame() noexcept;

  private:
    FrameUpdateContext(const FrameTiming& frameTiming, const FrameActionSnapshot& frameActions,
                       bool& exitRequested) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    const FrameActionSnapshot* m_frameActions = nullptr;
    bool* m_exitRequested = nullptr;

    friend class Detail::EngineHostImplementation;
};

class RenderSceneExtractionContext final {
  public:
    RenderSceneExtractionContext(const RenderSceneExtractionContext&) = delete;
    RenderSceneExtractionContext& operator=(const RenderSceneExtractionContext&) = delete;
    RenderSceneExtractionContext(RenderSceneExtractionContext&&) = delete;
    RenderSceneExtractionContext& operator=(RenderSceneExtractionContext&&) = delete;

    [[nodiscard]] const FrameTiming& frameTiming() const noexcept;

  private:
    explicit RenderSceneExtractionContext(const FrameTiming& frameTiming) noexcept;

    const FrameTiming* m_frameTiming = nullptr;

    friend class Detail::EngineHostImplementation;
};

class UIUpdateContext final {
  public:
    UIUpdateContext(const UIUpdateContext&) = delete;
    UIUpdateContext& operator=(const UIUpdateContext&) = delete;
    UIUpdateContext(UIUpdateContext&&) = delete;
    UIUpdateContext& operator=(UIUpdateContext&&) = delete;

    [[nodiscard]] const FrameTiming& frameTiming() const noexcept;

  private:
    explicit UIUpdateContext(const FrameTiming& frameTiming) noexcept;

    const FrameTiming* m_frameTiming = nullptr;

    friend class Detail::EngineHostImplementation;
};

class GameStateExitContext final {
  public:
    GameStateExitContext(const GameStateExitContext&) = delete;
    GameStateExitContext& operator=(const GameStateExitContext&) = delete;
    GameStateExitContext(GameStateExitContext&&) = delete;
    GameStateExitContext& operator=(GameStateExitContext&&) = delete;

    [[nodiscard]] RunStopCause stopCause() const noexcept;
    // Callback-only borrow. The pointed Error, when present, must not be stored.
    [[nodiscard]] const Core::Error* runtimeFailure() const noexcept;

  private:
    GameStateExitContext(RunStopCause stopCause, const Core::Error* runtimeFailure) noexcept;

    RunStopCause m_stopCause = RunStopCause::RuntimeFailure;
    const Core::Error* m_runtimeFailure = nullptr;

    friend class Detail::EngineHostImplementation;
};

class GameShutdownContext final {
  public:
    GameShutdownContext(const GameShutdownContext&) = delete;
    GameShutdownContext& operator=(const GameShutdownContext&) = delete;
    GameShutdownContext(GameShutdownContext&&) = delete;
    GameShutdownContext& operator=(GameShutdownContext&&) = delete;

    [[nodiscard]] RunStopCause stopCause() const noexcept;
    // Callback-only borrow. The pointed Error, when present, must not be stored.
    [[nodiscard]] const Core::Error* runtimeFailure() const noexcept;

  private:
    GameShutdownContext(RunStopCause stopCause, const Core::Error* runtimeFailure) noexcept;

    RunStopCause m_stopCause = RunStopCause::RuntimeFailure;
    const Core::Error* m_runtimeFailure = nullptr;

    friend class Detail::EngineHostImplementation;
};

} // namespace Tina
