#include <tina/runtime/EngineHost.hpp>

#include <tina/platform/PlatformBackend.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineFactories.hpp>
#include <tina/task/TaskSystem.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Tina {
namespace {

[[nodiscard]] std::string safeExceptionDetail(const std::exception& exception)
{
    constexpr std::size_t maximumLength = 256;
    const std::string_view source = exception.what() != nullptr
        ? std::string_view(exception.what())
        : std::string_view{};

    std::string result;
    result.reserve((std::min)(source.size(), maximumLength));
    std::size_t index = 0;
    while (index < source.size() && result.size() < maximumLength) {
        const auto first = static_cast<unsigned char>(source[index]);
        if (first <= 0x7FU) {
            result.push_back(first >= 0x20U && first != 0x7FU
                    ? static_cast<char>(first)
                    : '?');
            ++index;
            continue;
        }

        std::size_t encodedLength = 0;
        char32_t codePoint = 0;
        char32_t minimumCodePoint = 0;
        if ((first & 0xE0U) == 0xC0U) {
            encodedLength = 2;
            codePoint = first & 0x1FU;
            minimumCodePoint = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            encodedLength = 3;
            codePoint = first & 0x0FU;
            minimumCodePoint = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            encodedLength = 4;
            codePoint = first & 0x07U;
            minimumCodePoint = 0x10000U;
        }

        bool valid = encodedLength != 0 && encodedLength <= source.size() - index;
        for (std::size_t offset = 1; valid && offset < encodedLength; ++offset) {
            const auto continuation = static_cast<unsigned char>(source[index + offset]);
            valid = (continuation & 0xC0U) == 0x80U;
            if (valid) {
                codePoint = (codePoint << 6U) | (continuation & 0x3FU);
            }
        }
        valid = valid && codePoint >= minimumCodePoint && codePoint <= 0x10FFFFU
            && !(codePoint >= 0xD800U && codePoint <= 0xDFFFU);

        if (!valid) {
            result.push_back('?');
            ++index;
            continue;
        }
        if (encodedLength > maximumLength - result.size()) {
            break;
        }
        result.append(source.substr(index, encodedLength));
        index += encodedLength;
    }
    return result;
}

[[nodiscard]] Core::Error boundaryException(
    Core::ErrorCode errorCode,
    std::string_view operation,
    std::string_view detail = {})
{
    Core::Error error{errorCode, "An exception crossed a Tina module boundary"};
    error.addContext(operation, detail);
    return error;
}

template <typename Function>
[[nodiscard]] auto invokeResultBoundary(
    std::string_view operation,
    Core::ErrorCode exceptionCode,
    Function&& function) -> std::invoke_result_t<Function&>
{
    using ResultType = std::invoke_result_t<Function&>;
    try {
        ResultType result = std::invoke(function);
        if (!result) {
            auto error = std::move(result.error());
            error.addContext(operation);
            return Core::failure(std::move(error));
        }
        return result;
    } catch (const std::bad_alloc&) {
        return Core::failure(boundaryException(Core::CoreErrorCode::OutOfMemory, operation));
    } catch (const std::exception& exception) {
        return Core::failure(
            boundaryException(exceptionCode, operation, safeExceptionDetail(exception)));
    } catch (...) {
        return Core::failure(boundaryException(exceptionCode, operation, "non-standard exception"));
    }
}

[[nodiscard]] Core::Error factoryReturnedNull(std::string_view factoryName)
{
    Core::Error error{
        RuntimeErrorCode::EngineFactoryReturnedNull,
        "An Engine factory returned success with a null product"};
    error.addContext("EngineHost::Create", factoryName);
    return error;
}

[[nodiscard]] Core::Error initialStateWasNull()
{
    Core::Error error{
        RuntimeErrorCode::InitialGameStateWasNull,
        "IGameApplication returned success with a null initial state"};
    error.addContext("EngineHost::run", "IGameApplication::createInitialState");
    return error;
}

[[nodiscard]] std::string framePosition(u64 frameIndex, u64 simulationTick)
{
    return "frame=" + std::to_string(frameIndex) + ", simulationTick="
        + std::to_string(simulationTick);
}

struct EngineModules final {
    std::unique_ptr<Core::IMonotonicClock> monotonicClock;
    std::unique_ptr<Platform::IPlatformBackend> platform;
    std::unique_ptr<Task::ITaskSystem> taskSystem;
    std::unique_ptr<Render::IRenderDevice> renderDevice;

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
        if (renderDevice != nullptr) {
            renderDevice->shutdown();
            renderDevice.reset();
        }
        if (taskSystem != nullptr) {
            taskSystem->shutdownAndJoin();
            taskSystem.reset();
        }
        if (platform != nullptr) {
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
    EngineHostImplementation(
        EngineConfig config,
        Core::FixedStepAccumulator fixedStepAccumulator,
        EngineModules modules) noexcept
        : m_config(std::move(config)),
          m_fixedStepAccumulator(std::move(fixedStepAccumulator)),
          m_modules(std::move(modules))
    {
    }

    ~EngineHostImplementation() noexcept
    {
        if (m_lifecycleState == LifecycleState::Ready) {
            m_lifecycleState = LifecycleState::Stopping;
            m_modules.shutdown();
            m_lifecycleState = LifecycleState::Stopped;
        } else {
            m_modules.shutdown();
        }
    }

    [[nodiscard]] Core::Result<RunExitReason> run(IGameApplication& gameApplication)
    {
        if (m_lifecycleState != LifecycleState::Ready) {
            return Core::failure(
                RuntimeErrorCode::EngineRunAlreadyStarted,
                "EngineHost::run may be called only once");
        }

        try {
            return runUnchecked(gameApplication);
        } catch (const std::bad_alloc&) {
            return failUnexpectedRunException(
                gameApplication,
                boundaryException(Core::CoreErrorCode::OutOfMemory, "EngineHost::run"));
        } catch (const std::exception& exception) {
            return failUnexpectedRunException(
                gameApplication,
                boundaryException(
                    RuntimeErrorCode::LifecycleInvariantViolation,
                    "EngineHost::run",
                    safeExceptionDetail(exception)));
        } catch (...) {
            return failUnexpectedRunException(
                gameApplication,
                boundaryException(
                    RuntimeErrorCode::LifecycleInvariantViolation,
                    "EngineHost::run",
                    "non-standard exception"));
        }
    }

private:
    [[nodiscard]] Core::Result<RunExitReason> runUnchecked(
        IGameApplication& gameApplication)
    {

        m_lifecycleState = LifecycleState::Starting;
        GameStartupContext startupContext{m_config};
        auto initialStateResult = invokeResultBoundary(
            "IGameApplication::createInitialState",
            RuntimeErrorCode::GameCallbackThrewException,
            [&] { return gameApplication.createInitialState(startupContext); });
        if (!initialStateResult) {
            return failBeforeStartupCommit(std::move(initialStateResult.error()));
        }

        std::unique_ptr<IGameState> candidate = std::move(*initialStateResult);
        if (candidate == nullptr) {
            return failBeforeStartupCommit(initialStateWasNull());
        }

        GameStateEnterContext enterContext{m_config};
        auto enterResult = invokeResultBoundary(
            "IGameState::onEnter",
            RuntimeErrorCode::GameCallbackThrewException,
            [&] { return candidate->onEnter(enterContext); });
        if (!enterResult) {
            candidate.reset();
            return failBeforeStartupCommit(std::move(enterResult.error()));
        }

        m_committedPolicy = candidate->initialPolicy();
        m_gameState = std::move(candidate);
        m_lifecycleState = LifecycleState::Running;

        Core::MonotonicTimePoint previousFrameTime = m_modules.monotonicClock->now();
        u64 frameIndex = 0;
        u64 simulationTick = 0;

        for (;;) {
            auto pollResult = invokeResultBoundary(
                "IPlatformBackend::pollEvents",
                RuntimeErrorCode::LifecycleInvariantViolation,
                [&] { return m_modules.platform->pollEvents(); });
            if (!pollResult) {
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(pollResult.error()),
                    frameIndex,
                    simulationTick);
            }
            if (pollResult->exitRequested) {
                return stopNormally(
                    gameApplication,
                    RunExitReason::PrimaryWindowRequestedClose,
                    RunStopCause::PrimaryWindowRequestedClose);
            }

            const Core::MonotonicTimePoint currentFrameTime = m_modules.monotonicClock->now();
            if (currentFrameTime < previousFrameTime) {
                Core::Error error{
                    RuntimeErrorCode::MonotonicClockMovedBackward,
                    "The monotonic clock moved backward"};
                error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(error),
                    frameIndex,
                    simulationTick);
            }

            const Core::Duration realDelta = Core::durationBetween(
                previousFrameTime,
                currentFrameTime);
            previousFrameTime = currentFrameTime;
            auto framePlanResult = m_fixedStepAccumulator.advance(
                realDelta,
                m_config.gameplayTimeScale);
            if (!framePlanResult) {
                auto error = std::move(framePlanResult.error());
                error.addContext("FixedStepAccumulator::advance", framePosition(frameIndex, simulationTick));
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(error),
                    frameIndex,
                    simulationTick);
            }

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

            for (u32 fixedStepIndex = 0; fixedStepIndex < framePlan.stepCount; ++fixedStepIndex) {
                if (simulationTick == (std::numeric_limits<u64>::max)()) {
                    Core::Error error{
                        Core::CoreErrorCode::CapacityExceeded,
                        "The simulation tick counter is exhausted"};
                    error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
                    return failAfterStartupCommit(
                        gameApplication,
                        std::move(error),
                        frameIndex,
                        simulationTick);
                }

                const FixedUpdateTiming fixedTiming{
                    .fixedDelta = framePlan.fixedDelta,
                    .simulationTickIndex = simulationTick,
                    .fixedStepIndexInFrame = fixedStepIndex,
                    .fixedStepCountInFrame = framePlan.stepCount,
                };
                FixedUpdateContext fixedContext{frameTiming, fixedTiming};
                auto fixedResult = invokeResultBoundary(
                    "IGameState::fixedUpdate",
                    RuntimeErrorCode::GameCallbackThrewException,
                    [&] { return m_gameState->fixedUpdate(fixedContext); });
                if (!fixedResult) {
                    return failAfterStartupCommit(
                        gameApplication,
                        std::move(fixedResult.error()),
                        frameIndex,
                        simulationTick);
                }
                ++simulationTick;
            }
            frameTiming.completedSimulationTicks = simulationTick;

            bool exitAfterFrame = false;
            FrameUpdateContext frameContext{frameTiming, exitAfterFrame};
            auto updateResult = invokeResultBoundary(
                "IGameState::updateFrame",
                RuntimeErrorCode::GameCallbackThrewException,
                [&] { return m_gameState->updateFrame(frameContext); });
            if (!updateResult) {
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(updateResult.error()),
                    frameIndex,
                    simulationTick);
            }

            RenderSceneExtractionContext extractionContext{frameTiming};
            auto extractionResult = invokeResultBoundary(
                "IGameState::extractRenderScene",
                RuntimeErrorCode::GameCallbackThrewException,
                [&] { return m_gameState->extractRenderScene(extractionContext); });
            if (!extractionResult) {
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(extractionResult.error()),
                    frameIndex,
                    simulationTick);
            }

            UIUpdateContext uiContext{frameTiming};
            auto uiResult = invokeResultBoundary(
                "IGameState::updateUI",
                RuntimeErrorCode::GameCallbackThrewException,
                [&] { return m_gameState->updateUI(uiContext); });
            if (!uiResult) {
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(uiResult.error()),
                    frameIndex,
                    simulationTick);
            }

            const Render::RenderFrame renderFrame{
                .frameIndex = frameIndex,
                .interpolation = frameTiming.interpolation,
                .surfaceSuspended = pollResult->surfaceSuspended,
            };
            auto submitResult = invokeResultBoundary(
                "IRenderDevice::submitFrame",
                RuntimeErrorCode::LifecycleInvariantViolation,
                [&] { return m_modules.renderDevice->submitFrame(renderFrame); });
            if (!submitResult) {
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(submitResult.error()),
                    frameIndex,
                    simulationTick);
            }

            auto presentResult = invokeResultBoundary(
                "IRenderDevice::present",
                RuntimeErrorCode::LifecycleInvariantViolation,
                [&] { return m_modules.renderDevice->present(); });
            if (!presentResult) {
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(presentResult.error()),
                    frameIndex,
                    simulationTick);
            }

            if (frameIndex == (std::numeric_limits<u64>::max)()) {
                Core::Error error{
                    Core::CoreErrorCode::CapacityExceeded,
                    "The render frame counter is exhausted"};
                error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
                return failAfterStartupCommit(
                    gameApplication,
                    std::move(error),
                    frameIndex,
                    simulationTick);
            }
            ++frameIndex;

            if (exitAfterFrame) {
                return stopNormally(
                    gameApplication,
                    RunExitReason::GameRequestedExitAfterCurrentFrame,
                    RunStopCause::GameRequestedExitAfterCurrentFrame);
            }
        }
    }

    [[nodiscard]] Core::Result<RunExitReason> failUnexpectedRunException(
        IGameApplication& gameApplication,
        Core::Error error)
    {
        if (m_gameState != nullptr) {
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
        m_modules.shutdown();
        m_lifecycleState = LifecycleState::Failed;
        return Core::failure(std::move(error));
    }

    [[nodiscard]] Core::Result<RunExitReason> failAfterStartupCommit(
        IGameApplication& gameApplication,
        Core::Error error,
        u64 frameIndex,
        u64 simulationTick)
    {
        error.addContext("EngineHost::run", framePosition(frameIndex, simulationTick));
        stopCommittedGame(gameApplication, RunStopCause::RuntimeFailure, &error);
        m_lifecycleState = LifecycleState::Failed;
        return Core::failure(std::move(error));
    }

    [[nodiscard]] Core::Result<RunExitReason> stopNormally(
        IGameApplication& gameApplication,
        RunExitReason exitReason,
        RunStopCause stopCause)
    {
        stopCommittedGame(gameApplication, stopCause, nullptr);
        m_lifecycleState = LifecycleState::Stopped;
        return exitReason;
    }

    void stopCommittedGame(
        IGameApplication& gameApplication,
        RunStopCause stopCause,
        const Core::Error* runtimeFailure) noexcept
    {
        m_lifecycleState = LifecycleState::Stopping;
        if (m_gameState != nullptr) {
            GameStateExitContext exitContext{stopCause, runtimeFailure};
            m_gameState->onExit(exitContext);
            m_gameState.reset();
        }

        GameShutdownContext shutdownContext{stopCause, runtimeFailure};
        gameApplication.onShutdown(shutdownContext);
        m_modules.shutdown();
    }

    EngineConfig m_config;
    Core::FixedStepAccumulator m_fixedStepAccumulator;
    EngineModules m_modules;
    std::unique_ptr<IGameState> m_gameState;
    [[maybe_unused]] GameStatePolicy m_committedPolicy{};
    LifecycleState m_lifecycleState = LifecycleState::Ready;
};

} // namespace Detail

EngineHost::EngineHost(
    std::unique_ptr<Detail::EngineHostImplementation> implementation) noexcept
    : m_implementation(std::move(implementation))
{
}

EngineHost::~EngineHost() noexcept = default;

Core::Result<std::unique_ptr<EngineHost>> EngineHost::Create(
    const EngineConfig& config,
    EngineFactories factories) noexcept
{
    try {
        if (auto configStatus = config.validate(); !configStatus) {
            auto error = std::move(configStatus.error());
            error.addContext("EngineHost::Create", "EngineConfig validation");
            return Core::failure(std::move(error));
        }

        if (!factories.createMonotonicClock || !factories.createPlatformBackend
            || !factories.createTaskSystem || !factories.createRenderDevice) {
            return Core::failure(
                ConfigurationErrorCode::IncompleteEngineFactoryBundle,
                "Every M6-A Engine factory must be explicitly provided");
        }

        // The public noexcept boundary takes configuration by reference so a
        // potentially allocating string copy cannot occur before this try block.
        EngineConfig ownedConfig = config;

        auto accumulatorResult = Core::FixedStepAccumulator::Create(ownedConfig.fixedSimulation);
        if (!accumulatorResult) {
            auto error = std::move(accumulatorResult.error());
            error.addContext("EngineHost::Create", "FixedStepAccumulator construction");
            return Core::failure(std::move(error));
        }

        EngineModules modules;
        auto clockResult = invokeResultBoundary(
            "EngineFactories::createMonotonicClock",
            RuntimeErrorCode::EngineFactoryThrewException,
            [&] { return factories.createMonotonicClock(); });
        if (!clockResult) {
            return Core::failure(std::move(clockResult.error()));
        }
        modules.monotonicClock = std::move(*clockResult);
        if (modules.monotonicClock == nullptr) {
            return Core::failure(factoryReturnedNull("createMonotonicClock"));
        }

        const Platform::PlatformBackendCreateParams platformParams{
            .applicationName = ownedConfig.applicationName,
        };
        auto platformResult = invokeResultBoundary(
            "EngineFactories::createPlatformBackend",
            RuntimeErrorCode::EngineFactoryThrewException,
            [&] { return factories.createPlatformBackend(platformParams); });
        if (!platformResult) {
            return Core::failure(std::move(platformResult.error()));
        }
        modules.platform = std::move(*platformResult);
        if (modules.platform == nullptr) {
            return Core::failure(factoryReturnedNull("createPlatformBackend"));
        }

        const Task::TaskSystemCreateParams taskParams{};
        auto taskResult = invokeResultBoundary(
            "EngineFactories::createTaskSystem",
            RuntimeErrorCode::EngineFactoryThrewException,
            [&] { return factories.createTaskSystem(taskParams); });
        if (!taskResult) {
            return Core::failure(std::move(taskResult.error()));
        }
        modules.taskSystem = std::move(*taskResult);
        if (modules.taskSystem == nullptr) {
            return Core::failure(factoryReturnedNull("createTaskSystem"));
        }

        const Render::RenderDeviceCreateParams renderParams{};
        auto renderResult = invokeResultBoundary(
            "EngineFactories::createRenderDevice",
            RuntimeErrorCode::EngineFactoryThrewException,
            [&] { return factories.createRenderDevice(renderParams); });
        if (!renderResult) {
            return Core::failure(std::move(renderResult.error()));
        }
        modules.renderDevice = std::move(*renderResult);
        if (modules.renderDevice == nullptr) {
            return Core::failure(factoryReturnedNull("createRenderDevice"));
        }

        auto implementation = std::make_unique<Detail::EngineHostImplementation>(
            std::move(ownedConfig),
            std::move(*accumulatorResult),
            std::move(modules));
        return std::unique_ptr<EngineHost>(new EngineHost(std::move(implementation)));
    } catch (const std::bad_alloc&) {
        return Core::failure(boundaryException(
            Core::CoreErrorCode::OutOfMemory,
            "EngineHost::Create"));
    } catch (const std::exception& exception) {
        return Core::failure(boundaryException(
            RuntimeErrorCode::LifecycleInvariantViolation,
            "EngineHost::Create",
            safeExceptionDetail(exception)));
    } catch (...) {
        return Core::failure(boundaryException(
            RuntimeErrorCode::LifecycleInvariantViolation,
            "EngineHost::Create",
            "non-standard exception"));
    }
}

Core::Result<RunExitReason> EngineHost::run(IGameApplication& gameApplication) noexcept
{
    return m_implementation->run(gameApplication);
}

} // namespace Tina
