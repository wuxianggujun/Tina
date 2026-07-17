#include <tina/core/error/Error.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
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

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    if (code == Tina::Core::CoreErrorCode::InvalidArgument)
    {
        return "core.invalid_argument";
    }
    if (code == Tina::Core::CoreErrorCode::OutOfMemory)
    {
        return "core.out_of_memory";
    }
    if (code == Tina::Core::CoreErrorCode::Internal)
    {
        return "core.internal";
    }
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "." + std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_desktop\",\"code\":";
    writeJsonString(std::cerr, errorCodeName(error.code));
    std::cerr << ",\"tinaCode\":{\"domain\":" << static_cast<std::uint16_t>(error.code.domain)
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

template <typename Value> [[nodiscard]] bool parseUnsigned(std::string_view text, Value& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view framesPrefix = "--frames=";
    constexpr std::string_view delayPrefix = "--frame-delay-ms=";

    SampleOptions options;
    bool hasFrames = false;
    bool hasDelay = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(framesPrefix))
        {
            if (hasFrames)
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Duplicate --frames argument"};
                error.addContext("parseOptions", argument);
                return Tina::Core::failure(std::move(error));
            }
            const std::string_view valueText = argument.substr(framesPrefix.size());
            if (!parseUnsigned(valueText, options.targetFrameCount) || options.targetFrameCount == 0)
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                        "--frames must be an unsigned integer greater than zero"};
                error.addContext("parseOptions", valueText);
                return Tina::Core::failure(std::move(error));
            }
            hasFrames = true;
            continue;
        }
        if (argument.starts_with(delayPrefix))
        {
            if (hasDelay)
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                        "Duplicate --frame-delay-ms argument"};
                error.addContext("parseOptions", argument);
                return Tina::Core::failure(std::move(error));
            }
            const std::string_view valueText = argument.substr(delayPrefix.size());
            if (!parseUnsigned(valueText, options.frameDelayMilliseconds))
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                        "--frame-delay-ms must be an unsigned integer"};
                error.addContext("parseOptions", valueText);
                return Tina::Core::failure(std::move(error));
            }
            hasDelay = true;
            continue;
        }

        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Unsupported command-line argument"};
        error.addContext("parseOptions", argument);
        return Tina::Core::failure(std::move(error));
    }
    return options;
}

class DesktopSmokeState final : public Tina::IGameState {
  public:
    DesktopSmokeState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        ++counters_.stateEnters;
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

class DesktopSmokeApplication final : public Tina::IGameApplication {
  public:
    DesktopSmokeApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        std::unique_ptr<Tina::IGameState> state = std::make_unique<DesktopSmokeState>(options_, counters_);
        return state;
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_.applicationShutdowns;
    }

  private:
    SampleOptions options_;
    LifecycleCounters& counters_;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Desktop Smoke Sample";
    config.primaryWindow.title = "Tina Desktop Smoke Sample";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    return config;
}

[[nodiscard]] std::string runExitReasonName(Tina::RunExitReason exitReason)
{
    switch (exitReason)
    {
    case Tina::RunExitReason::GameRequestedExitAfterCurrentFrame:
        return "GameRequestedExitAfterCurrentFrame";
    case Tina::RunExitReason::PrimaryWindowRequestedClose:
        return "PrimaryWindowRequestedClose";
    case Tina::RunExitReason::GameStateStackBecameEmpty:
        return "GameStateStackBecameEmpty";
    }
    return "Unknown";
}

[[nodiscard]] Tina::Core::Status verifyLifecycle(Tina::RunExitReason exitReason, const SampleOptions& options,
                                                 const LifecycleCounters& counters)
{
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "The desktop smoke sample stopped for an unexpected reason"};
        error.addContext("verifyLifecycle", "exitReason=" + runExitReasonName(exitReason));
        return Tina::Core::failure(std::move(error));
    }
    if (counters.frameUpdates != options.targetFrameCount || counters.stateEnters != 1 || counters.stateExits != 1 ||
        counters.applicationShutdowns != 1)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "The desktop smoke sample lifecycle counters did not match their contract"};
        error.addContext("verifyLifecycle",
                         "frameUpdates=" + std::to_string(counters.frameUpdates) +
                             ", targetFrameCount=" + std::to_string(options.targetFrameCount) +
                             ", stateEnters=" + std::to_string(counters.stateEnters) +
                             ", stateExits=" + std::to_string(counters.stateExits) +
                             ", applicationShutdowns=" + std::to_string(counters.applicationShutdowns));
        return Tina::Core::failure(std::move(error));
    }
    return Tina::Core::success();
}

[[nodiscard]] int runSample(int argumentCount, char** arguments)
{
    auto optionsResult = parseOptions(argumentCount, arguments);
    if (!optionsResult)
    {
        writeError(optionsResult.error());
        return 2;
    }
    const SampleOptions options = *optionsResult;

    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig());
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    LifecycleCounters counters;
    DesktopSmokeApplication application{options, counters};
    auto runResult = (*hostResult)->run(application);
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    auto lifecycleStatus = verifyLifecycle(*runResult, options, counters);
    if (!lifecycleStatus)
    {
        writeError(lifecycleStatus.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_desktop\",\"frames\":" << counters.frameUpdates
              << ",\"targetFrames\":" << options.targetFrameCount
              << ",\"frameDelayMs\":" << options.frameDelayMilliseconds << ",\"exit\":";
    writeJsonString(std::cout, runExitReasonName(*runResult));
    std::cout << ",\"stateEnters\":" << counters.stateEnters << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns << "}\n";
    return 0;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory, "The desktop smoke sample ran out of memory"};
        error.addContext("tina_sample_desktop", "std::bad_alloc");
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the desktop smoke sample boundary"};
        error.addContext("tina_sample_desktop", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the desktop smoke sample boundary"};
        error.addContext("tina_sample_desktop", "unknown exception");
        writeError(error);
        return 1;
    }
}
