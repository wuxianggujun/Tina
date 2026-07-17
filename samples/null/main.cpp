#include <tina/core/error/Error.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

using Tina::Core::u64;

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
};

class NullGameState final : public Tina::IGameState {
  public:
    NullGameState(u64 targetFrameCount, LifecycleCounters& counters) noexcept
        : m_targetFrameCount(targetFrameCount), m_counters(counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++m_counters.stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++m_counters.frameUpdates;
        if (context.frameTiming().frameIndex + 1U == m_targetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

  private:
    u64 m_targetFrameCount;
    LifecycleCounters& m_counters;
};

class NullGameApplication final : public Tina::IGameApplication {
  public:
    NullGameApplication(u64 targetFrameCount, LifecycleCounters& counters) noexcept
        : m_targetFrameCount(targetFrameCount), m_counters(counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{std::make_unique<NullGameState>(m_targetFrameCount, m_counters)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++m_counters.applicationShutdowns;
    }

  private:
    u64 m_targetFrameCount;
    LifecycleCounters& m_counters;
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
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
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
    bool firstContext = true;
    for (const Tina::Core::ErrorContext& context : error.context)
    {
        if (!firstContext)
        {
            std::cerr.put(',');
        }
        firstContext = false;
        std::cerr << "{\"operation\":";
        writeJsonString(std::cerr, context.operation);
        std::cerr << ",\"detail\":";
        writeJsonString(std::cerr, context.detail);
        std::cerr.put('}');
    }
    std::cerr << "]}\n";
}

[[nodiscard]] Tina::Core::Result<u64> parseTargetFrameCount(int argumentCount, char** arguments)
{
    constexpr std::string_view optionPrefix = "--frames=";
    if (argumentCount != 2)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Expected exactly one --frames=N argument"};
        error.addContext("tina_sample_null", "N must be an unsigned integer greater than zero");
        return Tina::Core::failure(std::move(error));
    }

    const std::string_view argument{arguments[1]};
    if (!argument.starts_with(optionPrefix))
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Unsupported command-line argument"};
        error.addContext("tina_sample_null", argument);
        return Tina::Core::failure(std::move(error));
    }

    const std::string_view valueText = argument.substr(optionPrefix.size());
    u64 value = 0;
    const auto [end, conversionError] = std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
    if (conversionError != std::errc{} || end != valueText.data() + valueText.size() || value == 0)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                "--frames must be an unsigned integer greater than zero"};
        error.addContext("tina_sample_null", valueText);
        return Tina::Core::failure(std::move(error));
    }
    return value;
}

[[nodiscard]] Tina::EngineCompositionFactories createEngineFactories()
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem = Tina::Task::createDisabledTaskSystem,
        .platformRender =
            Tina::IndependentPlatformRenderFactories{
                .createPlatformBackend = Tina::Platform::createHeadlessPlatformBackend,
                .createRenderDevice = Tina::Render::createNullRenderDevice,
            },
    };
}

[[nodiscard]] Tina::Core::Status verifyLifecycle(Tina::RunExitReason exitReason, u64 targetFrameCount,
                                                 const LifecycleCounters& counters)
{
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "The null sample stopped for an unexpected reason"};
        error.addContext("verifyLifecycle", "exitReason did not match the state request");
        return Tina::Core::failure(std::move(error));
    }
    if (counters.frameUpdates != targetFrameCount || counters.stateExits != 1 || counters.applicationShutdowns != 1)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "The null sample lifecycle counters did not match their contract"};
        error.addContext("verifyLifecycle",
                         "frameUpdates=" + std::to_string(counters.frameUpdates) +
                             ", stateExits=" + std::to_string(counters.stateExits) +
                             ", applicationShutdowns=" + std::to_string(counters.applicationShutdowns));
        return Tina::Core::failure(std::move(error));
    }
    return Tina::Core::success();
}

} // namespace

int main(int argumentCount, char** arguments)
{
    auto targetFrameCountResult = parseTargetFrameCount(argumentCount, arguments);
    if (!targetFrameCountResult)
    {
        writeError(targetFrameCountResult.error());
        return 2;
    }
    const u64 targetFrameCount = *targetFrameCountResult;

    auto hostResult = Tina::EngineHost::Create(Tina::EngineConfig::Defaults(), createEngineFactories());
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    LifecycleCounters counters;
    NullGameApplication application{targetFrameCount, counters};
    auto runResult = (*hostResult)->run(application);
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    auto lifecycleStatus = verifyLifecycle(*runResult, targetFrameCount, counters);
    if (!lifecycleStatus)
    {
        writeError(lifecycleStatus.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_null\",\"frames\":" << counters.frameUpdates
              << ",\"exit\":\"GameRequestedExitAfterCurrentFrame\",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns << "}\n";
    return 0;
}
