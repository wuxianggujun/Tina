#include <gtest/gtest.h>

#include <tina/platform/PlatformBackend.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineFactories.hpp>
#include <tina/task/TaskSystem.hpp>

#include "support/ManualMonotonicClock.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

using EventLog = std::vector<std::string>;

[[nodiscard]] bool containsEvent(const EventLog& events, std::string_view expected)
{
    return std::ranges::find(events, expected) != events.end();
}

[[nodiscard]] bool containsEventPrefix(const EventLog& events, std::string_view prefix)
{
    return std::ranges::any_of(events, [prefix](const std::string& event) {
        return event.starts_with(prefix);
    });
}

void expectEventSuffix(const EventLog& events, const EventLog& expectedSuffix)
{
    ASSERT_GE(events.size(), expectedSuffix.size());
    EXPECT_TRUE(std::ranges::equal(
        events.end() - static_cast<std::ptrdiff_t>(expectedSuffix.size()),
        events.end(),
        expectedSuffix.begin(),
        expectedSuffix.end()));
}

class LoggingClock final : public Core::IMonotonicClock {
public:
    explicit LoggingClock(EventLog& events) noexcept
        : events_(&events)
    {
    }

    ~LoggingClock() override
    {
        events_->emplace_back("clock.destroy");
    }

    [[nodiscard]] Core::MonotonicTimePoint now() const noexcept override
    {
        return {};
    }

private:
    EventLog* events_;
};

class LoggingPlatform final : public Platform::IPlatformBackend {
public:
    explicit LoggingPlatform(EventLog& events) noexcept
        : events_(&events)
    {
    }

    ~LoggingPlatform() override
    {
        events_->emplace_back("platform.destroy");
    }

    [[nodiscard]] Core::Result<Platform::PlatformPollResult> pollEvents() override
    {
        return Platform::PlatformPollResult{};
    }

    void shutdown() noexcept override
    {
        if (!stopped_) {
            events_->emplace_back("platform.shutdown");
            stopped_ = true;
        }
    }

private:
    EventLog* events_;
    bool stopped_ = false;
};

class LoggingTaskSystem final : public Task::ITaskSystem {
public:
    explicit LoggingTaskSystem(EventLog& events) noexcept
        : events_(&events)
    {
    }

    ~LoggingTaskSystem() override
    {
        events_->emplace_back("task.destroy");
    }

    [[nodiscard]] bool isIdle() const noexcept override
    {
        return true;
    }

    void shutdownAndJoin() noexcept override
    {
        if (!stopped_) {
            events_->emplace_back("task.shutdown");
            stopped_ = true;
        }
    }

private:
    EventLog* events_;
    bool stopped_ = false;
};

class LoggingRenderDevice final : public Render::IRenderDevice {
public:
    explicit LoggingRenderDevice(EventLog& events) noexcept
        : events_(&events)
    {
    }

    ~LoggingRenderDevice() override
    {
        events_->emplace_back("render.destroy");
    }

    [[nodiscard]] Core::Status submitFrame(const Render::RenderFrame&) override
    {
        ++statistics_.submitted;
        return Core::success();
    }

    [[nodiscard]] Core::Status present() override
    {
        ++statistics_.presented;
        return Core::success();
    }

    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return statistics_;
    }

    void shutdown() noexcept override
    {
        if (!stopped_) {
            events_->emplace_back("render.shutdown");
            stopped_ = true;
        }
    }

private:
    EventLog* events_;
    Render::RenderStatistics statistics_{};
    bool stopped_ = false;
};

enum class FactoryStage {
    Clock,
    Platform,
    Task,
    Render,
};

enum class FactoryMode {
    Failure,
    SuccessNull,
    Throw,
};

template <typename Interface, typename CreateProduct>
Core::Result<std::unique_ptr<Interface>> injectedFactoryResult(
    EventLog& events,
    std::string_view event,
    FactoryStage currentStage,
    FactoryStage injectedStage,
    FactoryMode mode,
    CreateProduct&& createProduct)
{
    events.emplace_back(event);
    if (currentStage != injectedStage) {
        return std::forward<CreateProduct>(createProduct)();
    }

    switch (mode) {
    case FactoryMode::Failure:
        return Core::failure(Core::CoreErrorCode::Internal, "injected factory failure");
    case FactoryMode::SuccessNull:
        return std::unique_ptr<Interface>{};
    case FactoryMode::Throw:
        throw std::runtime_error("injected factory exception");
    }
    return Core::failure(Core::CoreErrorCode::Internal, "unreachable factory mode");
}

EngineFactories makeInjectedFactories(
    EventLog& events,
    FactoryStage injectedStage,
    FactoryMode mode)
{
    EngineFactories factories;
    factories.createMonotonicClock = [&events, injectedStage, mode]()
        -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        return injectedFactoryResult<Core::IMonotonicClock>(
            events,
            "factory.clock",
            FactoryStage::Clock,
            injectedStage,
            mode,
            [&events] {
                std::unique_ptr<Core::IMonotonicClock> clock =
                    std::make_unique<LoggingClock>(events);
                return clock;
            });
    };
    factories.createPlatformBackend = [&events, injectedStage, mode](
                                          const Platform::PlatformBackendCreateParams&)
        -> Core::Result<std::unique_ptr<Platform::IPlatformBackend>> {
        return injectedFactoryResult<Platform::IPlatformBackend>(
            events,
            "factory.platform",
            FactoryStage::Platform,
            injectedStage,
            mode,
            [&events] {
                std::unique_ptr<Platform::IPlatformBackend> platform =
                    std::make_unique<LoggingPlatform>(events);
                return platform;
            });
    };
    factories.createTaskSystem = [&events, injectedStage, mode](
                                     const Task::TaskSystemCreateParams&)
        -> Core::Result<std::unique_ptr<Task::ITaskSystem>> {
        return injectedFactoryResult<Task::ITaskSystem>(
            events,
            "factory.task",
            FactoryStage::Task,
            injectedStage,
            mode,
            [&events] {
                std::unique_ptr<Task::ITaskSystem> taskSystem =
                    std::make_unique<LoggingTaskSystem>(events);
                return taskSystem;
            });
    };
    factories.createRenderDevice = [&events, injectedStage, mode](
                                       const Render::RenderDeviceCreateParams&)
        -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
        return injectedFactoryResult<Render::IRenderDevice>(
            events,
            "factory.render",
            FactoryStage::Render,
            injectedStage,
            mode,
            [&events] {
                std::unique_ptr<Render::IRenderDevice> renderDevice =
                    std::make_unique<LoggingRenderDevice>(events);
                return renderDevice;
            });
    };
    return factories;
}

void removeFactory(EngineFactories& factories, FactoryStage stage)
{
    switch (stage) {
    case FactoryStage::Clock:
        factories.createMonotonicClock = {};
        break;
    case FactoryStage::Platform:
        factories.createPlatformBackend = {};
        break;
    case FactoryStage::Task:
        factories.createTaskSystem = {};
        break;
    case FactoryStage::Render:
        factories.createRenderDevice = {};
        break;
    }
}

[[nodiscard]] EventLog expectedRollbackEvents(FactoryStage failedStage)
{
    switch (failedStage) {
    case FactoryStage::Clock:
        return {"factory.clock"};
    case FactoryStage::Platform:
        return {"factory.clock", "factory.platform", "clock.destroy"};
    case FactoryStage::Task:
        return {
            "factory.clock",
            "factory.platform",
            "factory.task",
            "platform.shutdown",
            "platform.destroy",
            "clock.destroy",
        };
    case FactoryStage::Render:
        return {
            "factory.clock",
            "factory.platform",
            "factory.task",
            "factory.render",
            "task.shutdown",
            "task.destroy",
            "platform.shutdown",
            "platform.destroy",
            "clock.destroy",
        };
    }
    return {};
}

struct InjectedFactoryCase final {
    FactoryStage stage;
    FactoryMode mode;
};

class EngineFactoryRollbackTest : public testing::TestWithParam<InjectedFactoryCase> {
};

TEST_P(EngineFactoryRollbackTest, ConvertsFactoryOutcomeAndRollsBackInReverseOrder)
{
    EventLog events;
    const InjectedFactoryCase testCase = GetParam();
    auto hostResult = EngineHost::Create(
        EngineConfig::Defaults(),
        makeInjectedFactories(events, testCase.stage, testCase.mode));

    ASSERT_FALSE(hostResult.has_value());
    switch (testCase.mode) {
    case FactoryMode::Failure:
        EXPECT_EQ(hostResult.error().code, Core::CoreErrorCode::Internal);
        break;
    case FactoryMode::SuccessNull:
        EXPECT_EQ(hostResult.error().code, RuntimeErrorCode::EngineFactoryReturnedNull);
        break;
    case FactoryMode::Throw:
        EXPECT_EQ(hostResult.error().code, RuntimeErrorCode::EngineFactoryThrewException);
        break;
    }
    EXPECT_EQ(events, expectedRollbackEvents(testCase.stage));
}

INSTANTIATE_TEST_SUITE_P(
    EveryFactoryStageAndOutcome,
    EngineFactoryRollbackTest,
    testing::Values(
        InjectedFactoryCase{FactoryStage::Clock, FactoryMode::Failure},
        InjectedFactoryCase{FactoryStage::Clock, FactoryMode::SuccessNull},
        InjectedFactoryCase{FactoryStage::Clock, FactoryMode::Throw},
        InjectedFactoryCase{FactoryStage::Platform, FactoryMode::Failure},
        InjectedFactoryCase{FactoryStage::Platform, FactoryMode::SuccessNull},
        InjectedFactoryCase{FactoryStage::Platform, FactoryMode::Throw},
        InjectedFactoryCase{FactoryStage::Task, FactoryMode::Failure},
        InjectedFactoryCase{FactoryStage::Task, FactoryMode::SuccessNull},
        InjectedFactoryCase{FactoryStage::Task, FactoryMode::Throw},
        InjectedFactoryCase{FactoryStage::Render, FactoryMode::Failure},
        InjectedFactoryCase{FactoryStage::Render, FactoryMode::SuccessNull},
        InjectedFactoryCase{FactoryStage::Render, FactoryMode::Throw}));

class MissingEngineFactoryTest : public testing::TestWithParam<FactoryStage> {
};

TEST_P(MissingEngineFactoryTest, ValidatesCompleteBundleBeforeInvokingAnyFactory)
{
    EventLog events;
    auto factories = makeInjectedFactories(events, FactoryStage::Clock, FactoryMode::Failure);
    removeFactory(factories, GetParam());

    auto hostResult = EngineHost::Create(EngineConfig::Defaults(), std::move(factories));

    ASSERT_FALSE(hostResult.has_value());
    EXPECT_EQ(
        hostResult.error().code,
        ConfigurationErrorCode::IncompleteEngineFactoryBundle);
    EXPECT_TRUE(events.empty());
}

INSTANTIATE_TEST_SUITE_P(
    EveryFactorySlot,
    MissingEngineFactoryTest,
    testing::Values(
        FactoryStage::Clock,
        FactoryStage::Platform,
        FactoryStage::Task,
        FactoryStage::Render));

enum class CommittedFailurePoint {
    None,
    PlatformPoll,
    FixedUpdate,
    UpdateFrame,
    ExtractRenderScene,
    UpdateUI,
    RenderSubmit,
    RenderPresent,
};

enum class InjectedOutcome {
    ReturnError,
    Throw,
};

struct RuntimeProbe final {
    EventLog events;
    ManualMonotonicClock* clock = nullptr;
    std::vector<Core::Duration> frameDeltas;
    CommittedFailurePoint failurePoint = CommittedFailurePoint::None;
    InjectedOutcome failureOutcome = InjectedOutcome::ReturnError;
    bool platformExitRequested = false;
    bool platformSurfaceSuspended = false;
    std::size_t pollCount = 0;
    u64 submitCalls = 0;
    u64 presentCalls = 0;
    u64 submittedFrames = 0;
    u64 presentedFrames = 0;
    bool lastSubmittedSurfaceSuspended = false;
};

class AdvancingPlatform final : public Platform::IPlatformBackend {
public:
    explicit AdvancingPlatform(RuntimeProbe& probe) noexcept
        : probe_(&probe)
    {
    }

    ~AdvancingPlatform() override
    {
        probe_->events.emplace_back("platform.destroy");
    }

    [[nodiscard]] Core::Result<Platform::PlatformPollResult> pollEvents() override
    {
        const std::size_t frameIndex = probe_->pollCount++;
        probe_->events.emplace_back("platform.poll." + std::to_string(frameIndex));
        if (probe_->failurePoint == CommittedFailurePoint::PlatformPoll) {
            if (probe_->failureOutcome == InjectedOutcome::Throw) {
                throw std::runtime_error("platform poll exception");
            }
            return Core::failure(Core::CoreErrorCode::Internal, "platform poll failure");
        }
        if (probe_->platformExitRequested) {
            return Platform::PlatformPollResult{.exitRequested = true};
        }
        if (frameIndex < probe_->frameDeltas.size()) {
            probe_->clock->advance(probe_->frameDeltas[frameIndex]);
        }
        return Platform::PlatformPollResult{
            .surfaceSuspended = probe_->platformSurfaceSuspended,
        };
    }

    void shutdown() noexcept override
    {
        if (!stopped_) {
            probe_->events.emplace_back("platform.shutdown");
            stopped_ = true;
        }
    }

private:
    RuntimeProbe* probe_;
    bool stopped_ = false;
};

class ProbeTaskSystem final : public Task::ITaskSystem {
public:
    explicit ProbeTaskSystem(RuntimeProbe& probe) noexcept
        : probe_(&probe)
    {
    }

    ~ProbeTaskSystem() override
    {
        probe_->events.emplace_back("task.destroy");
    }

    [[nodiscard]] bool isIdle() const noexcept override
    {
        return true;
    }

    void shutdownAndJoin() noexcept override
    {
        if (!stopped_) {
            probe_->events.emplace_back("task.shutdown");
            stopped_ = true;
        }
    }

private:
    RuntimeProbe* probe_;
    bool stopped_ = false;
};

class ProbeRenderDevice final : public Render::IRenderDevice {
public:
    explicit ProbeRenderDevice(RuntimeProbe& probe) noexcept
        : probe_(&probe)
    {
    }

    ~ProbeRenderDevice() override
    {
        probe_->events.emplace_back("render.destroy");
    }

    [[nodiscard]] Core::Status submitFrame(const Render::RenderFrame& frame) override
    {
        probe_->events.emplace_back("render.submit." + std::to_string(frame.frameIndex));
        probe_->lastSubmittedSurfaceSuspended = frame.surfaceSuspended;
        ++probe_->submitCalls;
        if (probe_->failurePoint == CommittedFailurePoint::RenderSubmit) {
            if (probe_->failureOutcome == InjectedOutcome::Throw) {
                throw std::runtime_error("render submit exception");
            }
            return Core::failure(Core::CoreErrorCode::Internal, "render submit failure");
        }
        ++probe_->submittedFrames;
        return Core::success();
    }

    [[nodiscard]] Core::Status present() override
    {
        probe_->events.emplace_back("render.present");
        ++probe_->presentCalls;
        if (probe_->failurePoint == CommittedFailurePoint::RenderPresent) {
            if (probe_->failureOutcome == InjectedOutcome::Throw) {
                throw std::runtime_error("render present exception");
            }
            return Core::failure(Core::CoreErrorCode::Internal, "render present failure");
        }
        ++probe_->presentedFrames;
        return Core::success();
    }

    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return Render::RenderStatistics{
            .submitted = probe_->submittedFrames,
            .presented = probe_->presentedFrames,
            .liveResources = 0,
        };
    }

    void shutdown() noexcept override
    {
        if (!stopped_) {
            probe_->events.emplace_back("render.shutdown");
            stopped_ = true;
        }
    }

private:
    RuntimeProbe* probe_;
    bool stopped_ = false;
};

EngineFactories makeRuntimeFactories(RuntimeProbe& probe)
{
    EngineFactories factories;
    factories.createMonotonicClock = [&probe]()
        -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        auto clock = std::make_unique<ManualMonotonicClock>();
        probe.clock = clock.get();
        std::unique_ptr<Core::IMonotonicClock> result = std::move(clock);
        return result;
    };
    factories.createPlatformBackend = [&probe](const Platform::PlatformBackendCreateParams&)
        -> Core::Result<std::unique_ptr<Platform::IPlatformBackend>> {
        std::unique_ptr<Platform::IPlatformBackend> platform =
            std::make_unique<AdvancingPlatform>(probe);
        return platform;
    };
    factories.createTaskSystem = [&probe](const Task::TaskSystemCreateParams&)
        -> Core::Result<std::unique_ptr<Task::ITaskSystem>> {
        std::unique_ptr<Task::ITaskSystem> taskSystem =
            std::make_unique<ProbeTaskSystem>(probe);
        return taskSystem;
    };
    factories.createRenderDevice = [&probe](const Render::RenderDeviceCreateParams&)
        -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
        std::unique_ptr<Render::IRenderDevice> renderDevice =
            std::make_unique<ProbeRenderDevice>(probe);
        return renderDevice;
    };
    return factories;
}

enum class StartupMode {
    Normal,
    ReturnNull,
    ReturnError,
    Throw,
};

enum class EnterMode {
    Normal,
    ReturnError,
    Throw,
};

struct FixedObservation final {
    u64 frameIndex = 0;
    u64 simulationTickIndex = 0;
    u32 fixedStepIndexInFrame = 0;
    u32 fixedStepCountInFrame = 0;
};

struct GameProbe final {
    RuntimeProbe* runtime = nullptr;
    StartupMode startupMode = StartupMode::Normal;
    EnterMode enterMode = EnterMode::Normal;
    Core::Duration advanceClockDuringEnter{};
    u64 exitOnFrame = 0;
    std::vector<FixedObservation> fixedObservations;
    std::vector<u32> fixedCountsByFrame;
    u32 exitCount = 0;
    u32 shutdownCount = 0;
    std::optional<RunStopCause> exitStopCause;
    std::optional<RunStopCause> shutdownStopCause;
    std::optional<Core::ErrorCode> exitFailureCode;
    std::optional<Core::ErrorCode> shutdownFailureCode;
};

[[nodiscard]] Core::Status injectedGameFailure(std::string_view phase)
{
    return Core::failure(Core::CoreErrorCode::Internal, phase);
}

[[nodiscard]] Core::Status maybeInjectCommittedGameFailure(
    RuntimeProbe& runtime,
    CommittedFailurePoint currentPoint,
    std::string_view phase)
{
    if (runtime.failurePoint != currentPoint) {
        return Core::success();
    }
    if (runtime.failureOutcome == InjectedOutcome::Throw) {
        throw std::runtime_error(std::string(phase) + " exception");
    }
    return injectedGameFailure(phase);
}

class ScriptedGameState final : public IGameState {
public:
    explicit ScriptedGameState(GameProbe& probe) noexcept
        : probe_(&probe)
    {
    }

    ~ScriptedGameState() noexcept override
    {
        probe_->runtime->events.emplace_back("state.destroy");
    }

    Core::Status onEnter(GameStateEnterContext& context) override
    {
        probe_->runtime->events.emplace_back("state.enter");
        EXPECT_FALSE(context.engineConfig().applicationName.empty());
        probe_->runtime->clock->advance(probe_->advanceClockDuringEnter);
        switch (probe_->enterMode) {
        case EnterMode::Normal:
            return Core::success();
        case EnterMode::ReturnError:
            return injectedGameFailure("onEnter");
        case EnterMode::Throw:
            throw std::runtime_error("onEnter exception");
        }
        return injectedGameFailure("unreachable enter mode");
    }

    void onExit(GameStateExitContext& context) noexcept override
    {
        probe_->runtime->events.emplace_back("state.exit");
        ++probe_->exitCount;
        probe_->exitStopCause = context.stopCause();
        if (context.runtimeFailure() != nullptr) {
            probe_->exitFailureCode = context.runtimeFailure()->code;
        }
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        probe_->runtime->events.emplace_back("state.policy");
        return {};
    }

    Core::Status fixedUpdate(FixedUpdateContext& context) override
    {
        const auto& frameTiming = context.frameTiming();
        const auto& fixedTiming = context.fixedUpdateTiming();
        probe_->runtime->events.emplace_back(
            "state.fixed." + std::to_string(frameTiming.frameIndex) + "."
            + std::to_string(fixedTiming.fixedStepIndexInFrame));
        probe_->fixedObservations.push_back(FixedObservation{
            .frameIndex = frameTiming.frameIndex,
            .simulationTickIndex = fixedTiming.simulationTickIndex,
            .fixedStepIndexInFrame = fixedTiming.fixedStepIndexInFrame,
            .fixedStepCountInFrame = fixedTiming.fixedStepCountInFrame,
        });
        return maybeInjectCommittedGameFailure(
            *probe_->runtime,
            CommittedFailurePoint::FixedUpdate,
            "fixedUpdate");
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        const u64 frameIndex = context.frameTiming().frameIndex;
        probe_->runtime->events.emplace_back("state.update." + std::to_string(frameIndex));
        probe_->fixedCountsByFrame.push_back(context.frameTiming().fixedStepCount);
        if (frameIndex == probe_->exitOnFrame) {
            context.requestExitAfterFrame();
        }
        return maybeInjectCommittedGameFailure(
            *probe_->runtime,
            CommittedFailurePoint::UpdateFrame,
            "updateFrame");
    }

    Core::Status extractRenderScene(RenderSceneExtractionContext& context) const override
    {
        probe_->runtime->events.emplace_back(
            "state.extract." + std::to_string(context.frameTiming().frameIndex));
        return maybeInjectCommittedGameFailure(
            *probe_->runtime,
            CommittedFailurePoint::ExtractRenderScene,
            "extractRenderScene");
    }

    Core::Status updateUI(UIUpdateContext& context) override
    {
        probe_->runtime->events.emplace_back(
            "state.ui." + std::to_string(context.frameTiming().frameIndex));
        return maybeInjectCommittedGameFailure(
            *probe_->runtime,
            CommittedFailurePoint::UpdateUI,
            "updateUI");
    }

private:
    GameProbe* probe_;
};

class ScriptedGameApplication final : public IGameApplication {
public:
    explicit ScriptedGameApplication(GameProbe& probe) noexcept
        : probe_(&probe)
    {
    }

    Core::Result<std::unique_ptr<IGameState>> createInitialState(
        GameStartupContext& context) override
    {
        probe_->runtime->events.emplace_back("game.create");
        EXPECT_FALSE(context.engineConfig().applicationName.empty());
        switch (probe_->startupMode) {
        case StartupMode::Normal: {
            std::unique_ptr<IGameState> state = std::make_unique<ScriptedGameState>(*probe_);
            return state;
        }
        case StartupMode::ReturnNull:
            return std::unique_ptr<IGameState>{};
        case StartupMode::ReturnError:
            return Core::failure(Core::CoreErrorCode::Internal, "createInitialState");
        case StartupMode::Throw:
            throw std::runtime_error("createInitialState 中文异常");
        }
        return Core::failure(Core::CoreErrorCode::Internal, "unreachable startup mode");
    }

    void onShutdown(GameShutdownContext& context) noexcept override
    {
        probe_->runtime->events.emplace_back("game.shutdown");
        ++probe_->shutdownCount;
        probe_->shutdownStopCause = context.stopCause();
        if (context.runtimeFailure() != nullptr) {
            probe_->shutdownFailureCode = context.runtimeFailure()->code;
        }
    }

private:
    GameProbe* probe_;
};

Core::Result<std::unique_ptr<EngineHost>> createRuntimeHost(
    RuntimeProbe& probe,
    EngineConfig config = EngineConfig::Defaults())
{
    return EngineHost::Create(std::move(config), makeRuntimeFactories(probe));
}

} // namespace

TEST(EngineConfigTest, DefaultsAreValidAndUseSixtyHertzWithFourCatchUpSteps)
{
    const EngineConfig config = EngineConfig::Defaults();
    EXPECT_TRUE(config.validate().has_value());
    EXPECT_EQ(config.applicationName, "Tina");
    EXPECT_DOUBLE_EQ(config.fixedSimulation.fixedDelta.count(), 1.0 / 60.0);
    EXPECT_EQ(config.fixedSimulation.maximumStepsPerFrame, 4U);
    EXPECT_DOUBLE_EQ(config.gameplayTimeScale, 1.0);
}

TEST(EngineConfigTest, RejectsInvalidTextTimingAndScale)
{
    std::vector<EngineConfig> invalidConfigs;

    auto emptyName = EngineConfig::Defaults();
    emptyName.applicationName.clear();
    invalidConfigs.push_back(std::move(emptyName));

    auto embeddedNul = EngineConfig::Defaults();
    embeddedNul.applicationName = std::string("Tina\0Engine", 11);
    invalidConfigs.push_back(std::move(embeddedNul));

    auto invalidUtf8 = EngineConfig::Defaults();
    invalidUtf8.applicationName = std::string("\xC0\xAF", 2);
    invalidConfigs.push_back(std::move(invalidUtf8));

    auto negativeScale = EngineConfig::Defaults();
    negativeScale.gameplayTimeScale = -1.0;
    invalidConfigs.push_back(std::move(negativeScale));

    auto infiniteScale = EngineConfig::Defaults();
    infiniteScale.gameplayTimeScale = (std::numeric_limits<double>::infinity)();
    invalidConfigs.push_back(std::move(infiniteScale));

    auto zeroShutdownDeadline = EngineConfig::Defaults();
    zeroShutdownDeadline.shutdownDeadline = Core::Duration::zero();
    invalidConfigs.push_back(std::move(zeroShutdownDeadline));

    auto invalidFixedStep = EngineConfig::Defaults();
    invalidFixedStep.fixedSimulation.maximumStepsPerFrame = 0;
    invalidConfigs.push_back(std::move(invalidFixedStep));

    auto excessiveFixedSteps = EngineConfig::Defaults();
    excessiveFixedSteps.fixedSimulation.maximumStepsPerFrame =
        EngineConfig::MaximumFixedStepsPerFrame + 1;
    invalidConfigs.push_back(std::move(excessiveFixedSteps));

    for (const EngineConfig& config : invalidConfigs) {
        auto result = config.validate();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    }
}

TEST(EngineHostCreationTest, InvalidConfigIsRejectedBeforeAnyFactoryInvocation)
{
    EventLog events;
    auto config = EngineConfig::Defaults();
    config.applicationName.clear();

    auto result = EngineHost::Create(
        std::move(config),
        makeInjectedFactories(events, FactoryStage::Clock, FactoryMode::Failure));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ConfigurationErrorCode::InvalidEngineConfig);
    EXPECT_TRUE(events.empty());
}

TEST(EngineHostCreationTest, DestroyingReadyHostWithoutRunShutsModulesDownInReverseOrder)
{
    EventLog events;
    EngineFactories factories;
    factories.createMonotonicClock = [&events]()
        -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
        events.emplace_back("factory.clock");
        std::unique_ptr<Core::IMonotonicClock> clock = std::make_unique<LoggingClock>(events);
        return clock;
    };
    factories.createPlatformBackend = [&events](const Platform::PlatformBackendCreateParams&)
        -> Core::Result<std::unique_ptr<Platform::IPlatformBackend>> {
        events.emplace_back("factory.platform");
        std::unique_ptr<Platform::IPlatformBackend> platform =
            std::make_unique<LoggingPlatform>(events);
        return platform;
    };
    factories.createTaskSystem = [&events](const Task::TaskSystemCreateParams&)
        -> Core::Result<std::unique_ptr<Task::ITaskSystem>> {
        events.emplace_back("factory.task");
        std::unique_ptr<Task::ITaskSystem> taskSystem =
            std::make_unique<LoggingTaskSystem>(events);
        return taskSystem;
    };
    factories.createRenderDevice = [&events](const Render::RenderDeviceCreateParams&)
        -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
        events.emplace_back("factory.render");
        std::unique_ptr<Render::IRenderDevice> renderDevice =
            std::make_unique<LoggingRenderDevice>(events);
        return renderDevice;
    };

    auto hostResult = EngineHost::Create(EngineConfig::Defaults(), std::move(factories));
    ASSERT_TRUE(hostResult.has_value());
    ASSERT_NE(*hostResult, nullptr);
    hostResult->reset();

    EXPECT_EQ(events, EventLog({
                          "factory.clock",
                          "factory.platform",
                          "factory.task",
                          "factory.render",
                          "render.shutdown",
                          "render.destroy",
                          "task.shutdown",
                          "task.destroy",
                          "platform.shutdown",
                          "platform.destroy",
                          "clock.destroy",
                      }));
}

struct StartupFailureCase final {
    StartupMode startupMode;
    EnterMode enterMode;
    Core::ErrorCode expectedCode;
};

class RuntimeStartupFailureTest : public testing::TestWithParam<StartupFailureCase> {
};

TEST_P(RuntimeStartupFailureTest, RollsBackWithoutExitOrApplicationShutdown)
{
    RuntimeProbe runtime;
    GameProbe game;
    game.runtime = &runtime;
    game.startupMode = GetParam().startupMode;
    game.enterMode = GetParam().enterMode;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, GetParam().expectedCode);
    if (GetParam().startupMode == StartupMode::Throw) {
        EXPECT_TRUE(std::ranges::any_of(
            runResult.error().context,
            [](const Core::ErrorContext& context) {
                return context.detail == "createInitialState 中文异常";
            }));
    }
    EXPECT_EQ(game.exitCount, 0U);
    EXPECT_EQ(game.shutdownCount, 0U);
    EXPECT_FALSE(containsEvent(runtime.events, "state.exit"));
    EXPECT_FALSE(containsEvent(runtime.events, "game.shutdown"));
    EXPECT_EQ(runtime.submittedFrames, 0U);
    EXPECT_EQ(runtime.presentedFrames, 0U);
    EXPECT_TRUE(containsEvent(runtime.events, "render.shutdown"));
    EXPECT_TRUE(containsEvent(runtime.events, "task.shutdown"));
    EXPECT_TRUE(containsEvent(runtime.events, "platform.shutdown"));
}

INSTANTIATE_TEST_SUITE_P(
    TransactionalStartup,
    RuntimeStartupFailureTest,
    testing::Values(
        StartupFailureCase{
            StartupMode::ReturnNull,
            EnterMode::Normal,
            RuntimeErrorCode::InitialGameStateWasNull,
        },
        StartupFailureCase{
            StartupMode::ReturnError,
            EnterMode::Normal,
            Core::CoreErrorCode::Internal,
        },
        StartupFailureCase{
            StartupMode::Throw,
            EnterMode::Normal,
            RuntimeErrorCode::GameCallbackThrewException,
        },
        StartupFailureCase{
            StartupMode::Normal,
            EnterMode::ReturnError,
            Core::CoreErrorCode::Internal,
        },
        StartupFailureCase{
            StartupMode::Normal,
            EnterMode::Throw,
            RuntimeErrorCode::GameCallbackThrewException,
        }));

TEST(EngineHostRunTest, ExitRequestStillCompletesExtractionUiRenderAndPresent)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(*runResult, RunExitReason::GameRequestedExitAfterCurrentFrame);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::GameRequestedExitAfterCurrentFrame);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::GameRequestedExitAfterCurrentFrame);
    EXPECT_FALSE(game.exitFailureCode.has_value());
    EXPECT_FALSE(game.shutdownFailureCode.has_value());
    EXPECT_EQ(runtime.submittedFrames, 1U);
    EXPECT_EQ(runtime.presentedFrames, 1U);
    EXPECT_EQ(runtime.events, EventLog({
                               "game.create",
                               "state.enter",
                               "state.policy",
                               "platform.poll.0",
                               "state.update.0",
                               "state.extract.0",
                               "state.ui.0",
                               "render.submit.0",
                               "render.present",
                               "state.exit",
                               "state.destroy",
                               "game.shutdown",
                               "render.shutdown",
                               "render.destroy",
                               "task.shutdown",
                               "task.destroy",
                               "platform.shutdown",
                               "platform.destroy",
                           }));
}

TEST(EngineHostRunTest, LaterUiFailureWinsOverExitRequestFromSameFrame)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.failurePoint = CommittedFailurePoint::UpdateUI;
    runtime.failureOutcome = InjectedOutcome::ReturnError;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, Core::CoreErrorCode::Internal);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.exitFailureCode, Core::CoreErrorCode::Internal);
    EXPECT_EQ(game.shutdownFailureCode, Core::CoreErrorCode::Internal);
    EXPECT_EQ(runtime.submittedFrames, 0U);
    EXPECT_EQ(runtime.presentedFrames, 0U);
    EXPECT_TRUE(containsEvent(runtime.events, "state.extract.0"));
    EXPECT_TRUE(containsEvent(runtime.events, "state.ui.0"));
    EXPECT_FALSE(containsEvent(runtime.events, "render.submit.0"));
}

namespace {

using CommittedFailureCase = std::tuple<CommittedFailurePoint, InjectedOutcome>;

[[nodiscard]] Core::ErrorCode expectedFailureCode(
    CommittedFailurePoint failurePoint,
    InjectedOutcome outcome) noexcept
{
    if (outcome == InjectedOutcome::ReturnError) {
        return Core::CoreErrorCode::Internal;
    }

    switch (failurePoint) {
    case CommittedFailurePoint::FixedUpdate:
    case CommittedFailurePoint::UpdateFrame:
    case CommittedFailurePoint::ExtractRenderScene:
    case CommittedFailurePoint::UpdateUI:
        return RuntimeErrorCode::GameCallbackThrewException;
    case CommittedFailurePoint::PlatformPoll:
    case CommittedFailurePoint::RenderSubmit:
    case CommittedFailurePoint::RenderPresent:
        return RuntimeErrorCode::LifecycleInvariantViolation;
    case CommittedFailurePoint::None:
        break;
    }
    return Core::CoreErrorCode::Internal;
}

[[nodiscard]] std::string_view invocationEvent(CommittedFailurePoint failurePoint) noexcept
{
    switch (failurePoint) {
    case CommittedFailurePoint::PlatformPoll:
        return "platform.poll.0";
    case CommittedFailurePoint::FixedUpdate:
        return "state.fixed.0.0";
    case CommittedFailurePoint::UpdateFrame:
        return "state.update.0";
    case CommittedFailurePoint::ExtractRenderScene:
        return "state.extract.0";
    case CommittedFailurePoint::UpdateUI:
        return "state.ui.0";
    case CommittedFailurePoint::RenderSubmit:
        return "render.submit.0";
    case CommittedFailurePoint::RenderPresent:
        return "render.present";
    case CommittedFailurePoint::None:
        break;
    }
    return {};
}

[[nodiscard]] std::string_view boundaryOperation(CommittedFailurePoint failurePoint) noexcept
{
    switch (failurePoint) {
    case CommittedFailurePoint::PlatformPoll:
        return "IPlatformBackend::pollEvents";
    case CommittedFailurePoint::FixedUpdate:
        return "IGameState::fixedUpdate";
    case CommittedFailurePoint::UpdateFrame:
        return "IGameState::updateFrame";
    case CommittedFailurePoint::ExtractRenderScene:
        return "IGameState::extractRenderScene";
    case CommittedFailurePoint::UpdateUI:
        return "IGameState::updateUI";
    case CommittedFailurePoint::RenderSubmit:
        return "IRenderDevice::submitFrame";
    case CommittedFailurePoint::RenderPresent:
        return "IRenderDevice::present";
    case CommittedFailurePoint::None:
        break;
    }
    return {};
}

[[nodiscard]] std::string_view forbiddenLaterPhasePrefix(
    CommittedFailurePoint failurePoint) noexcept
{
    switch (failurePoint) {
    case CommittedFailurePoint::PlatformPoll:
        return "state.fixed.";
    case CommittedFailurePoint::FixedUpdate:
        return "state.update.";
    case CommittedFailurePoint::UpdateFrame:
        return "state.extract.";
    case CommittedFailurePoint::ExtractRenderScene:
        return "state.ui.";
    case CommittedFailurePoint::UpdateUI:
        return "render.submit.";
    case CommittedFailurePoint::RenderSubmit:
        return "render.present";
    case CommittedFailurePoint::RenderPresent:
    case CommittedFailurePoint::None:
        return {};
    }
    return {};
}

class CommittedRuntimeFailureTest : public testing::TestWithParam<CommittedFailureCase> {
};

TEST_P(CommittedRuntimeFailureTest, StopsLaterPhasesAndPerformsExactlyOnceReverseCleanup)
{
    const auto [failurePoint, outcome] = GetParam();
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration{0.02}};
    runtime.failurePoint = failurePoint;
    runtime.failureOutcome = outcome;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    const Core::ErrorCode expectedCode = expectedFailureCode(failurePoint, outcome);
    EXPECT_EQ(runResult.error().code, expectedCode);
    ASSERT_GE(runResult.error().context.size(), 2U);
    EXPECT_EQ(runResult.error().context.front().operation, boundaryOperation(failurePoint));
    EXPECT_EQ(runResult.error().context.back().operation, "EngineHost::run");
    EXPECT_TRUE(runResult.error().context.back().detail.starts_with("frame=0, simulationTick="));
    EXPECT_TRUE(containsEvent(runtime.events, invocationEvent(failurePoint)));
    const std::string_view forbiddenPrefix = forbiddenLaterPhasePrefix(failurePoint);
    if (!forbiddenPrefix.empty()) {
        EXPECT_FALSE(containsEventPrefix(runtime.events, forbiddenPrefix));
    }
    if (failurePoint == CommittedFailurePoint::PlatformPoll) {
        EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    }

    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::RuntimeFailure);
    EXPECT_EQ(game.exitFailureCode, expectedCode);
    EXPECT_EQ(game.shutdownFailureCode, expectedCode);

    const bool submitWasReached = failurePoint == CommittedFailurePoint::RenderSubmit
        || failurePoint == CommittedFailurePoint::RenderPresent;
    const bool presentWasReached = failurePoint == CommittedFailurePoint::RenderPresent;
    EXPECT_EQ(runtime.submitCalls, submitWasReached ? 1U : 0U);
    EXPECT_EQ(runtime.presentCalls, presentWasReached ? 1U : 0U);
    expectEventSuffix(runtime.events, EventLog({
                                         "state.exit",
                                         "state.destroy",
                                         "game.shutdown",
                                         "render.shutdown",
                                         "render.destroy",
                                         "task.shutdown",
                                         "task.destroy",
                                         "platform.shutdown",
                                         "platform.destroy",
                                     }));
}

INSTANTIATE_TEST_SUITE_P(
    EveryCommittedBoundary,
    CommittedRuntimeFailureTest,
    testing::Combine(
        testing::Values(
            CommittedFailurePoint::PlatformPoll,
            CommittedFailurePoint::FixedUpdate,
            CommittedFailurePoint::UpdateFrame,
            CommittedFailurePoint::ExtractRenderScene,
            CommittedFailurePoint::UpdateUI,
            CommittedFailurePoint::RenderSubmit,
            CommittedFailurePoint::RenderPresent),
        testing::Values(InjectedOutcome::ReturnError, InjectedOutcome::Throw)));

} // namespace

TEST(EngineHostRunTest, PlatformExitRequestStopsNormallyBeforeStartingAFrame)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration{0.02}};
    runtime.platformExitRequested = true;
    GameProbe game;
    game.runtime = &runtime;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(*runResult, RunExitReason::PrimaryWindowRequestedClose);
    EXPECT_EQ(runtime.pollCount, 1U);
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.fixed."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    EXPECT_EQ(runtime.submitCalls, 0U);
    EXPECT_EQ(runtime.presentCalls, 0U);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
    EXPECT_EQ(game.exitStopCause, RunStopCause::PrimaryWindowRequestedClose);
    EXPECT_EQ(game.shutdownStopCause, RunStopCause::PrimaryWindowRequestedClose);
    EXPECT_FALSE(game.exitFailureCode.has_value());
    EXPECT_FALSE(game.shutdownFailureCode.has_value());
    expectEventSuffix(runtime.events, EventLog({
                                         "state.exit",
                                         "state.destroy",
                                         "game.shutdown",
                                         "render.shutdown",
                                         "render.destroy",
                                         "task.shutdown",
                                         "task.destroy",
                                         "platform.shutdown",
                                         "platform.destroy",
                                     }));
}

TEST(EngineHostRunTest, StartupElapsedTimeIsExcludedFromTheFirstFrameDelta)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    GameProbe game;
    game.runtime = &runtime;
    game.advanceClockDuringEnter = Core::Duration{2.0};
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    ASSERT_EQ(game.fixedCountsByFrame.size(), 1U);
    EXPECT_EQ(game.fixedCountsByFrame.front(), 0U);
    EXPECT_TRUE(game.fixedObservations.empty());
}

TEST(EngineHostRunTest, MonotonicClockRegressionFailsBeforeFrameCallbacksAndCleansUp)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration{-0.001}};
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_FALSE(runResult.has_value());
    EXPECT_EQ(runResult.error().code, RuntimeErrorCode::MonotonicClockMovedBackward);
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.fixed."));
    EXPECT_FALSE(containsEventPrefix(runtime.events, "state.update."));
    EXPECT_EQ(runtime.submitCalls, 0U);
    EXPECT_EQ(runtime.presentCalls, 0U);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
}

TEST(EngineHostRunTest, SurfaceSuspensionIsForwardedToTheRenderFrame)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.platformSurfaceSuspended = true;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_TRUE(runtime.lastSubmittedSurfaceSuspended);
    EXPECT_EQ(runtime.submittedFrames, 1U);
    EXPECT_EQ(runtime.presentedFrames, 1U);
}

TEST(EngineHostRunTest, FixedStepTimingCoversZeroOneAndMaximumFourStepsWithStableIndices)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {
        Core::Duration{0.0},
        Core::Duration{0.01},
        Core::Duration{0.10},
    };
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 2;
    ScriptedGameApplication application(game);
    auto config = EngineConfig::Defaults();
    config.fixedSimulation = Core::FixedStepConfig{
        .fixedDelta = Core::Duration{0.01},
        .maximumAcceptedRealDelta = Core::Duration{0.20},
        .maximumStepsPerFrame = 4,
    };
    auto hostResult = createRuntimeHost(runtime, std::move(config));
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(game.fixedCountsByFrame, std::vector<u32>({0, 1, 4}));
    ASSERT_EQ(game.fixedObservations.size(), 5U);
    EXPECT_EQ(game.fixedObservations[0].frameIndex, 1U);
    EXPECT_EQ(game.fixedObservations[0].simulationTickIndex, 0U);
    EXPECT_EQ(game.fixedObservations[0].fixedStepIndexInFrame, 0U);
    EXPECT_EQ(game.fixedObservations[0].fixedStepCountInFrame, 1U);
    for (u32 index = 0; index < 4; ++index) {
        const auto& observation = game.fixedObservations[index + 1U];
        EXPECT_EQ(observation.frameIndex, 2U);
        EXPECT_EQ(observation.simulationTickIndex, static_cast<u64>(index + 1U));
        EXPECT_EQ(observation.fixedStepIndexInFrame, index);
        EXPECT_EQ(observation.fixedStepCountInFrame, 4U);
    }
}

TEST(EngineHostRunTest, RunCanOnlyBeStartedOnce)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    ASSERT_TRUE((*hostResult)->run(application).has_value());
    const std::size_t eventCountAfterFirstRun = runtime.events.size();

    auto secondRun = (*hostResult)->run(application);
    ASSERT_FALSE(secondRun.has_value());
    EXPECT_EQ(secondRun.error().code, RuntimeErrorCode::EngineRunAlreadyStarted);
    EXPECT_EQ(runtime.events.size(), eventCountAfterFirstRun);
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
}

TEST(EngineHostRunTest, StartupFailureAlsoConsumesTheSingleRunAttempt)
{
    RuntimeProbe runtime;
    GameProbe game;
    game.runtime = &runtime;
    game.startupMode = StartupMode::ReturnError;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    ASSERT_FALSE((*hostResult)->run(application).has_value());
    const std::size_t eventCountAfterFailure = runtime.events.size();

    auto secondRun = (*hostResult)->run(application);
    ASSERT_FALSE(secondRun.has_value());
    EXPECT_EQ(secondRun.error().code, RuntimeErrorCode::EngineRunAlreadyStarted);
    EXPECT_EQ(runtime.events.size(), eventCountAfterFailure);
}

TEST(EngineHostRunTest, CommittedRuntimeFailureAlsoConsumesTheSingleRunAttempt)
{
    RuntimeProbe runtime;
    runtime.frameDeltas = {Core::Duration::zero()};
    runtime.failurePoint = CommittedFailurePoint::UpdateUI;
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 0;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    ASSERT_FALSE((*hostResult)->run(application).has_value());
    const std::size_t eventCountAfterFailure = runtime.events.size();

    auto secondRun = (*hostResult)->run(application);
    ASSERT_FALSE(secondRun.has_value());
    EXPECT_EQ(secondRun.error().code, RuntimeErrorCode::EngineRunAlreadyStarted);
    EXPECT_EQ(runtime.events.size(), eventCountAfterFailure);
}

TEST(EngineHostRunTest, NullRuntimeRunsThreeHundredFramesAndShutsDownExactlyOnce)
{
    RuntimeProbe runtime;
    runtime.frameDeltas.resize(300, Core::Duration{1.0 / 60.0});
    GameProbe game;
    game.runtime = &runtime;
    game.exitOnFrame = 299;
    ScriptedGameApplication application(game);
    auto hostResult = createRuntimeHost(runtime);
    ASSERT_TRUE(hostResult.has_value());

    auto runResult = (*hostResult)->run(application);

    ASSERT_TRUE(runResult.has_value());
    EXPECT_EQ(*runResult, RunExitReason::GameRequestedExitAfterCurrentFrame);
    EXPECT_EQ(runtime.pollCount, 300U);
    EXPECT_EQ(runtime.submittedFrames, 300U);
    EXPECT_EQ(runtime.presentedFrames, 300U);
    EXPECT_TRUE(std::ranges::all_of(
        game.fixedCountsByFrame,
        [](u32 fixedStepCount) { return fixedStepCount <= 4U; }));
    EXPECT_EQ(game.exitCount, 1U);
    EXPECT_EQ(game.shutdownCount, 1U);
}

} // namespace Tina::Tests
