#include <tina/core/error/Error.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/integration/WindowSurface.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr Tina::InputActionId ExitAction{1};

struct SampleOptions final {
    u64 targetFrameCount = 0;
    u32 frameDelayMilliseconds = 0;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 metricsEvents = 0;
    bool escapeRequested = false;
};

void writeJsonString(std::ostream& output, std::string_view value)
{
    constexpr char hexadecimal[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value)
    {
        switch (byte)
        {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U)
            {
                output << "\\u00" << hexadecimal[byte >> 4U] << hexadecimal[byte & 0x0FU];
            } else
            {
                output.put(static_cast<char>(byte));
            }
            break;
        }
    }
    output.put('"');
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"code\":{\"domain\":" << static_cast<std::uint16_t>(error.code.domain)
              << ",\"value\":" << error.code.value << "},\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << ",\"context\":[";
    bool first = true;
    for (const Tina::Core::ErrorContext& context : error.context)
    {
        if (!first)
        {
            std::cerr.put(',');
        }
        first = false;
        std::cerr << "{\"operation\":";
        writeJsonString(std::cerr, context.operation);
        std::cerr << ",\"detail\":";
        writeJsonString(std::cerr, context.detail);
        std::cerr.put('}');
    }
    std::cerr << "]}\n";
}

template <typename Value> [[nodiscard]] bool parseUnsigned(std::string_view text, Value& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    if (argumentCount == 1)
    {
        return SampleOptions{
            .targetFrameCount = 1800,
            .frameDelayMilliseconds = 16,
        };
    }
    if (argumentCount < 2 || argumentCount > 3)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                "Expected --frames=N and optional --frame-delay-ms=N"};
        error.addContext("tina_sample_platform", "N must be an unsigned integer");
        return Tina::Core::failure(std::move(error));
    }

    constexpr std::string_view framesPrefix = "--frames=";
    constexpr std::string_view delayPrefix = "--frame-delay-ms=";
    SampleOptions options;
    bool hasFrames = false;
    bool hasDelay = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(framesPrefix) && !hasFrames)
        {
            hasFrames = parseUnsigned(argument.substr(framesPrefix.size()), options.targetFrameCount) &&
                        options.targetFrameCount != 0;
            if (hasFrames)
            {
                continue;
            }
        } else if (argument.starts_with(delayPrefix) && !hasDelay)
        {
            hasDelay = parseUnsigned(argument.substr(delayPrefix.size()), options.frameDelayMilliseconds) &&
                       options.frameDelayMilliseconds <= 1000;
            if (hasDelay)
            {
                continue;
            }
        }

        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                "A platform sample command-line argument is invalid or duplicated"};
        error.addContext("tina_sample_platform", argument);
        return Tina::Core::failure(std::move(error));
    }
    if (!hasFrames)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--frames must be an unsigned integer greater than zero");
    }
    return options;
}

class PlatformSampleState final : public Tina::IGameState {
  public:
    PlatformSampleState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++counters_.stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_.frameUpdates;
        for (const Tina::FrameActionTransition& transition : context.frameActions().transitions)
        {
            const auto* actionTransition = std::get_if<Tina::InputActionTransition>(&transition);
            if (actionTransition != nullptr && actionTransition->action == ExitAction &&
                actionTransition->kind == Tina::InputActionTransitionKind::Started)
            {
                counters_.escapeRequested = true;
                context.requestExitAfterFrame();
            }
        }
        if (counters_.frameUpdates >= options_.targetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        if (options_.frameDelayMilliseconds != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        return Tina::Core::success();
    }

  private:
    SampleOptions options_;
    LifecycleCounters& counters_;
};

class PlatformSampleApplication final : public Tina::IGameApplication {
  public:
    PlatformSampleApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext& context) override
    {
        auto subscription =
            context.platformEventSubscriptions().subscribe([this](const Tina::PlatformEventNotification& notification) {
                if (std::holds_alternative<Tina::Platform::WindowMetricsChangedEvent>(notification.event().payload))
                {
                    ++counters_.metricsEvents;
                }
            });
        if (!subscription)
        {
            return std::unexpected(std::move(subscription.error()));
        }
        platformEvents_.emplace(std::move(*subscription));
        return std::unique_ptr<Tina::IGameState>{std::make_unique<PlatformSampleState>(options_, counters_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        platformEvents_.reset();
        ++counters_.applicationShutdowns;
    }

  private:
    SampleOptions options_;
    LifecycleCounters& counters_;
    std::optional<Tina::PlatformEventSubscription> platformEvents_;
};

// M7-B1 keeps this decorator sample-local. It exercises the production
// WindowSurface composition while NullRender remains the active backend. The
// member order guarantees that the RenderDevice dies before its native lease.
class SurfacePinnedNullRenderDevice final : public Tina::Render::IRenderDevice {
  public:
    SurfacePinnedNullRenderDevice(Tina::Integration::NativeWindowSurfaceLease surfaceLease,
                                  std::unique_ptr<Tina::Render::IRenderDevice> renderDevice) noexcept
        : surfaceLease_(std::move(surfaceLease)), renderDevice_(std::move(renderDevice))
    {
    }

    [[nodiscard]] Tina::Core::Result<Tina::Render::RenderFrameSubmission>
    submitFrame(const Tina::Render::RenderFrame& frame) override
    {
        return renderDevice_->submitFrame(frame);
    }

    [[nodiscard]] Tina::Core::Status present() override
    {
        return renderDevice_->present();
    }

    [[nodiscard]] Tina::Render::RenderStatistics statistics() const noexcept override
    {
        return renderDevice_->statistics();
    }

    void shutdown() noexcept override
    {
        renderDevice_->shutdown();
    }

  private:
    Tina::Integration::NativeWindowSurfaceLease surfaceLease_;
    std::unique_ptr<Tina::Render::IRenderDevice> renderDevice_;
};

[[nodiscard]] Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>>
createSurfacePinnedNullRenderDevice(const Tina::Render::RenderDeviceCreateParams& params,
                                    Tina::Integration::NativeWindowSurfaceLease surfaceLease)
{
    auto renderDevice = Tina::Render::createNullRenderDevice(params);
    if (!renderDevice)
    {
        return std::unexpected(std::move(renderDevice.error()));
    }

    std::unique_ptr<Tina::Render::IRenderDevice> pinnedRenderDevice =
        std::make_unique<SurfacePinnedNullRenderDevice>(std::move(surfaceLease), std::move(*renderDevice));
    return pinnedRenderDevice;
}

[[nodiscard]] Tina::EngineCompositionFactories createEngineFactories()
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem = Tina::Task::createDisabledTaskSystem,
        .platformRender =
            Tina::WindowSurfacePlatformRenderFactories{
                .createWindowSurfacePlatformBackend = Tina::Platform::createGlfwWindowSurfacePlatformBackend,
                .createWindowSurfaceRenderDevice = createSurfacePinnedNullRenderDevice,
            },
    };
}

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Platform Sample";
    config.primaryWindow.title = "Tina C++23 平台样例";
    config.primaryWindow.initialLogicalExtent = {960, 540};
    config.primaryWindow.initiallyVisible = true;
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Escape},
        .action = ExitAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    return config;
}

[[nodiscard]] Tina::Core::Status verifyLifecycle(Tina::RunExitReason exitReason, const SampleOptions& options,
                                                 const LifecycleCounters& counters)
{
    if (counters.stateExits != 1 || counters.applicationShutdowns != 1)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "The platform sample did not complete exactly one shutdown lifecycle");
    }
    if (exitReason == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame && !counters.escapeRequested &&
        counters.frameUpdates != options.targetFrameCount)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "The platform sample automatic frame count was not exact");
    }
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame &&
        exitReason != Tina::RunExitReason::PrimaryWindowRequestedClose)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "The platform sample stopped for an unexpected reason");
    }
    return Tina::Core::success();
}

} // namespace

int main(int argumentCount, char** arguments)
{
    auto options = parseOptions(argumentCount, arguments);
    if (!options)
    {
        writeError(options.error());
        return 2;
    }

    auto host = Tina::EngineHost::Create(createEngineConfig(), createEngineFactories());
    if (!host)
    {
        writeError(host.error());
        return 1;
    }

    LifecycleCounters counters;
    PlatformSampleApplication application{*options, counters};
    auto run = (*host)->run(application);
    if (!run)
    {
        writeError(run.error());
        return 1;
    }
    if (auto status = verifyLifecycle(*run, *options, counters); !status)
    {
        writeError(status.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"frames\":" << counters.frameUpdates
              << ",\"exitReason\":" << static_cast<unsigned>(*run)
              << ",\"escapeRequested\":" << (counters.escapeRequested ? "true" : "false")
              << ",\"metricsEvents\":" << counters.metricsEvents << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns << "}\n";
    return 0;
}
