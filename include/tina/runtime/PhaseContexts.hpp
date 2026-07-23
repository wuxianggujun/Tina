#pragma once

#include <tina/audio/AudioEngine.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/FrameTiming.hpp>
#include <tina/runtime/GameStateCommands.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <memory>

// GameState.hpp is included via GameStateCommands.hpp (IGameState + GameStatePolicy).

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
    // Queries availability without creating a sticky phase error. The builder
    // itself expires unconditionally when onEnter returns.
    [[nodiscard]] bool hasPrimaryWindowUI() const noexcept;
    [[nodiscard]] Core::Result<PrimaryWindowUIRootBuilder> primaryWindowUIRootBuilder();

  private:
    GameStateEnterContext(const EngineConfig& config, PlatformEventDispatcher& platformEvents,
                          Runtime::Detail::PrimaryWindowUICapabilityState& primaryWindowUI, u64 uiEpoch) noexcept;

    const EngineConfig* m_config = nullptr;
    PlatformEventSubscriptions m_platformEventSubscriptions;
    Runtime::Detail::PrimaryWindowUICapabilityState* m_primaryWindowUI = nullptr;
    u64 m_uiEpoch = 0;

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
    // Non-null only when EngineCompositionFactories::createAudioEngine was set.
    // Phase-local borrow; do not store across callbacks.
    [[nodiscard]] Audio::AudioEngine* audioEngine() const noexcept;

  private:
    FixedUpdateContext(const FrameTiming& frameTiming, const FixedUpdateTiming& fixedUpdateTiming,
                       const SimulationActionSnapshot& simulationActions,
                       Audio::AudioEngine* audioEngine) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    const FixedUpdateTiming* m_fixedUpdateTiming = nullptr;
    const SimulationActionSnapshot* m_simulationActions = nullptr;
    Audio::AudioEngine* m_audioEngine = nullptr;

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
    // Phase-local borrow; null when Audio factory was omitted.
    [[nodiscard]] Audio::AudioEngine* audioEngine() const noexcept;
    void requestExitAfterFrame() noexcept;

    // Deferred stack commands (ADR 0014). Commit is EngineHost-only after updateFrame.
    [[nodiscard]] Core::Status requestPush(std::unique_ptr<IGameState> state);
    [[nodiscard]] Core::Status requestPop();
    [[nodiscard]] Core::Status requestReplace(std::unique_ptr<IGameState> state);
    [[nodiscard]] Core::Status requestPolicyChange(GameStatePolicy policy);

  private:
    FrameUpdateContext(const FrameTiming& frameTiming, const FrameActionSnapshot& frameActions,
                       bool& exitRequested, GameStatePendingCommands* pendingCommands,
                       Audio::AudioEngine* audioEngine) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    const FrameActionSnapshot* m_frameActions = nullptr;
    bool* m_exitRequested = nullptr;
    GameStatePendingCommands* m_pendingCommands = nullptr;
    Audio::AudioEngine* m_audioEngine = nullptr;

    friend class Detail::EngineHostImplementation;
};

class RenderSceneExtractionContext final {
  public:
    RenderSceneExtractionContext(const RenderSceneExtractionContext&) = delete;
    RenderSceneExtractionContext& operator=(const RenderSceneExtractionContext&) = delete;
    RenderSceneExtractionContext(RenderSceneExtractionContext&&) = delete;
    RenderSceneExtractionContext& operator=(RenderSceneExtractionContext&&) = delete;

    [[nodiscard]] const FrameTiming& frameTiming() const noexcept;
    // The writer is valid only during this callback. It can add resolved
    // Camera2D/Sprite2D items but cannot publish or resize the frame storage.
    [[nodiscard]] Render::RenderSceneWriter& renderSceneWriter() noexcept;

  private:
    RenderSceneExtractionContext(const FrameTiming& frameTiming,
                                 Render::RenderSceneWriter& renderSceneWriter) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    Render::RenderSceneWriter* m_renderSceneWriter = nullptr;

    friend class Detail::EngineHostImplementation;
};

class UIUpdateContext final {
  public:
    UIUpdateContext(const UIUpdateContext&) = delete;
    UIUpdateContext& operator=(const UIUpdateContext&) = delete;
    UIUpdateContext(UIUpdateContext&&) = delete;
    UIUpdateContext& operator=(UIUpdateContext&&) = delete;

    [[nodiscard]] const FrameTiming& frameTiming() const noexcept;
    // Queries availability without creating a sticky phase error. The updater
    // is bound to rootOwner and expires unconditionally when updateUI returns.
    [[nodiscard]] bool hasPrimaryWindowUI() const noexcept;
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater> primaryWindowUITreeUpdater(UI::UIRootOwner& rootOwner);
    // Last committed semantics for accessibility rebuild (owner-thread, phase-scoped).
    // Reflects the most recent commitLayout (startup or previous frame), not in-progress edits.
    [[nodiscard]] Core::Result<UI::UICommittedSemanticsView> committedSemantics() const;

  private:
    UIUpdateContext(const FrameTiming& frameTiming, Runtime::Detail::PrimaryWindowUICapabilityState& primaryWindowUI,
                    u64 uiEpoch) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    Runtime::Detail::PrimaryWindowUICapabilityState* m_primaryWindowUI = nullptr;
    u64 m_uiEpoch = 0;

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
