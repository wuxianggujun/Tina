#include <tina/runtime/EngineHost.hpp>

#include <tina/platform/PlatformBackend.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameStateCommands.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/runtime/spi/PlatformEventDispatcher.hpp>
#include <tina/task/TaskSystem.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/diagnostics/Diagnostics.hpp>

#include "input/ActionMapper.hpp"
#include "input/LastPresentedCamera2DLatch.hpp"
#include "input/UIInputRouteProducer.hpp"
#include "ui/PrimaryWindowUICapabilityState.hpp"
#include "ui/PrimaryWindowUIContextOwner.hpp"
#include "ui/PrimaryWindowUIDisplayCoordinator.hpp"
#include "ui/PrimaryWindowUILayoutCoordinator.hpp"

#include "integration/WindowSurfaceLeaseAccess.hpp"

#if defined(TINA_HAS_UI_UIA)
#include "WindowsUiaHostBridge.hpp"
#include <tina/ui/UIAccessibility.hpp>
#include <Windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <concepts>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace Tina {
namespace {

[[nodiscard]] std::string safeExceptionDetail(const std::exception& exception)
{
    constexpr std::size_t maximumLength = 256;
    const std::string_view source =
        exception.what() != nullptr ? std::string_view(exception.what()) : std::string_view{};

    std::string result;
    result.reserve((std::min)(source.size(), maximumLength));
    std::size_t index = 0;
    while (index < source.size() && result.size() < maximumLength)
    {
        const auto first = static_cast<unsigned char>(source[index]);
        if (first <= 0x7FU)
        {
            result.push_back(first >= 0x20U && first != 0x7FU ? static_cast<char>(first) : '?');
            ++index;
            continue;
        }

        std::size_t encodedLength = 0;
        char32_t codePoint = 0;
        char32_t minimumCodePoint = 0;
        if ((first & 0xE0U) == 0xC0U)
        {
            encodedLength = 2;
            codePoint = first & 0x1FU;
            minimumCodePoint = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U)
        {
            encodedLength = 3;
            codePoint = first & 0x0FU;
            minimumCodePoint = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U)
        {
            encodedLength = 4;
            codePoint = first & 0x07U;
            minimumCodePoint = 0x10000U;
        }

        bool valid = encodedLength != 0 && encodedLength <= source.size() - index;
        for (std::size_t offset = 1; valid && offset < encodedLength; ++offset)
        {
            const auto continuation = static_cast<unsigned char>(source[index + offset]);
            valid = (continuation & 0xC0U) == 0x80U;
            if (valid)
            {
                codePoint = (codePoint << 6U) | (continuation & 0x3FU);
            }
        }
        valid = valid && codePoint >= minimumCodePoint && codePoint <= 0x10FFFFU &&
                !(codePoint >= 0xD800U && codePoint <= 0xDFFFU);

        if (!valid)
        {
            result.push_back('?');
            ++index;
            continue;
        }
        if (encodedLength > maximumLength - result.size())
        {
            break;
        }
        result.append(source.substr(index, encodedLength));
        index += encodedLength;
    }
    return result;
}

[[nodiscard]] Core::Error boundaryException(Core::ErrorCode errorCode, std::string_view operation,
                                            std::string_view detail = {})
{
    Core::Error error{errorCode, "An exception crossed a Tina module boundary"};
    error.addContext(operation, detail);
    return error;
}

template <typename Function>
[[nodiscard]] auto invokeResultBoundary(std::string_view operation, Core::ErrorCode exceptionCode,
                                        Function&& function) -> std::invoke_result_t<Function&>
{
    using ResultType = std::invoke_result_t<Function&>;
    try
    {
        ResultType result = std::invoke(function);
        if (!result)
        {
            auto error = std::move(result.error());
            error.addContext(operation);
            return Core::failure(std::move(error));
        }
        return result;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(boundaryException(Core::CoreErrorCode::OutOfMemory, operation));
    } catch (const std::exception& exception)
    {
        return Core::failure(boundaryException(exceptionCode, operation, safeExceptionDetail(exception)));
    } catch (...)
    {
        return Core::failure(boundaryException(exceptionCode, operation, "non-standard exception"));
    }
}

[[nodiscard]] Core::Error factoryReturnedNull(std::string_view factoryName)
{
    Core::Error error{RuntimeErrorCode::EngineFactoryReturnedNull,
                      "An Engine factory returned success with a null product"};
    error.addContext("EngineHost::Create", factoryName);
    return error;
}

[[nodiscard]] Core::Error initialStateWasNull()
{
    Core::Error error{RuntimeErrorCode::InitialGameStateWasNull,
                      "IGameApplication returned success with a null initial state"};
    error.addContext("EngineHost::run", "IGameApplication::createInitialState");
    return error;
}

[[nodiscard]] std::string framePosition(u64 frameIndex, u64 simulationTick)
{
    return "frame=" + std::to_string(frameIndex) + ", simulationTick=" + std::to_string(simulationTick);
}

template <typename Transition, typename IsReset>
[[nodiscard]] Core::Status validateBoundedBatchShape(std::span<const Transition> batch, u32 configuredCapacity,
                                                     IsReset&& isReset, std::string_view batchName)
{
    const usize maximumStoredCount = static_cast<usize>(configuredCapacity) + 1U;
    if (batch.size() > maximumStoredCount)
    {
        Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation,
                          "A Platform backend exceeded its configured frame capacity"};
        error.addContext("EngineHost::run", batchName);
        return Core::failure(std::move(error));
    }

    bool hasReset = false;
    for (usize index = 0; index < batch.size(); ++index)
    {
        if (!std::invoke(isReset, batch[index]))
        {
            continue;
        }
        if (hasReset || index + 1U != batch.size())
        {
            Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation,
                              "A Platform stream reset must be the final and only reset in its batch"};
            error.addContext("EngineHost::run", batchName);
            return Core::failure(std::move(error));
        }
        hasReset = true;
    }

    if (batch.size() > configuredCapacity && !hasReset)
    {
        Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation,
                          "The reserved Platform overflow slot must contain a stream reset"};
        error.addContext("EngineHost::run", batchName);
        return Core::failure(std::move(error));
    }
    return Core::success();
}

[[nodiscard]] Core::Status validatePlatformBatchShape(const Platform::PlatformFrameView& frame,
                                                      const Platform::PlatformFrameCapacityConfig& frameCapacities)
{
    const auto inputTransitions = frame.inputTransitions();
    if (auto status = validateBoundedBatchShape(
            inputTransitions, frameCapacities.inputTransitionCapacity,
            [](const Platform::InputTransition& transition) noexcept {
                return std::holds_alternative<Platform::InputStreamReset>(transition.payload);
            },
            "input transition batch");
        !status)
    {
        return status;
    }

    const usize inputTextByteCapacity = frameCapacities.inputTextByteCapacity;
    usize inputTextByteCount = 0;
    for (const Platform::InputTransition& transition : inputTransitions)
    {
        usize transitionTextByteCount = 0;
        if (const auto* text = std::get_if<Platform::TextInputTransition>(&transition.payload); text != nullptr)
        {
            transitionTextByteCount = text->committedUtf8.size();
        } else if (const auto* composition = std::get_if<Platform::TextCompositionTransition>(&transition.payload);
                   composition != nullptr)
        {
            transitionTextByteCount = composition->preeditUtf8.size();
        }
        if (inputTextByteCount > inputTextByteCapacity ||
            transitionTextByteCount > inputTextByteCapacity - inputTextByteCount)
        {
            return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                                 "Platform input text exceeded its configured frame byte capacity");
        }
        inputTextByteCount += transitionTextByteCount;
    }

    return validateBoundedBatchShape(
        frame.platformEvents(), frameCapacities.platformEventCapacity,
        [](const Platform::PlatformEvent& event) noexcept {
            return std::holds_alternative<Platform::PlatformEventStreamReset>(event.payload);
        },
        "platform event batch");
}

[[nodiscard]] Core::Status validatePlatformSourceSequence(const Platform::PlatformFrameView& frame,
                                                          std::optional<u64>& lastAcceptedSequence)
{
    const auto inputs = frame.inputTransitions();
    const auto events = frame.platformEvents();
    usize inputIndex = 0;
    usize eventIndex = 0;
    u64 previous = lastAcceptedSequence.value_or(0);

    while (inputIndex < inputs.size() || eventIndex < events.size())
    {
        const bool takeInput =
            eventIndex == events.size() ||
            (inputIndex < inputs.size() && inputs[inputIndex].sequence <= events[eventIndex].sequence);
        const u64 current = takeInput ? inputs[inputIndex++].sequence : events[eventIndex++].sequence;
        if (current == 0 || current <= previous)
        {
            return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                                 "Platform input and event sequences must be globally strictly increasing");
        }
        previous = current;
    }

    if (previous != lastAcceptedSequence.value_or(0))
    {
        lastAcceptedSequence = previous;
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateWindowSurfaceSnapshotStructure(const Integration::WindowSurfaceSnapshot& snapshot)
{
    if (!snapshot.surface.hasValue() || !snapshot.sourceWindow.hasValue() || snapshot.sourceMetricsRevision == 0 ||
        snapshot.surfaceRevision == 0)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "A WindowSurface snapshot contains an invalid identity or revision");
    }
    if (!std::isfinite(snapshot.contentScale.x) || !std::isfinite(snapshot.contentScale.y) ||
        snapshot.contentScale.x <= 0.0F || snapshot.contentScale.y <= 0.0F)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "A WindowSurface snapshot contains an invalid content scale");
    }
    const bool zeroExtent = snapshot.framebufferExtent.width == 0 || snapshot.framebufferExtent.height == 0;
    if (zeroExtent && !snapshot.suspended)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "A zero-sized WindowSurface must be suspended");
    }
    return Core::success();
}

[[nodiscard]] Core::Status
validateWindowSurfaceSnapshotForFrame(const Integration::WindowSurfaceSnapshot& snapshot,
                                      const Platform::PlatformFrameView& frame,
                                      const std::optional<Integration::WindowSurfaceSnapshot>& previous)
{
    if (auto status = validateWindowSurfaceSnapshotStructure(snapshot); !status)
    {
        return status;
    }

    const Platform::WindowFrameSnapshot* primaryWindow = frame.primaryWindow();
    if (primaryWindow == nullptr || frame.windows().size() != 1)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "A WindowSurface composition requires exactly one committed primary window");
    }

    const Platform::WindowMetricsSnapshot& metrics = primaryWindow->metrics;
    const bool expectedSuspended =
        metrics.minimized || metrics.framebufferExtent.width == 0 || metrics.framebufferExtent.height == 0;
    if (snapshot.sourceWindow != metrics.window || snapshot.sourceMetricsRevision != metrics.revision ||
        snapshot.framebufferExtent != metrics.framebufferExtent || snapshot.contentScale != metrics.contentScale ||
        snapshot.suspended != expectedSuspended)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "WindowSurface state does not match the committed Platform metrics");
    }

    if (!previous.has_value())
    {
        return Core::success();
    }
    if (snapshot.surface != previous->surface || snapshot.surfaceRevision < previous->surfaceRevision)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "WindowSurface identity changed or its revision moved backward");
    }
    if (snapshot.sourceWindow != previous->sourceWindow)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "WindowSurface source window identity changed");
    }
    if (snapshot.sourceMetricsRevision < previous->sourceMetricsRevision)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "WindowSurface source metrics revision moved backward");
    }

    const bool surfaceFactsChanged = snapshot.framebufferExtent != previous->framebufferExtent ||
                                     snapshot.contentScale != previous->contentScale ||
                                     snapshot.suspended != previous->suspended;
    if (surfaceFactsChanged && snapshot.sourceMetricsRevision == previous->sourceMetricsRevision)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "WindowSurface facts changed without a new source metrics revision");
    }

    const bool canAdvanceSurfaceRevision = previous->surfaceRevision != (std::numeric_limits<u64>::max)();
    const bool surfaceRevisionAdvancedExactlyOnce =
        canAdvanceSurfaceRevision && snapshot.surfaceRevision == previous->surfaceRevision + 1;
    if ((surfaceFactsChanged && !surfaceRevisionAdvancedExactlyOnce) ||
        (!surfaceFactsChanged && snapshot.surfaceRevision != previous->surfaceRevision))
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "WindowSurface revision must advance exactly once for each committed state change");
    }
    return Core::success();
}

[[nodiscard]] Render::RenderSurfaceState
toRenderSurfaceState(const Integration::WindowSurfaceSnapshot& snapshot) noexcept
{
    return Render::RenderSurfaceState{
        .surface =
            {
                .owner = snapshot.surface.owner().value(),
                .index = snapshot.surface.index(),
                .generation = snapshot.surface.generation(),
            },
        .framebufferExtent =
            {
                .width = snapshot.framebufferExtent.width,
                .height = snapshot.framebufferExtent.height,
            },
        .contentScale =
            {
                .x = snapshot.contentScale.x,
                .y = snapshot.contentScale.y,
            },
        .sourceMetricsRevision = snapshot.sourceMetricsRevision,
        .surfaceRevision = snapshot.surfaceRevision,
        .availability = snapshot.suspended ? Render::RenderSurfaceAvailability::Suspended
                                           : Render::RenderSurfaceAvailability::Active,
    };
}

[[nodiscard]] Render::RenderSceneFrameParameters
renderSceneFrameParameters(const Platform::PlatformFrameView& frame) noexcept
{
    const Platform::WindowFrameSnapshot* const primaryWindow = frame.primaryWindow();
    if (primaryWindow == nullptr)
    {
        return {};
    }

    const Platform::WindowMetricsSnapshot& metrics = primaryWindow->metrics;
    const bool hasFramebufferExtent =
        metrics.framebufferExtent.width != 0 && metrics.framebufferExtent.height != 0;
    const u32 width = hasFramebufferExtent ? metrics.framebufferExtent.width : metrics.logicalExtent.width;
    const u32 height = hasFramebufferExtent ? metrics.framebufferExtent.height : metrics.logicalExtent.height;
    if (width == 0 || height == 0)
    {
        return {};
    }
    return Render::RenderSceneFrameParameters{
        .primarySurfaceAspectRatio = static_cast<float>(static_cast<double>(width) / height),
    };
}

struct EngineModules final {
    // Created first, destroyed last (docs/runtime.md module order).
    std::unique_ptr<Core::Diagnostics::Diagnostics> diagnostics;
    std::unique_ptr<Core::IMonotonicClock> monotonicClock;
    std::unique_ptr<Platform::IPlatformBackend> platform;
    std::unique_ptr<Task::ITaskSystem> taskSystem;
    std::unique_ptr<Render::IRenderDevice> renderDevice;
    // Optional (M11-A14). Null when createAudioEngine factory was empty.
    std::optional<Audio::AudioEngine> audioEngine;
    Integration::IPrimaryWindowSurfaceProvider* windowSurfaceProvider = nullptr;

    EngineModules() = default;
    EngineModules(const EngineModules&) = delete;
    EngineModules& operator=(const EngineModules&) = delete;
    EngineModules(EngineModules&&) noexcept = default;
    EngineModules& operator=(EngineModules&&) noexcept = default;

    ~EngineModules() noexcept
    {
        shutdown();
    }

    void shutdown() noexcept
    {
        if (audioEngine.has_value())
        {
            audioEngine->shutdown();
            audioEngine.reset();
        }
        if (renderDevice != nullptr)
        {
            renderDevice->shutdown();
            renderDevice.reset();
        }
        if (taskSystem != nullptr)
        {
            taskSystem->shutdownAndJoin();
            taskSystem.reset();
        }
        if (platform != nullptr)
        {
            windowSurfaceProvider = nullptr;
            platform->shutdown();
            platform.reset();
        }
        monotonicClock.reset();
        if (diagnostics != nullptr)
        {
            diagnostics->shutdown();
            diagnostics.reset();
        }
    }

    [[nodiscard]] Audio::AudioEngine* audioEnginePtr() noexcept
    {
        return audioEngine.has_value() ? &(*audioEngine) : nullptr;
    }
};

enum class LifecycleState : u8 {
    Ready,
    Starting,
    Running,
    Stopping,
    Stopped,
    Failed,
};

} // namespace

namespace Detail {

class EngineHostImplementation final {
  public:
    EngineHostImplementation(
        EngineConfig config, Core::FixedStepAccumulator fixedStepAccumulator,
        PlatformEventDispatcher platformEventDispatcher,
        std::unique_ptr<Runtime::Input::ActionMapper> actionMapper,
        std::unique_ptr<Runtime::Input::UIInputRouteProducer> uiInputRouteProducer,
        Runtime::Detail::PrimaryWindowUIDisplayCoordinator primaryWindowUIDisplay,
        Render::RenderSceneBuilder renderSceneBuilder, EngineModules modules,
        std::optional<Integration::WindowSurfaceSnapshot> initialWindowSurface,
        PrimaryWindowUIContextFactory createPrimaryWindowUIContext = {},
        std::optional<std::uintptr_t> primaryWin32Hwnd = {},
        std::unique_ptr<Render::ISubmissionCompletionLedger> submissionCompletionLedger = {}) noexcept
        : m_config(std::move(config)), m_fixedStepAccumulator(std::move(fixedStepAccumulator)),
          m_platformEventDispatcher(std::move(platformEventDispatcher)), m_actionMapper(std::move(actionMapper)),
          m_uiInputRouteProducer(std::move(uiInputRouteProducer)), m_modules(std::move(modules)),
          m_primaryWindowUi(m_config.primaryWindowUICapacities, *std::pmr::get_default_resource(),
                            std::move(createPrimaryWindowUIContext)),
          m_primaryWindowUIDisplay(std::move(primaryWindowUIDisplay)),
          m_renderSceneBuilder(std::move(renderSceneBuilder)), m_ownerThread(std::this_thread::get_id()),
          m_submissionCompletionLedger(submissionCompletionLedger != nullptr
                                           ? std::move(submissionCompletionLedger)
                                           : std::unique_ptr<Render::ISubmissionCompletionLedger>(
                                                 std::make_unique<Render::NullSubmissionCompletionLedger>())),
          m_lastWindowSurface(std::move(initialWindowSurface))
#if defined(TINA_HAS_UI_UIA)
          ,
          m_primaryWin32Hwnd(primaryWin32Hwnd)
#endif
    {
#if !defined(TINA_HAS_UI_UIA)
        static_cast<void>(primaryWin32Hwnd);
#endif
    }

    ~EngineHostImplementation() noexcept
    {
        if (std::this_thread::get_id() != m_ownerThread)
        {
            std::terminate();
        }
        if (m_lifecycleState == LifecycleState::Ready)
        {
            m_lifecycleState = LifecycleState::Stopping;
            detachPrimaryWindowUia();
            m_primaryWindowUi.shutdown();
            m_platformEventDispatcher.shutdown();
            m_modules.shutdown();
            m_lifecycleState = LifecycleState::Stopped;
        } else
        {
            detachPrimaryWindowUia();
            m_primaryWindowUi.shutdown();
            m_modules.shutdown();
        }
    }

    [[nodiscard]] Core::Result<RunExitReason> run(IGameApplication& gameApplication)
    {
        if (std::this_thread::get_id() != m_ownerThread)
        {
            return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                                 "EngineHost::run must execute on the thread that created the host");
        }
        if (m_lifecycleState != LifecycleState::Ready)
        {
            return Core::failure(RuntimeErrorCode::EngineRunAlreadyStarted, "EngineHost::run may be called only once");
        }

        try
        {
            return runUnchecked(gameApplication);
        } catch (const std::bad_alloc&)
        {
            return failUnexpectedRunException(gameApplication,
                                              boundaryException(Core::CoreErrorCode::OutOfMemory, "EngineHost::run"));
        } catch (const std::exception& exception)
        {
            return failUnexpectedRunException(gameApplication,
                                              boundaryException(RuntimeErrorCode::LifecycleInvariantViolation,
                                                                "EngineHost::run", safeExceptionDetail(exception)));
        } catch (...)
        {
            return failUnexpectedRunException(gameApplication,
                                              boundaryException(RuntimeErrorCode::LifecycleInvariantViolation,
                                                                "EngineHost::run", "non-standard exception"));
        }
    }

  private:
    [[nodiscard]] Core::Result<RunExitReason> runUnchecked(IGameApplication& gameApplication)
    {

        m_lifecycleState = LifecycleState::Starting;
        GameStartupContext startupContext{m_config, m_platformEventDispatcher};
        auto initialStateResult =
            invokeResultBoundary("IGameApplication::createInitialState", RuntimeErrorCode::GameCallbackThrewException,
                                 [&] { return gameApplication.createInitialState(startupContext); });
        if (!initialStateResult)
        {
            return failBeforeStartupCommit(std::move(initialStateResult.error()));
        }

        std::unique_ptr<IGameState> candidate = std::move(*initialStateResult);
        if (candidate == nullptr)
        {
            return failBeforeStartupCommit(initialStateWasNull());
        }

        auto initialMetricsResult = invokeResultBoundary(
            "IPlatformBackend::initialPrimaryWindowMetrics", RuntimeErrorCode::LifecycleInvariantViolation,
            [&] { return m_modules.platform->initialPrimaryWindowMetrics(); });
        if (!initialMetricsResult)
        {
            candidate.reset();
            return failBeforeStartupCommit(std::move(initialMetricsResult.error()));
        }
        const std::optional<Platform::WindowMetricsSnapshot> initialMetrics = std::move(*initialMetricsResult);

        auto uiContextResult = m_primaryWindowUi.bindForStartup(initialMetrics);
        if (!uiContextResult)
        {
            candidate.reset();
            auto error = std::move(uiContextResult.error());
            error.addContext("PrimaryWindowUIContextOwner::bindForStartup");
            return failBeforeStartupCommit(std::move(error));
        }

        auto enterUIPhase = m_primaryWindowUICapability.beginGameStateEnterPhase(*uiContextResult);
        if (!enterUIPhase)
        {
            candidate.reset();
            return failBeforeStartupCommit(std::move(enterUIPhase.error()));
        }
        auto enterUIPhaseGuard = Core::makeScopeExit([this, epoch = *enterUIPhase]() noexcept {
            m_primaryWindowUICapability.abortPhase(epoch, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter);
        });

        GameStateEnterContext enterContext{m_config, m_platformEventDispatcher, m_primaryWindowUICapability,
                                           *enterUIPhase};
        auto enterResult = invokeResultBoundary("IGameState::onEnter", RuntimeErrorCode::GameCallbackThrewException,
                                                [&] { return candidate->onEnter(enterContext); });
        Core::Status enterUIPhaseStatus = m_primaryWindowUICapability.finishPhase(
            *enterUIPhase, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter);
        if (!enterUIPhaseStatus)
        {
            candidate.reset();
            auto error = std::move(enterUIPhaseStatus.error());
            error.addContext("IGameState::onEnter", "primary-window UI capability");
            return failBeforeStartupCommit(std::move(error));
        }
        enterUIPhaseGuard.release();
        if (!enterResult)
        {
            candidate.reset();
            return failBeforeStartupCommit(std::move(enterResult.error()));
        }

        const GameStatePolicy initialPolicy = candidate->initialPolicy();
        if (Core::Status layoutStatus = m_primaryWindowUILayout.commitForStartup(*uiContextResult, initialMetrics);
            !layoutStatus)
        {
            candidate.reset();
            return failBeforeStartupCommit(std::move(layoutStatus.error()));
        }
        if (auto uiaStatus = publishPrimaryWindowUia(*uiContextResult); !uiaStatus)
        {
            candidate.reset();
            return failBeforeStartupCommit(std::move(uiaStatus.error()));
        }
        if (Core::Status pushStatus = m_gameStateStack.pushCommitted(std::move(candidate), initialPolicy); !pushStatus)
        {
            return failBeforeStartupCommit(std::move(pushStatus.error()));
        }
        m_committedPolicy = initialPolicy;
        m_pendingCommands.clearAll();
        m_lifecycleState = LifecycleState::Running;

        Core::MonotonicTimePoint previousFrameTime = m_modules.monotonicClock->now();
        u64 frameIndex = 0;
        u64 simulationTick = 0;
        std::optional<Platform::PlatformFrameId> lastPlatformFrameId;
        std::optional<u64> lastPlatformSourceSequence;

        for (;;)
        {
            auto pollResult =
                invokeResultBoundary("IPlatformBackend::pollFrame", RuntimeErrorCode::LifecycleInvariantViolation,
                                     [&] { return m_modules.platform->pollFrame(); });
            if (!pollResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(pollResult.error()), frameIndex,
                                              simulationTick);
            }
            if (pollResult->isExitRequested())
            {
                return stopNormally(gameApplication, RunExitReason::PrimaryWindowRequestedClose,
                                    RunStopCause::PrimaryWindowRequestedClose);
            }

            const Platform::PlatformFrameView* platformFrame = pollResult->frame();
            if (platformFrame == nullptr)
            {
                return failAfterStartupCommit(gameApplication,
                                              Core::Error{RuntimeErrorCode::LifecycleInvariantViolation,
                                                          "Platform ContinueFrame did not contain a frame view"},
                                              frameIndex, simulationTick);
            }
            if (!platformFrame->id().hasValue() ||
                (lastPlatformFrameId.has_value() && platformFrame->id() <= *lastPlatformFrameId))
            {
                return failAfterStartupCommit(
                    gameApplication,
                    Core::Error{RuntimeErrorCode::LifecycleInvariantViolation,
                                "Platform frame identity must be non-zero and strictly increasing"},
                    frameIndex, simulationTick);
            }
            lastPlatformFrameId = platformFrame->id();
            if (auto batchStatus = validatePlatformBatchShape(*platformFrame, m_config.platformFrameCapacities);
                !batchStatus)
            {
                return failAfterStartupCommit(gameApplication, std::move(batchStatus.error()), frameIndex,
                                              simulationTick);
            }
            if (auto sequenceStatus = validatePlatformSourceSequence(*platformFrame, lastPlatformSourceSequence);
                !sequenceStatus)
            {
                auto error = std::move(sequenceStatus.error());
                error.addContext("EngineHost::run", "PlatformFrame sequence validation");
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }

            std::optional<Render::RenderSurfaceState> primaryWindowSurface;
            if (m_modules.windowSurfaceProvider != nullptr)
            {
                auto surfaceResult =
                    invokeResultBoundary("IPrimaryWindowSurfaceProvider::primaryWindowSurfaceSnapshot",
                                         RuntimeErrorCode::LifecycleInvariantViolation, [&] {
                                             return m_modules.windowSurfaceProvider->primaryWindowSurfaceSnapshot();
                                         });
                if (!surfaceResult)
                {
                    return failAfterStartupCommit(gameApplication, std::move(surfaceResult.error()), frameIndex,
                                                  simulationTick);
                }
                if (auto surfaceStatus =
                        validateWindowSurfaceSnapshotForFrame(*surfaceResult, *platformFrame, m_lastWindowSurface);
                    !surfaceStatus)
                {
                    auto error = std::move(surfaceStatus.error());
                    error.addContext("EngineHost::run", "WindowSurface snapshot validation");
                    return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
                }
                m_lastWindowSurface = *surfaceResult;
                primaryWindowSurface = toRenderSurfaceState(*surfaceResult);
            }

            const Core::MonotonicTimePoint currentFrameTime = m_modules.monotonicClock->now();
            if (currentFrameTime < previousFrameTime)
            {
                Core::Error error{RuntimeErrorCode::MonotonicClockMovedBackward, "The monotonic clock moved backward"};
                error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }

            const Core::Duration realDelta = Core::durationBetween(previousFrameTime, currentFrameTime);
            auto framePlanResult = m_fixedStepAccumulator.advance(realDelta, m_config.gameplayTimeScale);
            if (!framePlanResult)
            {
                auto error = std::move(framePlanResult.error());
                error.addContext("FixedStepAccumulator::advance", framePosition(frameIndex, simulationTick));
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }
            previousFrameTime = currentFrameTime;

            const Core::FixedStepFramePlan& framePlan = *framePlanResult;
            FrameTiming frameTiming{
                .realDelta = framePlan.realDelta,
                .acceptedRealDelta = framePlan.acceptedRealDelta,
                .rejectedRealDelta = framePlan.rejectedRealDelta,
                .updateDelta = framePlan.updateDelta,
                .discardedSimulationDelta = framePlan.discardedSimulationDelta,
                .fixedDelta = framePlan.fixedDelta,
                .interpolation = framePlan.interpolation,
                .frameIndex = frameIndex,
                .completedSimulationTicks = simulationTick,
                .fixedStepCount = framePlan.stepCount,
            };

            if (auto eventStatus = m_platformEventDispatcher.dispatch(
                    platformFrame->platformEvents(), platformFrame->windows(), platformFrame->gamepads());
                !eventStatus)
            {
                auto error = std::move(eventStatus.error());
                error.addContext("PlatformEventDispatcher::dispatch");
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }

            auto uiContextResult = m_primaryWindowUi.selectForFrame(*platformFrame);
            if (!uiContextResult)
            {
                auto error = std::move(uiContextResult.error());
                error.addContext("PrimaryWindowUIContextOwner::selectForFrame");
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }

            auto uiRouteResult = m_uiInputRouteProducer->produce(*uiContextResult, *platformFrame);
            if (!uiRouteResult)
            {
                auto error = std::move(uiRouteResult.error());
                error.addContext("UIInputRouteProducer::produce");
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }
            if (auto mappingStatus = m_actionMapper->mapFrame(*platformFrame, uiRouteResult->consumption,
                                                              uiRouteResult->claims, frameIndex, simulationTick,
                                                              &m_lastPresentedCamera2D);
                !mappingStatus)
            {
                auto error = std::move(mappingStatus.error());
                error.addContext("ActionMapper::mapFrame");
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }

            for (u32 fixedStepIndex = 0; fixedStepIndex < framePlan.stepCount; ++fixedStepIndex)
            {
                if (simulationTick == (std::numeric_limits<u64>::max)())
                {
                    Core::Error error{Core::CoreErrorCode::CapacityExceeded,
                                      "The simulation tick counter is exhausted"};
                    error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
                    return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
                }

                const FixedUpdateTiming fixedTiming{
                    .fixedDelta = framePlan.fixedDelta,
                    .simulationTickIndex = simulationTick,
                    .fixedStepIndexInFrame = fixedStepIndex,
                    .fixedStepCountInFrame = framePlan.stepCount,
                };
                auto simulationActionsResult = m_actionMapper->simulationActionsForTick(simulationTick);
                if (!simulationActionsResult)
                {
                    auto error = std::move(simulationActionsResult.error());
                    error.addContext("ActionMapper::simulationActionsForTick");
                    return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
                }
                const SimulationActionSnapshot& simulationActions = *simulationActionsResult;
                if (m_gameStateStack.empty())
                {
                    return failAfterStartupCommit(
                        gameApplication,
                        Core::Error{RuntimeErrorCode::LifecycleInvariantViolation, "GameStateStack is empty during fixedUpdate"},
                        frameIndex, simulationTick);
                }
                auto fixedDispatch = m_gameStateStack.forEachDispatch(
                    GameStateDispatchPhase::FixedUpdate,
                    [&](IGameState& state, const GameStatePolicy&, usize depthFromTop) -> Core::Status {
                        // ADR 0014: blocksGameplayInputBelow suppresses action state for layers below.
                        const SimulationActionSnapshot& actionsForState =
                            m_gameStateStack.gameplayInputBlockedForDepth(depthFromTop)
                                ? SimulationActionSnapshot::suppressed(simulationTick)
                                : simulationActions;
                        FixedUpdateContext fixedContext{frameTiming, fixedTiming, actionsForState,
                                                        m_modules.audioEnginePtr()};
                        return invokeResultBoundary("IGameState::fixedUpdate",
                                                    RuntimeErrorCode::GameCallbackThrewException,
                                                    [&] { return state.fixedUpdate(fixedContext); });
                    });
                if (!fixedDispatch)
                {
                    return failAfterStartupCommit(gameApplication, std::move(fixedDispatch.error()), frameIndex,
                                                  simulationTick);
                }
                if (auto completeStatus = m_actionMapper->completeSimulationTick(simulationTick); !completeStatus)
                {
                    auto error = std::move(completeStatus.error());
                    error.addContext("ActionMapper::completeSimulationTick");
                    return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
                }
                ++simulationTick;
            }
            frameTiming.completedSimulationTicks = simulationTick;

            bool exitAfterFrame = false;
            m_pendingCommands.clearAll();
            const FrameActionSnapshot frameActions = m_actionMapper->frameActions();
            if (m_gameStateStack.empty())
            {
                return failAfterStartupCommit(
                    gameApplication,
                    Core::Error{RuntimeErrorCode::LifecycleInvariantViolation, "GameStateStack is empty during updateFrame"},
                    frameIndex, simulationTick);
            }
            // Only the stack top may queue structural commands (ADR 0014).
            // blocksGameplayInputBelow: layers below see empty frame actions (no held/edge).
            // blocksUIUpdateBelow still only gates updateUI dispatch (not UI route this frame).
            const FrameActionSnapshot suppressedFrameActions =
                FrameActionSnapshot::suppressed(frameIndex);
            auto updateResult = m_gameStateStack.forEachDispatch(
                GameStateDispatchPhase::FrameUpdate,
                [&](IGameState& state, const GameStatePolicy&, usize depthFromTop) -> Core::Status {
                    const bool suppressGameplay =
                        m_gameStateStack.gameplayInputBlockedForDepth(depthFromTop);
                    const FrameActionSnapshot& actionsForState =
                        suppressGameplay ? suppressedFrameActions : frameActions;
                    // Top alone may queue push/pop/replace; below never gets pendingCommands.
                    FrameUpdateContext ctx{frameTiming, actionsForState, exitAfterFrame,
                                           depthFromTop == 0 ? &m_pendingCommands : nullptr,
                                           m_modules.audioEnginePtr()};
                    return invokeResultBoundary("IGameState::updateFrame",
                                                RuntimeErrorCode::GameCallbackThrewException,
                                                [&] { return state.updateFrame(ctx); });
                });
            if (!updateResult)
            {
                m_pendingCommands.clearAll();
                return failAfterStartupCommit(gameApplication, std::move(updateResult.error()), frameIndex,
                                              simulationTick);
            }

            // State Transition Commit (ADR 0014): after Frame Update, before extractRenderScene.
            auto transitionResult = commitPendingGameStateCommands(gameApplication, *uiContextResult);
            if (!transitionResult)
            {
                m_pendingCommands.clearAll();
                return failAfterStartupCommit(gameApplication, std::move(transitionResult.error()), frameIndex,
                                              simulationTick);
            }
            if (*transitionResult)
            {
                // Pop emptied the stack: finish the current frame phases only if a state remains.
                // Empty stack means normal exit after this frame's render is skipped.
                if (m_gameStateStack.empty())
                {
                    m_pendingCommands.clearAll();
                    return stopNormally(gameApplication, RunExitReason::GameStateStackBecameEmpty,
                                        RunStopCause::GameRequestedExitAfterCurrentFrame);
                }
            }
            m_pendingCommands.clearAll();

            if (m_gameStateStack.empty())
            {
                return failAfterStartupCommit(
                    gameApplication,
                    Core::Error{RuntimeErrorCode::LifecycleInvariantViolation, "GameStateStack empty after transition commit"},
                    frameIndex, simulationTick);
            }

            // Drain audio completions after gameplay may have enqueued Play/Stop.
            if (Audio::AudioEngine* audio = m_modules.audioEnginePtr(); audio != nullptr)
            {
                auto audioPump =
                    invokeResultBoundary("AudioEngine::pumpCompletions", RuntimeErrorCode::LifecycleInvariantViolation,
                                         [&] { return audio->pumpCompletions(/*budget=*/0); });
                if (!audioPump)
                {
                    return failAfterStartupCommit(gameApplication, std::move(audioPump.error()), frameIndex,
                                                  simulationTick);
                }
            }

            auto renderSceneBeginStatus =
                m_renderSceneBuilder.beginFrame(renderSceneFrameParameters(*platformFrame));
            if (!renderSceneBeginStatus)
            {
                return failAfterStartupCommit(gameApplication, std::move(renderSceneBeginStatus.error()), frameIndex,
                                              simulationTick);
            }
            auto renderSceneRollback = Core::makeScopeExit([this]() noexcept { m_renderSceneBuilder.rollback(); });
            Render::RenderSceneWriter renderSceneWriter = m_renderSceneBuilder.writer();
            RenderSceneExtractionContext extractionContext{frameTiming, renderSceneWriter};
            auto extractionResult = m_gameStateStack.forEachDispatch(
                GameStateDispatchPhase::RenderExtract,
                [&](IGameState& state, const GameStatePolicy&, usize) -> Core::Status {
                    return invokeResultBoundary("IGameState::extractRenderScene",
                                                RuntimeErrorCode::GameCallbackThrewException,
                                                [&] { return state.extractRenderScene(extractionContext); });
                });
            if (!extractionResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(extractionResult.error()), frameIndex,
                                              simulationTick);
            }
            auto renderSceneResult = m_renderSceneBuilder.commit();
            if (!renderSceneResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(renderSceneResult.error()), frameIndex,
                                              simulationTick);
            }
            renderSceneRollback.release();

            auto updateUIPhase = m_primaryWindowUICapability.beginUIUpdatePhase(*uiContextResult);
            if (!updateUIPhase)
            {
                return failAfterStartupCommit(gameApplication, std::move(updateUIPhase.error()), frameIndex,
                                              simulationTick);
            }
            auto updateUIPhaseGuard = Core::makeScopeExit([this, epoch = *updateUIPhase]() noexcept {
                m_primaryWindowUICapability.abortPhase(epoch, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
            });
            UIUpdateContext uiContext{frameTiming, m_primaryWindowUICapability, *updateUIPhase};
            auto uiResult = m_gameStateStack.forEachDispatch(
                GameStateDispatchPhase::UIUpdate,
                [&](IGameState& state, const GameStatePolicy&, usize) -> Core::Status {
                    return invokeResultBoundary("IGameState::updateUI", RuntimeErrorCode::GameCallbackThrewException,
                                                [&] { return state.updateUI(uiContext); });
                });
            Core::Status updateUIPhaseStatus = m_primaryWindowUICapability.finishPhase(
                *updateUIPhase, Runtime::Detail::PrimaryWindowUIPhase::UIUpdate);
            if (!updateUIPhaseStatus)
            {
                auto error = std::move(updateUIPhaseStatus.error());
                error.addContext("IGameState::updateUI", "primary-window UI capability");
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }
            updateUIPhaseGuard.release();
            if (!uiResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(uiResult.error()), frameIndex, simulationTick);
            }

            if (auto layoutStatus = m_primaryWindowUILayout.commitForFrame(*uiContextResult, *platformFrame);
                !layoutStatus)
            {
                return failAfterStartupCommit(gameApplication, std::move(layoutStatus.error()), frameIndex,
                                              simulationTick);
            }
            if (auto uiaStatus = publishPrimaryWindowUia(*uiContextResult); !uiaStatus)
            {
                return failAfterStartupCommit(gameApplication, std::move(uiaStatus.error()), frameIndex,
                                              simulationTick);
            }

            auto uiDisplayResult =
                m_primaryWindowUIDisplay.buildForFrame(*uiContextResult, *platformFrame, primaryWindowSurface);
            if (!uiDisplayResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(uiDisplayResult.error()), frameIndex,
                                              simulationTick);
            }

            std::optional<Render::UIGlyphAtlasPageView> glyphAtlasPage;
            if (*uiContextResult != nullptr)
            {
                UI::UIContext& uiContext = **uiContextResult;
                const auto pixels = uiContext.glyphAtlasPixels();
                if (!pixels.empty() && uiContext.glyphAtlasWidth() > 0
                    && uiContext.glyphAtlasHeight() > 0)
                {
                    glyphAtlasPage = Render::UIGlyphAtlasPageView{
                        .width = uiContext.glyphAtlasWidth(),
                        .height = uiContext.glyphAtlasHeight(),
                        .pixels = pixels,
                    };
                }
            }

            // RUNTIME-002: owning packet + injectable completion ledger around submit/present.
            // Null = PresentSync; Desktop Bgfx = FrameDeferred (one present lag).
            if (auto packetBegin = m_renderFramePacket.beginFrame(frameIndex); !packetBegin)
            {
                return failAfterStartupCommit(gameApplication, std::move(packetBegin.error()), frameIndex,
                                              simulationTick);
            }
            auto packetRollback = Core::makeScopeExit([this]() noexcept {
                (void)m_renderFramePacket.abandon(m_submissionCompletionLedger.get());
            });

            // Surface pin: keep surface snapshot facts alive for this submission epoch.
            if (primaryWindowSurface.has_value())
            {
                struct SurfacePinCookie final {
                    u64 surfaceRevision = 0;
                    bool released = false;
                };
                // Cookie is owned by the pin release callback lifetime for this frame only.
                // Static thread-local last-revision probe for failure-injection tests.
                static thread_local u64 s_lastReleasedSurfaceRevision = 0;
                auto* cookie = new SurfacePinCookie{
                    .surfaceRevision = primaryWindowSurface->surfaceRevision,
                    .released = false,
                };
                Render::FramePin surfacePin{
                    Render::FramePinKind::Surface,
                    primaryWindowSurface->surfaceRevision,
                    cookie,
                    [](void* userData) noexcept {
                        auto* pinCookie = static_cast<SurfacePinCookie*>(userData);
                        if (pinCookie != nullptr && !pinCookie->released)
                        {
                            s_lastReleasedSurfaceRevision = pinCookie->surfaceRevision;
                            pinCookie->released = true;
                        }
                        delete pinCookie;
                    },
                };
                if (auto pinStatus =
                        m_renderFramePacket.add(Render::FramePinKind::Surface, std::move(surfacePin));
                    !pinStatus)
                {
                    return failAfterStartupCommit(gameApplication, std::move(pinStatus.error()), frameIndex,
                                                  simulationTick);
                }
                (void)s_lastReleasedSurfaceRevision;
            }

            // Glyph atlas pin: UI owner payload must remain valid through sync submit.
            if (glyphAtlasPage.has_value() && !glyphAtlasPage->pixels.empty())
            {
                struct AtlasPinCookie final {
                    u32 width = 0;
                    u32 height = 0;
                    bool released = false;
                };
                auto* cookie = new AtlasPinCookie{
                    .width = glyphAtlasPage->width,
                    .height = glyphAtlasPage->height,
                    .released = false,
                };
                Render::FramePin atlasPin{
                    Render::FramePinKind::GlyphAtlas,
                    (static_cast<u64>(glyphAtlasPage->width) << 32) | glyphAtlasPage->height,
                    cookie,
                    [](void* userData) noexcept {
                        auto* pinCookie = static_cast<AtlasPinCookie*>(userData);
                        if (pinCookie != nullptr)
                        {
                            pinCookie->released = true;
                        }
                        delete pinCookie;
                    },
                };
                if (auto pinStatus =
                        m_renderFramePacket.add(Render::FramePinKind::GlyphAtlas, std::move(atlasPin));
                    !pinStatus)
                {
                    return failAfterStartupCommit(gameApplication, std::move(pinStatus.error()), frameIndex,
                                                  simulationTick);
                }
            }

            const Render::RenderFrame renderFrame{
                .frameIndex = frameIndex,
                .interpolation = frameTiming.interpolation,
                .primaryWindowSurface = primaryWindowSurface,
                .primaryWindowUIDisplayList = uiDisplayResult->displayList,
                .primaryWindowUIGlyphAtlas = glyphAtlasPage,
                .primaryWorldScene = *renderSceneResult,
            };
            auto submitResult =
                invokeResultBoundary("IRenderDevice::submitFrame", RuntimeErrorCode::LifecycleInvariantViolation,
                                     [&] { return m_modules.renderDevice->submitFrame(renderFrame); });
            if (!submitResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(submitResult.error()), frameIndex,
                                              simulationTick);
            }

            const bool surfaceSuspended =
                primaryWindowSurface.has_value() &&
                primaryWindowSurface->availability == Render::RenderSurfaceAvailability::Suspended;
            if (submitResult->requiresPresent() == surfaceSuspended)
            {
                Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation,
                                  "Render submission outcome contradicts the current WindowSurface state"};
                error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }

            if (submitResult->requiresPresent())
            {
                auto& ledger = *m_submissionCompletionLedger;
                auto ticketResult = ledger.beginSubmitted(submitResult->submissionIndex);
                if (!ticketResult)
                {
                    return failAfterStartupCommit(gameApplication, std::move(ticketResult.error()), frameIndex,
                                                  simulationTick);
                }
                if (auto attachStatus = m_renderFramePacket.attachSubmission(*ticketResult); !attachStatus)
                {
                    (void)ledger.abandon(*ticketResult);
                    return failAfterStartupCommit(gameApplication, std::move(attachStatus.error()), frameIndex,
                                                  simulationTick);
                }

                auto presentResult =
                    invokeResultBoundary("IRenderDevice::present", RuntimeErrorCode::LifecycleInvariantViolation,
                                         [&] { return m_modules.renderDevice->present(); });
                if (!presentResult)
                {
                    return failAfterStartupCommit(gameApplication, std::move(presentResult.error()), frameIndex,
                                                  simulationTick);
                }

                const Core::u64 presentToken =
                    m_modules.renderDevice->lastPresentFrameToken().value_or(submitResult->submissionIndex);
                if (auto* bgfxLedger =
                        dynamic_cast<Render::BgfxSubmissionCompletionLedger*>(&ledger);
                    bgfxLedger != nullptr)
                {
                    bgfxLedger->notePresentReturned(submitResult->submissionIndex, presentToken);
                }

                if (ledger.completionMode() == Render::SubmissionCompletionMode::FrameDeferred)
                {
                    // Complete previous deferred handoff (one present lag), then park this frame.
                    if (m_deferredSubmission.has_value())
                    {
                        if (auto status = Render::RenderFramePacket::completeDeferred(
                                ledger, *m_deferredSubmission);
                            !status)
                        {
                            return failAfterStartupCommit(gameApplication, std::move(status.error()),
                                                          frameIndex, simulationTick);
                        }
                        m_deferredSubmission.reset();
                    }
                    auto handoff = m_renderFramePacket.handOffDeferred(presentToken);
                    if (!handoff)
                    {
                        return failAfterStartupCommit(gameApplication, std::move(handoff.error()), frameIndex,
                                                      simulationTick);
                    }
                    m_deferredSubmission = std::move(*handoff);
                }
                else
                {
                    // PresentSync: pins release after present returns.
                    if (auto completeStatus = m_renderFramePacket.complete(ledger); !completeStatus)
                    {
                        return failAfterStartupCommit(gameApplication, std::move(completeStatus.error()),
                                                      frameIndex, simulationTick);
                    }
                }
                packetRollback.release();
                // Latch the presented Camera2D for next-frame world pointer picks.
                // Extraction-only camera moves do not update the latch until present.
                const u64 surfaceRevision =
                    primaryWindowSurface.has_value() ? primaryWindowSurface->surfaceRevision : 0;
                m_lastPresentedCamera2D.notePresented(*renderSceneResult, surfaceRevision);
            }
            else
            {
                // Suspended / skipped: no submission ticket; still release pins.
                if (auto completeStatus = m_renderFramePacket.completeSkipped(); !completeStatus)
                {
                    return failAfterStartupCommit(gameApplication, std::move(completeStatus.error()), frameIndex,
                                                  simulationTick);
                }
                packetRollback.release();
            }

            if (frameIndex == (std::numeric_limits<u64>::max)())
            {
                Core::Error error{Core::CoreErrorCode::CapacityExceeded, "The render frame counter is exhausted"};
                error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
                return failAfterStartupCommit(gameApplication, std::move(error), frameIndex, simulationTick);
            }
            ++frameIndex;

            if (exitAfterFrame)
            {
                return stopNormally(gameApplication, RunExitReason::GameRequestedExitAfterCurrentFrame,
                                    RunStopCause::GameRequestedExitAfterCurrentFrame);
            }
        }
    }

    [[nodiscard]] Core::Result<RunExitReason> failUnexpectedRunException(IGameApplication& gameApplication,
                                                                         Core::Error error)
    {
        if (!m_gameStateStack.empty())
        {
            stopCommittedGame(gameApplication, RunStopCause::RuntimeFailure, &error);
            m_lifecycleState = LifecycleState::Failed;
            return Core::failure(std::move(error));
        }
        return failBeforeStartupCommit(std::move(error));
    }

    [[nodiscard]] Core::Result<RunExitReason> failBeforeStartupCommit(Core::Error error)
    {
        error.addContext("EngineHost::run", "startup transaction was rolled back");
        m_lifecycleState = LifecycleState::Stopping;
        detachPrimaryWindowUia();
        m_primaryWindowUi.shutdown();
        m_platformEventDispatcher.shutdown();
        m_modules.shutdown();
        m_lifecycleState = LifecycleState::Failed;
        return Core::failure(std::move(error));
    }

    [[nodiscard]] Core::Result<RunExitReason>
    failAfterStartupCommit(IGameApplication& gameApplication, Core::Error error, u64 frameIndex, u64 simulationTick)
    {
        error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
        stopCommittedGame(gameApplication, RunStopCause::RuntimeFailure, &error);
        m_lifecycleState = LifecycleState::Failed;
        return Core::failure(std::move(error));
    }

    [[nodiscard]] Core::Result<RunExitReason> stopNormally(IGameApplication& gameApplication, RunExitReason exitReason,
                                                           RunStopCause stopCause)
    {
        stopCommittedGame(gameApplication, stopCause, nullptr);
        m_lifecycleState = LifecycleState::Stopped;
        return exitReason;
    }

    void flushDeferredSubmission() noexcept
    {
        if (!m_deferredSubmission.has_value() || m_submissionCompletionLedger == nullptr)
        {
            m_deferredSubmission.reset();
            return;
        }
        (void)Render::RenderFramePacket::completeDeferred(*m_submissionCompletionLedger, *m_deferredSubmission);
        m_deferredSubmission.reset();
    }

    void stopCommittedGame(IGameApplication& gameApplication, RunStopCause stopCause,
                           const Core::Error* runtimeFailure) noexcept
    {
        m_lifecycleState = LifecycleState::Stopping;
        m_pendingCommands.clearAll();
        flushDeferredSubmission();
        (void)m_renderFramePacket.abandon(m_submissionCompletionLedger.get());
        while (!m_gameStateStack.empty())
        {
            std::unique_ptr<IGameState> state = m_gameStateStack.popCommitted();
            if (state != nullptr)
            {
                GameStateExitContext exitContext{stopCause, runtimeFailure};
                state->onExit(exitContext);
            }
        }

        GameShutdownContext shutdownContext{stopCause, runtimeFailure};
        gameApplication.onShutdown(shutdownContext);
        detachPrimaryWindowUia();
        m_primaryWindowUi.shutdown();
        m_platformEventDispatcher.shutdown();
        m_modules.shutdown();
    }

    // Returns true when a structural transition changed the stack (including emptying it).
    [[nodiscard]] Core::Result<bool> commitPendingGameStateCommands(
        IGameApplication& gameApplication,
        UI::UIContext* uiContext)
    {
        bool structuralChanged = false;
        if (m_pendingCommands.policyChangeRequested)
        {
            m_committedPolicy = m_pendingCommands.requestedPolicy;
            m_gameStateStack.setTopPolicy(m_committedPolicy);
            m_pendingCommands.clearPolicy();
        }

        if (!m_pendingCommands.hasStructural())
        {
            return false;
        }

        const GameStateStructuralCommandKind kind = m_pendingCommands.structural;
        std::unique_ptr<IGameState> candidate = std::move(m_pendingCommands.candidate);
        m_pendingCommands.clearStructural();

        if (kind == GameStateStructuralCommandKind::Pop)
        {
            if (m_gameStateStack.empty())
            {
                return Core::failure(RuntimeErrorCode::GameStateCommandRejected, "GameStateStack is already empty");
            }
            std::unique_ptr<IGameState> leaving = m_gameStateStack.popCommitted();
            if (leaving != nullptr)
            {
                GameStateExitContext exitContext{RunStopCause::GameRequestedExitAfterCurrentFrame, nullptr};
                leaving->onExit(exitContext);
            }
            structuralChanged = true;
            if (!m_gameStateStack.empty())
            {
                m_committedPolicy = m_gameStateStack.topPolicy();
            }
            return structuralChanged;
        }

        if (kind == GameStateStructuralCommandKind::Push || kind == GameStateStructuralCommandKind::Replace)
        {
            if (candidate == nullptr)
            {
                return Core::failure(RuntimeErrorCode::InitialGameStateWasNull, "transition candidate is null");
            }
            if (kind == GameStateStructuralCommandKind::Push && m_gameStateStack.full())
            {
                return Core::failure(RuntimeErrorCode::GameStateStackCapacityExceeded,
                                     "GameStateStack cannot push beyond MaxStackDepth");
            }

            auto enterUIPhase = m_primaryWindowUICapability.beginGameStateEnterPhase(uiContext);
            if (!enterUIPhase)
            {
                return Core::failure(std::move(enterUIPhase.error()));
            }
            auto enterUIPhaseGuard = Core::makeScopeExit([this, epoch = *enterUIPhase]() noexcept {
                m_primaryWindowUICapability.abortPhase(epoch, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter);
            });
            GameStateEnterContext enterContext{m_config, m_platformEventDispatcher, m_primaryWindowUICapability,
                                               *enterUIPhase};
            auto enterResult = invokeResultBoundary("IGameState::onEnter", RuntimeErrorCode::GameCallbackThrewException,
                                                    [&] { return candidate->onEnter(enterContext); });
            Core::Status enterUIPhaseStatus = m_primaryWindowUICapability.finishPhase(
                *enterUIPhase, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter);
            if (!enterUIPhaseStatus)
            {
                // Capability failure is a runtime fault; discard candidate without onExit.
                candidate.reset();
                auto error = std::move(enterUIPhaseStatus.error());
                error.addContext("IGameState::onEnter", "primary-window UI capability");
                return Core::failure(std::move(error));
            }
            enterUIPhaseGuard.release();
            if (!enterResult)
            {
                // ADR 0014: enter failure rolls back candidate only (no onExit), keep stack.
                candidate.reset();
                return false;
            }

            const GameStatePolicy policy = candidate->initialPolicy();
            if (kind == GameStateStructuralCommandKind::Replace)
            {
                if (m_gameStateStack.empty())
                {
                    candidate.reset();
                    return Core::failure(RuntimeErrorCode::GameStateCommandRejected,
                                         "GameStateStack replace requires a current state");
                }
                std::unique_ptr<IGameState> leaving = m_gameStateStack.popCommitted();
                if (leaving != nullptr)
                {
                    GameStateExitContext exitContext{RunStopCause::GameRequestedExitAfterCurrentFrame, nullptr};
                    leaving->onExit(exitContext);
                }
            }
            if (Core::Status pushStatus = m_gameStateStack.pushCommitted(std::move(candidate), policy); !pushStatus)
            {
                return Core::failure(std::move(pushStatus.error()));
            }
            m_committedPolicy = policy;
            structuralChanged = true;
        }

        return structuralChanged;
    }

    EngineConfig m_config;
    Core::FixedStepAccumulator m_fixedStepAccumulator;
    PlatformEventDispatcher m_platformEventDispatcher;
    std::unique_ptr<Runtime::Input::ActionMapper> m_actionMapper;
    std::unique_ptr<Runtime::Input::UIInputRouteProducer> m_uiInputRouteProducer;
    EngineModules m_modules;
    Runtime::Detail::PrimaryWindowUIContextOwner m_primaryWindowUi;
    Runtime::Detail::PrimaryWindowUICapabilityState m_primaryWindowUICapability;
    Runtime::Detail::PrimaryWindowUILayoutCoordinator m_primaryWindowUILayout;
    Runtime::Detail::PrimaryWindowUIDisplayCoordinator m_primaryWindowUIDisplay;
    Runtime::Input::LastPresentedCamera2DLatch m_lastPresentedCamera2D{};
    Render::RenderSceneBuilder m_renderSceneBuilder;
    std::thread::id m_ownerThread;
    GameStateStack m_gameStateStack{};
    GameStatePendingCommands m_pendingCommands{};
    GameStatePolicy m_committedPolicy{};
    Render::RenderFramePacket m_renderFramePacket{};
    // Owned SPI: Null = PresentSync; Desktop Bgfx = FrameDeferred (one present lag).
    std::unique_ptr<Render::ISubmissionCompletionLedger> m_submissionCompletionLedger{};
    std::optional<Render::RenderFramePacket::DeferredHandoff> m_deferredSubmission{};
    LifecycleState m_lifecycleState = LifecycleState::Ready;
    std::optional<Integration::WindowSurfaceSnapshot> m_lastWindowSurface;

#if defined(TINA_HAS_UI_UIA)
    std::optional<std::uintptr_t> m_primaryWin32Hwnd{};
    std::unique_ptr<UI::WindowsUiaHostBridge> m_uiaHostBridge{};
    UI::UIAccessibilityTree m_uiaTree{};

    void detachPrimaryWindowUia() noexcept
    {
        if (m_uiaHostBridge)
        {
            m_uiaHostBridge->detach();
            m_uiaHostBridge.reset();
        }
        m_uiaTree = UI::UIAccessibilityTree{};
    }

    // UIA is optional product wiring: fake/test HWNDs and attach failures must not
    // fail EngineHost::run. Disable the optional path and continue.
    void disablePrimaryWindowUia() noexcept
    {
        detachPrimaryWindowUia();
        m_primaryWin32Hwnd.reset();
    }

    [[nodiscard]] Core::Status ensurePrimaryWindowUiaAttached()
    {
        if (!m_primaryWin32Hwnd.has_value() || *m_primaryWin32Hwnd == 0)
        {
            return Core::success();
        }
        if (m_uiaHostBridge && m_uiaHostBridge->isAttached())
        {
            return Core::success();
        }
        HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(*m_primaryWin32Hwnd));
        if (!::IsWindow(hwnd))
        {
            disablePrimaryWindowUia();
            return Core::success();
        }
        auto bridgeResult = UI::createWindowsUiaHostBridge();
        if (!bridgeResult)
        {
            disablePrimaryWindowUia();
            return Core::success();
        }
        if (auto status = (*bridgeResult)->attach(hwnd); !status)
        {
            disablePrimaryWindowUia();
            return Core::success();
        }
        m_uiaHostBridge = std::move(*bridgeResult);
        return Core::success();
    }

    [[nodiscard]] Core::Status publishPrimaryWindowUia(UI::UIContext* context)
    {
        if (!m_primaryWin32Hwnd.has_value() || *m_primaryWin32Hwnd == 0)
        {
            return Core::success();
        }
        if (context == nullptr)
        {
            if (m_uiaHostBridge)
            {
                m_uiaHostBridge->clear();
            }
            return Core::success();
        }
        if (auto status = ensurePrimaryWindowUiaAttached(); !status)
        {
            return status;
        }
        if (m_uiaHostBridge == nullptr)
        {
            return Core::success();
        }
        if (auto status = m_uiaTree.rebuildFrom(context->committedSemantics()); !status)
        {
            // Semantics rebuild failure is a real UI invariant; keep hard.
            return status;
        }
        if (auto status = m_uiaHostBridge->publish(m_uiaTree); !status)
        {
            disablePrimaryWindowUia();
            return Core::success();
        }
        return Core::success();
    }
#else
    void detachPrimaryWindowUia() noexcept {}
    [[nodiscard]] Core::Status publishPrimaryWindowUia(UI::UIContext*) { return Core::success(); }
#endif
};

} // namespace Detail

EngineHost::EngineHost(std::unique_ptr<Detail::EngineHostImplementation> implementation) noexcept
    : m_implementation(std::move(implementation))
{
}

EngineHost::~EngineHost() noexcept = default;

Core::Result<std::unique_ptr<EngineHost>> EngineHost::Create(const EngineConfig& config,
                                                             EngineCompositionFactories factories) noexcept
{
    try
    {
        if (auto configStatus = config.validate(); !configStatus)
        {
            auto error = std::move(configStatus.error());
            error.addContext("EngineHost::Create", "EngineConfig validation");
            return Core::failure(std::move(error));
        }

        const bool platformRenderComplete = std::visit(
            [](const auto& platformRender) noexcept {
                using Composition = std::remove_cvref_t<decltype(platformRender)>;
                if constexpr (std::same_as<Composition, IndependentPlatformRenderFactories>)
                {
                    return static_cast<bool>(platformRender.createPlatformBackend) &&
                           static_cast<bool>(platformRender.createRenderDevice);
                } else
                {
                    return static_cast<bool>(platformRender.createWindowSurfacePlatformBackend) &&
                           static_cast<bool>(platformRender.createWindowSurfaceRenderDevice);
                }
            },
            factories.platformRender);
        if (!factories.createMonotonicClock || !factories.createTaskSystem || !platformRenderComplete)
        {
            return Core::failure(ConfigurationErrorCode::IncompleteEngineFactoryBundle,
                                 "Every required Engine factory must be explicitly provided");
        }

        // The public noexcept boundary takes configuration by reference so a
        // potentially allocating string copy cannot occur before this try block.
        EngineConfig ownedConfig = config;

        auto accumulatorResult = Core::FixedStepAccumulator::Create(ownedConfig.fixedSimulation);
        if (!accumulatorResult)
        {
            auto error = std::move(accumulatorResult.error());
            error.addContext("EngineHost::Create", "FixedStepAccumulator construction");
            return Core::failure(std::move(error));
        }

        auto platformEventDispatcherResult = PlatformEventDispatcher::Create(ownedConfig.platformEventSubscriptions);
        if (!platformEventDispatcherResult)
        {
            auto error = std::move(platformEventDispatcherResult.error());
            error.addContext("EngineHost::Create", "PlatformEventDispatcher construction");
            return Core::failure(std::move(error));
        }

        const InputActionMapperCapacityConfig mapperCapacities{
            .rawInputTransitionCapacity = ownedConfig.platformFrameCapacities.inputTransitionCapacity,
            .continuousControlClaimCapacity = InputActionMapperCapacityConfig::DefaultContinuousControlClaimCapacity,
            .simulationActionTransitionCapacity =
                ownedConfig.inputActions.capacities.simulationActionTransitionCapacity,
            .frameActionTransitionCapacity = ownedConfig.inputActions.capacities.frameActionTransitionCapacity,
            .digitalActionBindingCapacity = ownedConfig.inputActions.capacities.digitalActionBindingCapacity,
        };
        auto actionMapperResult =
            Runtime::Input::ActionMapper::Create(ownedConfig.inputActions.digitalBindings, mapperCapacities);
        if (!actionMapperResult)
        {
            auto error = std::move(actionMapperResult.error());
            error.addContext("EngineHost::Create", "ActionMapper construction");
            return Core::failure(std::move(error));
        }

        auto uiInputRouteProducerResult =
            Runtime::Input::UIInputRouteProducer::Create(
                ownedConfig.platformFrameCapacities.inputTransitionCapacity,
                mapperCapacities.continuousControlClaimCapacity);
        if (!uiInputRouteProducerResult)
        {
            auto error = std::move(uiInputRouteProducerResult.error());
            error.addContext("EngineHost::Create", "UIInputRouteProducer construction");
            return Core::failure(std::move(error));
        }

        const PrimaryWindowUIDisplayListCapacityConfig& uiDisplayCapacities =
            ownedConfig.primaryWindowUIDisplayListCapacities;
        auto primaryWindowUIDisplayResult = Runtime::Detail::PrimaryWindowUIDisplayCoordinator::Create({
            .commandCount = uiDisplayCapacities.commandCapacity,
            .clipCount = uiDisplayCapacities.clipCapacity,
            .batchCount = uiDisplayCapacities.batchCapacity,
        });
        if (!primaryWindowUIDisplayResult)
        {
            auto error = std::move(primaryWindowUIDisplayResult.error());
            error.addContext("EngineHost::Create", "PrimaryWindowUIDisplayCoordinator construction");
            return Core::failure(std::move(error));
        }

        auto renderSceneBuilderResult =
            Render::RenderSceneBuilder::Create(ownedConfig.renderSceneCapacities);
        if (!renderSceneBuilderResult)
        {
            auto error = std::move(renderSceneBuilderResult.error());
            error.addContext("EngineHost::Create", "RenderSceneBuilder construction");
            return Core::failure(std::move(error));
        }

        EngineModules modules;
        {
            auto diagnosticsResult = Core::Diagnostics::Diagnostics::Create({});
            if (!diagnosticsResult)
            {
                auto error = std::move(diagnosticsResult.error());
                error.addContext("EngineHost::Create", "Diagnostics construction");
                return Core::failure(std::move(error));
            }
            modules.diagnostics = std::move(*diagnosticsResult);
        }

        auto clockResult = invokeResultBoundary("EngineCompositionFactories::createMonotonicClock",
                                                RuntimeErrorCode::EngineFactoryThrewException,
                                                [&] { return factories.createMonotonicClock(); });
        if (!clockResult)
        {
            return Core::failure(std::move(clockResult.error()));
        }
        modules.monotonicClock = std::move(*clockResult);
        if (modules.monotonicClock == nullptr)
        {
            return Core::failure(factoryReturnedNull("createMonotonicClock"));
        }

        const Platform::PlatformBackendCreateParams platformParams{
            .primaryWindow = ownedConfig.primaryWindow,
            .frameCapacities = ownedConfig.platformFrameCapacities,
        };
        Integration::IWindowSurfacePlatformBackend* windowSurfaceBackend = nullptr;
        if (auto* independent = std::get_if<IndependentPlatformRenderFactories>(&factories.platformRender);
            independent != nullptr)
        {
            auto platformResult = invokeResultBoundary("IndependentPlatformRenderFactories::createPlatformBackend",
                                                       RuntimeErrorCode::EngineFactoryThrewException, [&] {
                                                           return independent->createPlatformBackend(platformParams);
                                                       });
            if (!platformResult)
            {
                return Core::failure(std::move(platformResult.error()));
            }
            modules.platform = std::move(*platformResult);
            if (modules.platform == nullptr)
            {
                return Core::failure(factoryReturnedNull("createPlatformBackend"));
            }
        } else
        {
            auto& windowed = std::get<WindowSurfacePlatformRenderFactories>(factories.platformRender);
            auto platformResult =
                invokeResultBoundary("WindowSurfacePlatformRenderFactories::createWindowSurfacePlatformBackend",
                                     RuntimeErrorCode::EngineFactoryThrewException,
                                     [&] { return windowed.createWindowSurfacePlatformBackend(platformParams); });
            if (!platformResult)
            {
                return Core::failure(std::move(platformResult.error()));
            }
            if (*platformResult == nullptr)
            {
                return Core::failure(factoryReturnedNull("createWindowSurfacePlatformBackend"));
            }
            windowSurfaceBackend = platformResult->get();
            modules.windowSurfaceProvider = windowSurfaceBackend;
            modules.platform = std::move(*platformResult);
        }

        const Task::TaskSystemCreateParams taskParams{};
        auto taskResult = invokeResultBoundary("EngineCompositionFactories::createTaskSystem",
                                               RuntimeErrorCode::EngineFactoryThrewException,
                                               [&] { return factories.createTaskSystem(taskParams); });
        if (!taskResult)
        {
            return Core::failure(std::move(taskResult.error()));
        }
        modules.taskSystem = std::move(*taskResult);
        if (modules.taskSystem == nullptr)
        {
            return Core::failure(factoryReturnedNull("createTaskSystem"));
        }

        // Optional Audio (M11-A14). Empty factory keeps Runtime Null graphs free of audio.
        if (factories.createAudioEngine)
        {
            auto audioResult = invokeResultBoundary("EngineCompositionFactories::createAudioEngine",
                                                    RuntimeErrorCode::EngineFactoryThrewException,
                                                    [&] { return factories.createAudioEngine(); });
            if (!audioResult)
            {
                return Core::failure(std::move(audioResult.error()));
            }
            modules.audioEngine = std::move(*audioResult);
        }

        std::optional<Integration::WindowSurfaceSnapshot> initialWindowSurface;
        std::optional<std::uintptr_t> primaryWin32Hwnd;
        if (auto* independent = std::get_if<IndependentPlatformRenderFactories>(&factories.platformRender);
            independent != nullptr)
        {
            const Render::RenderDeviceCreateParams renderParams{};
            auto renderResult = invokeResultBoundary("IndependentPlatformRenderFactories::createRenderDevice",
                                                     RuntimeErrorCode::EngineFactoryThrewException,
                                                     [&] { return independent->createRenderDevice(renderParams); });
            if (!renderResult)
            {
                return Core::failure(std::move(renderResult.error()));
            }
            modules.renderDevice = std::move(*renderResult);
            if (modules.renderDevice == nullptr)
            {
                return Core::failure(factoryReturnedNull("createRenderDevice"));
            }
        } else
        {
            auto& windowed = std::get<WindowSurfacePlatformRenderFactories>(factories.platformRender);
            auto leaseResult = invokeResultBoundary("IPrimaryWindowSurfaceProvider::acquirePrimaryWindowSurfaceLease",
                                                    RuntimeErrorCode::EngineFactoryThrewException, [&] {
                                                        return windowSurfaceBackend->acquirePrimaryWindowSurfaceLease();
                                                    });
            if (!leaseResult)
            {
                return Core::failure(std::move(leaseResult.error()));
            }
            if (!leaseResult->hasValue())
            {
                return Core::failure(factoryReturnedNull("acquirePrimaryWindowSurfaceLease"));
            }

            auto snapshotResult = invokeResultBoundary("IPrimaryWindowSurfaceProvider::primaryWindowSurfaceSnapshot",
                                                       RuntimeErrorCode::EngineFactoryThrewException, [&] {
                                                           return windowSurfaceBackend->primaryWindowSurfaceSnapshot();
                                                       });
            if (!snapshotResult)
            {
                return Core::failure(std::move(snapshotResult.error()));
            }
            if (auto status = validateWindowSurfaceSnapshotStructure(*snapshotResult); !status)
            {
                auto error = std::move(status.error());
                error.addContext("EngineHost::Create", "initial WindowSurface snapshot");
                return Core::failure(std::move(error));
            }
            if (leaseResult->surface() != snapshotResult->surface)
            {
                return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                                     "The WindowSurface lease and initial snapshot identify different surfaces");
            }

            // Decode Win32 HWND before the lease is moved into the render device (UI-002 product attach).
            if (auto bindingResult = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*leaseResult);
                bindingResult)
            {
                if (bindingResult->kind == Integration::Detail::NativeWindowBindingKind::Win32
                    && bindingResult->nativeWindow != 0)
                {
                    primaryWin32Hwnd = bindingResult->nativeWindow;
                }
            }

            const Render::RenderDeviceCreateParams renderParams{
                .initialPrimaryWindowSurface = toRenderSurfaceState(*snapshotResult),
            };
            auto renderResult = invokeResultBoundary(
                "WindowSurfacePlatformRenderFactories::createWindowSurfaceRenderDevice",
                RuntimeErrorCode::EngineFactoryThrewException,
                [&] { return windowed.createWindowSurfaceRenderDevice(renderParams, std::move(*leaseResult)); });
            if (!renderResult)
            {
                return Core::failure(std::move(renderResult.error()));
            }
            modules.renderDevice = std::move(*renderResult);
            if (modules.renderDevice == nullptr)
            {
                return Core::failure(factoryReturnedNull("createWindowSurfaceRenderDevice"));
            }

            auto publishStatus = invokeResultBoundary("IWindowSurfacePlatformBackend::publishPrimaryWindow",
                                                      RuntimeErrorCode::EngineFactoryThrewException,
                                                      [&] { return windowSurfaceBackend->publishPrimaryWindow(); });
            if (!publishStatus)
            {
                return Core::failure(std::move(publishStatus.error()));
            }
            initialWindowSurface = *snapshotResult;
        }

        std::unique_ptr<Render::ISubmissionCompletionLedger> submissionCompletionLedger{};
        if (factories.createSubmissionCompletionLedger)
        {
            auto ledgerResult =
                invokeResultBoundary("EngineCompositionFactories::createSubmissionCompletionLedger",
                                     RuntimeErrorCode::EngineFactoryThrewException,
                                     [&] { return factories.createSubmissionCompletionLedger(); });
            if (!ledgerResult)
            {
                return Core::failure(std::move(ledgerResult.error()));
            }
            submissionCompletionLedger = std::move(*ledgerResult);
            if (submissionCompletionLedger == nullptr)
            {
                return Core::failure(factoryReturnedNull("createSubmissionCompletionLedger"));
            }
        }
        else
        {
            // Headless/Null and any composition without an override: present-sync Null ledger.
            submissionCompletionLedger = std::make_unique<Render::NullSubmissionCompletionLedger>();
        }

        auto implementation = std::make_unique<Detail::EngineHostImplementation>(
            std::move(ownedConfig), std::move(*accumulatorResult), std::move(*platformEventDispatcherResult),
            std::move(*actionMapperResult), std::move(*uiInputRouteProducerResult),
            std::move(*primaryWindowUIDisplayResult), std::move(*renderSceneBuilderResult), std::move(modules),
            std::move(initialWindowSurface), std::move(factories.createPrimaryWindowUIContext), primaryWin32Hwnd,
            std::move(submissionCompletionLedger));
        return std::unique_ptr<EngineHost>(new EngineHost(std::move(implementation)));
    } catch (const std::bad_alloc&)
    {
        return Core::failure(boundaryException(Core::CoreErrorCode::OutOfMemory, "EngineHost::Create"));
    } catch (const std::exception& exception)
    {
        return Core::failure(boundaryException(RuntimeErrorCode::LifecycleInvariantViolation, "EngineHost::Create",
                                               safeExceptionDetail(exception)));
    } catch (...)
    {
        return Core::failure(boundaryException(RuntimeErrorCode::LifecycleInvariantViolation, "EngineHost::Create",
                                               "non-standard exception"));
    }
}

Core::Result<RunExitReason> EngineHost::run(IGameApplication& gameApplication) noexcept
{
    return m_implementation->run(gameApplication);
}

} // namespace Tina
