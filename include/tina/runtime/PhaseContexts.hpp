#pragma once

#include <tina/audio/AudioEngine.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/FrameTiming.hpp>
#include <tina/runtime/GameStateCommands.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/ui/UIContextStatistics.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UILayoutDebugger.hpp>

#include <memory>

// GameState.hpp is included via GameStateCommands.hpp (IGameState + GameStatePolicy).

namespace Tina::Detail {
class EngineHostImplementation;
}

namespace Tina {

class FrameUpdateContext;

// Phase-local handle for player-facing display options. It deliberately exposes
// only the settings a game menu may change at runtime, not the RenderDevice
// itself, so backend lifecycle and resource APIs stay out of GameState reach.
// Copyable and cheap; never outlive the phase that handed it out.
class DisplaySettings final {
  public:
    constexpr DisplaySettings() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_device != nullptr;
    }

    // No-op when the device is absent. The backend applies the change no later
    // than the next present(), so a toggle is not guaranteed to affect the frame
    // that requested it.
    void setVsyncEnabled(bool enabled) const noexcept
    {
        if (m_device != nullptr)
        {
            m_device->setVsyncEnabled(enabled);
        }
    }

    // Reports the requested state, which the backend may not have applied yet.
    // Defaults to true when no device is present.
    [[nodiscard]] bool vsyncEnabled() const noexcept
    {
        return m_device == nullptr || m_device->vsyncEnabled();
    }

  private:
    friend class Detail::EngineHostImplementation;
    friend class FrameUpdateContext;

    explicit constexpr DisplaySettings(Render::IRenderDevice* device) noexcept : m_device(device) {}

    Render::IRenderDevice* m_device = nullptr;
};

// Phase-local handle for the gameplay time scale. Simulation time is scaled
// before it reaches the fixed-step accumulator, so this drives slow motion,
// hitstop and pause without any state having to reinterpret its own deltas.
// Copyable and cheap; never outlive the phase that handed it out.
class TimeScaleSettings final {
  public:
    constexpr TimeScaleSettings() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_timeScale != nullptr;
    }

    // Rejects non-finite and negative values so an invalid scale cannot reach
    // the accumulator and fail the frame. Zero is legal and freezes simulation:
    // fixedUpdate stops receiving steps while updateFrame keeps running, which
    // is what a pause menu wants.
    [[nodiscard]] Core::Status setTimeScale(double timeScale) const noexcept;

    // Defaults to 1.0 when no owner is present.
    [[nodiscard]] double timeScale() const noexcept
    {
        return m_timeScale == nullptr ? 1.0 : *m_timeScale;
    }

  private:
    friend class Detail::EngineHostImplementation;
    friend class FrameUpdateContext;

    explicit constexpr TimeScaleSettings(double* timeScale) noexcept : m_timeScale(timeScale) {}

    double* m_timeScale = nullptr;
};

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
    // Phase-local top-state authority for transactional Action rebinding. Lower
    // GameStates receive null and cannot mutate the global binding map.
    [[nodiscard]] InputActionRebinding* inputActionRebinding() noexcept;
    // Player-facing display options. Empty when no render device is present.
    [[nodiscard]] DisplaySettings displaySettings() const noexcept;
    // Gameplay time scale authority, restricted to the top GameState so a paused
    // layer below cannot fight the state that owns the pause.
    [[nodiscard]] TimeScaleSettings timeScaleSettings() const noexcept;
    void requestExitAfterFrame() noexcept;

    // Deferred stack commands (ADR 0014). Commit is EngineHost-only after updateFrame.
    [[nodiscard]] Core::Status requestPush(std::unique_ptr<IGameState> state);
    [[nodiscard]] Core::Status requestPop();
    [[nodiscard]] Core::Status requestReplace(std::unique_ptr<IGameState> state);
    [[nodiscard]] Core::Status requestPolicyChange(GameStatePolicy policy);

  private:
    FrameUpdateContext(const FrameTiming& frameTiming, const FrameActionSnapshot& frameActions,
                       bool& exitRequested, GameStatePendingCommands* pendingCommands,
                       Audio::AudioEngine* audioEngine, Runtime::Input::ActionMapper* actionMapper,
                       Render::IRenderDevice* renderDevice, double* gameplayTimeScale) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    const FrameActionSnapshot* m_frameActions = nullptr;
    bool* m_exitRequested = nullptr;
    GameStatePendingCommands* m_pendingCommands = nullptr;
    Audio::AudioEngine* m_audioEngine = nullptr;
    Render::IRenderDevice* m_renderDevice = nullptr;
    double* m_gameplayTimeScale = nullptr;
    InputActionRebinding m_inputActionRebinding;
    bool m_rebindingAvailable = false;

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
    // Packet-local resource sink. Refs issued here are valid only for the
    // RenderFramePacket currently being extracted and submitted.
    [[nodiscard]] Render::FrameResourceSink& frameResourceSink() noexcept;

  private:
    RenderSceneExtractionContext(const FrameTiming& frameTiming,
                                 Render::RenderSceneWriter& renderSceneWriter,
                                 Render::FrameResourceSink& frameResourceSink) noexcept;

    const FrameTiming* m_frameTiming = nullptr;
    Render::RenderSceneWriter* m_renderSceneWriter = nullptr;
    Render::FrameResourceSink* m_frameResourceSink = nullptr;

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
    // Owner-thread diagnostic snapshot for bounded UI storage. The result is
    // phase-scoped and does not expose the UIContext itself.
    [[nodiscard]] Core::Result<UI::UIContextStatistics> primaryWindowUIStatistics() const;
    [[nodiscard]] Core::Result<UI::UILayoutDebugOptions> primaryWindowUILayoutDebugOptions() const;
    [[nodiscard]] Core::Status setPrimaryWindowUILayoutDebugOptions(UI::UILayoutDebugOptions options);
    [[nodiscard]] Core::Result<UI::UILayoutDebugSnapshotView> committedLayoutDebugSnapshot() const;
    [[nodiscard]] Core::Result<UI::UIPointerHitQueryResult>
    queryCommittedPrimaryWindowUIPointerHit(UI::UILogicalPoint point) const;

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
