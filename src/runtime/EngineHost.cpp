#include <tina/runtime/EngineHost.hpp>

#include <tina/platform/PlatformBackend.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/runtime/spi/PlatformEventDispatcher.hpp>
#include <tina/task/TaskSystem.hpp>

#include <tina/core/base/ScopeExit.hpp>

#include "input/ActionMapper.hpp"
#include "input/UIInputRouteProducer.hpp"
#include "ui/PrimaryWindowUICapabilityState.hpp"
#include "ui/PrimaryWindowUIContextOwner.hpp"
#include "ui/PrimaryWindowUILayoutCoordinator.hpp"

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

struct EngineModules final {
    std::unique_ptr<Core::IMonotonicClock> monotonicClock;
    std::unique_ptr<Platform::IPlatformBackend> platform;
    std::unique_ptr<Task::ITaskSystem> taskSystem;
    std::unique_ptr<Render::IRenderDevice> renderDevice;
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
    EngineHostImplementation(EngineConfig config, Core::FixedStepAccumulator fixedStepAccumulator,
                             PlatformEventDispatcher platformEventDispatcher,
                             std::unique_ptr<Runtime::Input::ActionMapper> actionMapper,
                             std::unique_ptr<Runtime::Input::UIInputRouteProducer> uiInputRouteProducer,
                             EngineModules modules,
                             std::optional<Integration::WindowSurfaceSnapshot> initialWindowSurface) noexcept
        : m_config(std::move(config)), m_fixedStepAccumulator(std::move(fixedStepAccumulator)),
          m_platformEventDispatcher(std::move(platformEventDispatcher)), m_actionMapper(std::move(actionMapper)),
          m_uiInputRouteProducer(std::move(uiInputRouteProducer)), m_modules(std::move(modules)),
          m_primaryWindowUi(m_config.primaryWindowUICapacities), m_ownerThread(std::this_thread::get_id()),
          m_lastWindowSurface(std::move(initialWindowSurface))
    {
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
            m_primaryWindowUi.shutdown();
            m_platformEventDispatcher.shutdown();
            m_modules.shutdown();
            m_lifecycleState = LifecycleState::Stopped;
        } else
        {
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

        m_committedPolicy = candidate->initialPolicy();
        if (Core::Status layoutStatus = m_primaryWindowUILayout.commitForStartup(*uiContextResult, initialMetrics);
            !layoutStatus)
        {
            candidate.reset();
            return failBeforeStartupCommit(std::move(layoutStatus.error()));
        }
        m_gameState = std::move(candidate);
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
                                                              uiRouteResult->claims, frameIndex, simulationTick);
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
                FixedUpdateContext fixedContext{frameTiming, fixedTiming, simulationActions};
                auto fixedResult =
                    invokeResultBoundary("IGameState::fixedUpdate", RuntimeErrorCode::GameCallbackThrewException,
                                         [&] { return m_gameState->fixedUpdate(fixedContext); });
                if (!fixedResult)
                {
                    return failAfterStartupCommit(gameApplication, std::move(fixedResult.error()), frameIndex,
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
            const FrameActionSnapshot frameActions = m_actionMapper->frameActions();
            FrameUpdateContext frameContext{frameTiming, frameActions, exitAfterFrame};
            auto updateResult =
                invokeResultBoundary("IGameState::updateFrame", RuntimeErrorCode::GameCallbackThrewException,
                                     [&] { return m_gameState->updateFrame(frameContext); });
            if (!updateResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(updateResult.error()), frameIndex,
                                              simulationTick);
            }

            RenderSceneExtractionContext extractionContext{frameTiming};
            auto extractionResult =
                invokeResultBoundary("IGameState::extractRenderScene", RuntimeErrorCode::GameCallbackThrewException,
                                     [&] { return m_gameState->extractRenderScene(extractionContext); });
            if (!extractionResult)
            {
                return failAfterStartupCommit(gameApplication, std::move(extractionResult.error()), frameIndex,
                                              simulationTick);
            }

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
            auto uiResult = invokeResultBoundary("IGameState::updateUI", RuntimeErrorCode::GameCallbackThrewException,
                                                 [&] { return m_gameState->updateUI(uiContext); });
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

            const Render::RenderFrame renderFrame{
                .frameIndex = frameIndex,
                .interpolation = frameTiming.interpolation,
                .primaryWindowSurface = primaryWindowSurface,
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
                auto presentResult =
                    invokeResultBoundary("IRenderDevice::present", RuntimeErrorCode::LifecycleInvariantViolation,
                                         [&] { return m_modules.renderDevice->present(); });
                if (!presentResult)
                {
                    return failAfterStartupCommit(gameApplication, std::move(presentResult.error()), frameIndex,
                                                  simulationTick);
                }
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
        if (m_gameState != nullptr)
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

    void stopCommittedGame(IGameApplication& gameApplication, RunStopCause stopCause,
                           const Core::Error* runtimeFailure) noexcept
    {
        m_lifecycleState = LifecycleState::Stopping;
        if (m_gameState != nullptr)
        {
            GameStateExitContext exitContext{stopCause, runtimeFailure};
            m_gameState->onExit(exitContext);
            m_gameState.reset();
        }

        GameShutdownContext shutdownContext{stopCause, runtimeFailure};
        gameApplication.onShutdown(shutdownContext);
        m_primaryWindowUi.shutdown();
        m_platformEventDispatcher.shutdown();
        m_modules.shutdown();
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
    std::thread::id m_ownerThread;
    std::unique_ptr<IGameState> m_gameState;
    [[maybe_unused]] GameStatePolicy m_committedPolicy{};
    LifecycleState m_lifecycleState = LifecycleState::Ready;
    std::optional<Integration::WindowSurfaceSnapshot> m_lastWindowSurface;
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

        EngineModules modules;
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

        std::optional<Integration::WindowSurfaceSnapshot> initialWindowSurface;
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

        auto implementation = std::make_unique<Detail::EngineHostImplementation>(
            std::move(ownedConfig), std::move(*accumulatorResult), std::move(*platformEventDispatcherResult),
            std::move(*actionMapperResult), std::move(*uiInputRouteProducerResult), std::move(modules),
            std::move(initialWindowSurface));
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
