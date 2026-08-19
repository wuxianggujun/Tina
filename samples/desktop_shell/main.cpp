#include "DesktopShellUI.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
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

inline constexpr u32 ShellLogicalWidth = 1440;
inline constexpr u32 ShellLogicalHeight = 900;

struct SampleOptions final {
    u64 targetFrameCount = 0;
    u32 frameDelayMilliseconds = 0;
    u32 windowLogicalWidth = ShellLogicalWidth;
    u32 windowLogicalHeight = ShellLogicalHeight;
    Tina::SampleUI::ShellTheme initialTheme = Tina::SampleUI::ShellTheme::Dark;
    Tina::UI::UIDensity initialDensity = Tina::UI::UIDensity::Compact;
    bool autoDemo = false;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 uiRootsCreated = 0;
    u64 uiRootsReleased = 0;
    u32 logicalPixelWidth = ShellLogicalWidth;
    u32 logicalPixelHeight = ShellLogicalHeight;
    u32 framebufferPixelWidth = ShellLogicalWidth;
    u32 framebufferPixelHeight = ShellLogicalHeight;
    float contentScaleX = 1.0F;
    float contentScaleY = 1.0F;
    u64 windowMetricsEvents = 0;
    Tina::SampleUI::DesktopShellSnapshot finalUI{};
};

void writeJsonString(std::ostream& output, std::string_view value)
{
    constexpr char Hexadecimal[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value) {
        switch (byte) {
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
            if (byte < 0x20U) {
                output << "\\u00" << Hexadecimal[byte >> 4U] << Hexadecimal[byte & 0x0FU];
            } else {
                output.put(static_cast<char>(byte));
            }
            break;
        }
    }
    output.put('"');
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_desktop_shell\",\"domain\":"
              << static_cast<std::uint16_t>(error.code.domain) << ",\"code\":" << error.code.value
              << ",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
}

template <typename Value> [[nodiscard]] bool parseUnsigned(std::string_view text, Value& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view FramesPrefix = "--frames=";
    constexpr std::string_view DelayPrefix = "--frame-delay-ms=";
    constexpr std::string_view ThemePrefix = "--theme=";
    constexpr std::string_view DensityPrefix = "--density=";
    constexpr std::string_view WidthPrefix = "--width=";
    constexpr std::string_view HeightPrefix = "--height=";

    SampleOptions options{};
    bool hasWidth = false;
    bool hasHeight = false;
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(FramesPrefix)) {
            const std::string_view value = argument.substr(FramesPrefix.size());
            if (!parseUnsigned(value, options.targetFrameCount) || options.targetFrameCount == 0) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must be an unsigned integer greater than zero");
            }
            continue;
        }
        if (argument.starts_with(DelayPrefix)) {
            const std::string_view value = argument.substr(DelayPrefix.size());
            if (!parseUnsigned(value, options.frameDelayMilliseconds)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must be an unsigned integer");
            }
            continue;
        }
        if (argument.starts_with(ThemePrefix)) {
            const std::string_view value = argument.substr(ThemePrefix.size());
            if (value == "dark") {
                options.initialTheme = Tina::SampleUI::ShellTheme::Dark;
            } else if (value == "light") {
                options.initialTheme = Tina::SampleUI::ShellTheme::Light;
            } else {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--theme must be dark or light");
            }
            continue;
        }
        if (argument.starts_with(DensityPrefix)) {
            const std::string_view value = argument.substr(DensityPrefix.size());
            if (value == "compact") {
                options.initialDensity = Tina::UI::UIDensity::Compact;
            } else if (value == "comfortable") {
                options.initialDensity = Tina::UI::UIDensity::Comfortable;
            } else {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--density must be compact or comfortable");
            }
            continue;
        }
        if (argument.starts_with(WidthPrefix)) {
            const std::string_view value = argument.substr(WidthPrefix.size());
            if (!parseUnsigned(value, options.windowLogicalWidth) ||
                options.windowLogicalWidth < 960U || options.windowLogicalWidth > 3840U) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--width must be in the range 960..3840");
            }
            hasWidth = true;
            continue;
        }
        if (argument.starts_with(HeightPrefix)) {
            const std::string_view value = argument.substr(HeightPrefix.size());
            if (!parseUnsigned(value, options.windowLogicalHeight) ||
                options.windowLogicalHeight < 640U || options.windowLogicalHeight > 2160U) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--height must be in the range 640..2160");
            }
            hasHeight = true;
            continue;
        }
        if (argument == "--auto-demo") {
            options.autoDemo = true;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Unsupported desktop shell command-line argument");
    }
    if (hasWidth != hasHeight) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--width and --height must be supplied together");
    }
    if (options.autoDemo && options.targetFrameCount != 0 && options.targetFrameCount < 60) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--auto-demo requires at least 60 frames when --frames is supplied");
    }
    return options;
}

[[nodiscard]] std::string_view themeName(Tina::SampleUI::ShellTheme theme) noexcept
{
    return theme == Tina::SampleUI::ShellTheme::Dark ? "dark" : "light";
}

[[nodiscard]] std::string_view densityName(Tina::UI::UIDensity density) noexcept
{
    return density == Tina::UI::UIDensity::Compact ? "compact" : "comfortable";
}

[[nodiscard]] std::string_view tierName(Tina::SampleUI::ShellResponsiveTier tier) noexcept
{
    switch (tier) {
    case Tina::SampleUI::ShellResponsiveTier::Full:
        return "full";
    case Tina::SampleUI::ShellResponsiveTier::Compressed:
        return "compressed";
    case Tina::SampleUI::ShellResponsiveTier::Minimal:
        return "minimal";
    }
    return "unknown";
}

[[nodiscard]] std::string_view exitReasonName(Tina::RunExitReason reason) noexcept
{
    switch (reason) {
    case Tina::RunExitReason::GameRequestedExitAfterCurrentFrame:
        return "GameRequestedExitAfterCurrentFrame";
    case Tina::RunExitReason::PrimaryWindowRequestedClose:
        return "PrimaryWindowRequestedClose";
    case Tina::RunExitReason::GameStateStackBecameEmpty:
        return "GameStateStackBecameEmpty";
    }
    return "Unknown";
}

class ShellState final : public Tina::IGameState {
  public:
    ShellState(SampleOptions options, Tina::SampleUI::DesktopShellState& shellState,
               LifecycleCounters& counters) noexcept
        : options_(options), shellState_(shellState), counters_(counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_.stateEnters;
        if (Tina::Core::Status status = ui_.build(context, shellState_); !status) {
            return status;
        }
        ui_.setLogicalWidth(static_cast<float>(counters_.logicalPixelWidth));
        ++counters_.uiRootsCreated;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++counters_.stateExits;
        ui_.release();
        counters_.finalUI = ui_.snapshot();
        ++counters_.uiRootsReleased;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_.frameUpdates;
        ui_.setLogicalWidth(static_cast<float>(counters_.logicalPixelWidth));
        if (options_.autoDemo) {
            ui_.requestAutomatedStep(counters_.frameUpdates);
        }
        if (options_.targetFrameCount != 0 && counters_.frameUpdates >= options_.targetFrameCount) {
            context.requestExitAfterFrame();
        }
        if (options_.frameDelayMilliseconds != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override
    {
        return ui_.update(context);
    }

  private:
    SampleOptions options_{};
    Tina::SampleUI::DesktopShellState& shellState_;
    LifecycleCounters& counters_;
    Tina::SampleUI::DesktopShellUI ui_{};
};

class ShellApplication final : public Tina::IGameApplication {
  public:
    ShellApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(counters)
    {
        shellState_.theme = options.initialTheme;
        shellState_.density = options.initialDensity;
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext& context) override
    {
        const auto& window = context.engineConfig().primaryWindow;
        counters_.logicalPixelWidth = window.initialLogicalExtent.width;
        counters_.logicalPixelHeight = window.initialLogicalExtent.height;
        counters_.framebufferPixelWidth = window.initialLogicalExtent.width;
        counters_.framebufferPixelHeight = window.initialLogicalExtent.height;
        auto subscription = context.platformEventSubscriptions().subscribe(
            [this](const Tina::PlatformEventNotification& notification) {
                if (!std::holds_alternative<Tina::Platform::WindowMetricsChangedEvent>(
                        notification.event().payload)) {
                    return;
                }
                ++counters_.windowMetricsEvents;
                if (const auto* metrics = notification.primaryWindowMetrics(); metrics != nullptr) {
                    counters_.logicalPixelWidth = metrics->logicalExtent.width;
                    counters_.logicalPixelHeight = metrics->logicalExtent.height;
                    counters_.framebufferPixelWidth = metrics->framebufferExtent.width;
                    counters_.framebufferPixelHeight = metrics->framebufferExtent.height;
                    counters_.contentScaleX = metrics->contentScale.x;
                    counters_.contentScaleY = metrics->contentScale.y;
                }
            });
        if (!subscription) {
            return Tina::Core::failure(std::move(subscription.error()));
        }
        platformEvents_.emplace(std::move(*subscription));
        std::unique_ptr<Tina::IGameState> state =
            std::make_unique<ShellState>(options_, shellState_, counters_);
        return state;
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        platformEvents_.reset();
        ++counters_.applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters& counters_;
    Tina::SampleUI::DesktopShellState shellState_{};
    std::optional<Tina::PlatformEventSubscription> platformEvents_{};
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(const SampleOptions& options)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Desktop Shell";
    config.primaryWindow.title = "Tina Desktop Shell";
    config.primaryWindow.initialLogicalExtent = {options.windowLogicalWidth,
                                                 options.windowLogicalHeight};
    config.primaryWindow.resizable = true;
    config.primaryWindow.initiallyVisible = true;

    config.primaryWindowUICapacities = Tina::UI::UIContextCapacityConfig{
        .nodeCapacity = 1024,
        // Exactly one root is ever live; a density rebuild releases the outgoing
        // root before the replacement state builds.
        .rootCapacity = 1,
        .dirtyQueueCapacity = 256,
        .layoutSnapshotCapacity = 512,
        .hitSnapshotCapacity = 256,
        .paintSnapshotCapacity = 1024,
        .routePathCapacity = 128,
        .routedPointerListenerCapacity = 32,
        .buttonActionCapacity = 32,
        .textByteCapacity = 16U * 1024U,
        .applyDefaultProductChrome = true,
    };
    config.primaryWindowUIDisplayListCapacities = Tina::PrimaryWindowUIDisplayListCapacityConfig{
        .commandCapacity = 2048,
        .clipCapacity = 256,
        .batchCapacity = 512,
    };
    return config;
}

[[nodiscard]] Tina::Core::Status verifyRun(Tina::RunExitReason reason, const SampleOptions& options,
                                           const LifecycleCounters& counters)
{
    const Tina::SampleUI::DesktopShellSnapshot& ui = counters.finalUI;
    if (counters.stateEnters != 1 || counters.stateExits != 1 ||
        counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiRootsReleased != 1 || ui.rootAlive) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell lifecycle verification failed");
    }
    // Three nested SplitViews and five bands are the frozen shell structure.
    if (ui.splitViewCount != 3 || ui.bandCount != 5 || ui.commandCount != 6 ||
        ui.inspectorRowCount != 4) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell structure inventory verification failed: splitViews=" +
                                       std::to_string(ui.splitViewCount) + " bands=" +
                                       std::to_string(ui.bandCount) + " commands=" +
                                       std::to_string(ui.commandCount) + " inspectorRows=" +
                                       std::to_string(ui.inspectorRowCount));
    }
    // The viewport owns the remaining area and is never covered by a dock.
    if (!ui.viewportUnobstructed || ui.viewportWidth < 480.0F || ui.viewportHeight < 320.0F) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Desktop shell viewport verification failed: width=" + std::to_string(ui.viewportWidth) +
                " height=" + std::to_string(ui.viewportHeight) +
                " unobstructed=" + (ui.viewportUnobstructed ? "true" : "false"));
    }
    if (ui.leftDockWidth < 200.0F || ui.statusBarHeight <= 0.0F) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell dock/status geometry verification failed: dock=" +
                                       std::to_string(ui.leftDockWidth) + " status=" +
                                       std::to_string(ui.statusBarHeight));
    }
    // Responsive tier must match the committed logical width, and the panes it
    // governs must agree with it.
    const auto width = static_cast<float>(counters.logicalPixelWidth);
    const bool expectFull = width >= 1280.0F;
    const bool expectMinimal = width < 960.0F;
    if (expectFull && (ui.tier != Tina::SampleUI::ShellResponsiveTier::Full ||
                       ui.timelineCollapsed || !ui.inspectorVisible)) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell full-tier responsive verification failed");
    }
    if (!expectFull && !expectMinimal &&
        (ui.tier != Tina::SampleUI::ShellResponsiveTier::Compressed || !ui.timelineCollapsed ||
         !ui.inspectorVisible)) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell compressed-tier responsive verification failed");
    }
    if (ui.tier == Tina::SampleUI::ShellResponsiveTier::Full && ui.timelineHeight < 160.0F) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell timeline minimum verification failed: height=" +
                                       std::to_string(ui.timelineHeight));
    }
    if (ui.inspectorVisible && ui.inspectorWidth < 240.0F) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell inspector minimum verification failed: width=" +
                                       std::to_string(ui.inspectorWidth));
    }
    if (ui.theme != options.initialTheme || ui.density != options.initialDensity) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell theme/density verification failed");
    }
    // Keyboard-only entry point: the Context rejects a focus target that is not
    // an enabled committed candidate, so this proves the command bar is focusable.
    if (!ui.initialFocusApplied || !ui.focusedNode.hasValue()) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell initial focus verification failed");
    }
    if (options.autoDemo) {
        // Menu open was read back from the Menu store; dialog open/dismiss were
        // read back from committed Modal geometry.
        if (!ui.menuOpenObserved || !ui.dialogOpenObserved || !ui.dialogDismissed) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Desktop shell overlay workflow verification failed: menu=" +
                    std::string(ui.menuOpenObserved ? "true" : "false") + " dialogOpen=" +
                    std::string(ui.dialogOpenObserved ? "true" : "false") + " dialogDismissed=" +
                    std::string(ui.dialogDismissed ? "true" : "false"));
        }
        // Explicit pane commands took effect while requested.
        if (!ui.timelineHideObserved || !ui.inspectorHideObserved) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Desktop shell explicit pane command verification failed: timeline=" +
                    std::string(ui.timelineHideObserved ? "true" : "false") + " inspector=" +
                    std::string(ui.inspectorHideObserved ? "true" : "false"));
        }
        // Overlays closed and command intent cleared. Resolved pane visibility is
        // then governed by the width tier alone, not by leftover intent.
        if (ui.menuOpen || ui.dialogOpen || ui.timelineHideRequested ||
            ui.inspectorHideRequested) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Desktop shell automated restore verification failed");
        }
        const bool tierCollapsesTimeline = ui.tier != Tina::SampleUI::ShellResponsiveTier::Full;
        if (ui.timelineCollapsed != tierCollapsesTimeline || !ui.inspectorVisible) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Desktop shell tier-resolved pane visibility verification failed");
        }
    }
    if (counters.logicalPixelWidth > options.windowLogicalWidth ||
        counters.logicalPixelHeight > options.windowLogicalHeight ||
        counters.logicalPixelWidth == 0 || counters.logicalPixelHeight == 0) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Desktop shell window extent verification failed: logical=" +
                                       std::to_string(counters.logicalPixelWidth) + "x" +
                                       std::to_string(counters.logicalPixelHeight));
    }
    if (options.targetFrameCount != 0) {
        if (reason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
            counters.frameUpdates != options.targetFrameCount) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Desktop shell frame-bounded run did not reach its requested exit");
        }
    } else if (reason != Tina::RunExitReason::PrimaryWindowRequestedClose) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Interactive desktop shell stopped without a window close request");
    }
    return Tina::Core::success();
}

[[nodiscard]] int runSample(int argumentCount, char** arguments)
{
    auto options = parseOptions(argumentCount, arguments);
    if (!options) {
        writeError(options.error());
        return 2;
    }

    auto host = Tina::Desktop::CreateEngine(createEngineConfig(*options));
    if (!host) {
        writeError(host.error());
        return 1;
    }

    LifecycleCounters counters{};
    ShellApplication application{*options, counters};
    auto result = (*host)->run(application);
    if (!result) {
        writeError(result.error());
        return 1;
    }
    if (Tina::Core::Status status = verifyRun(*result, *options, counters); !status) {
        writeError(status.error());
        return 1;
    }

    const Tina::SampleUI::DesktopShellSnapshot& ui = counters.finalUI;
    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_desktop_shell\",\"frames\":"
              << counters.frameUpdates << ",\"theme\":";
    writeJsonString(std::cout, themeName(ui.theme));
    std::cout << ",\"density\":";
    writeJsonString(std::cout, densityName(ui.density));
    std::cout << ",\"tier\":";
    writeJsonString(std::cout, tierName(ui.tier));
    std::cout << ",\"splitViews\":" << ui.splitViewCount << ",\"bands\":" << ui.bandCount
              << ",\"commands\":" << ui.commandCount
              << ",\"inspectorRows\":" << ui.inspectorRowCount
              << ",\"leftDockWidth\":" << ui.leftDockWidth
              << ",\"viewportWidth\":" << ui.viewportWidth
              << ",\"viewportHeight\":" << ui.viewportHeight
              << ",\"inspectorWidth\":" << ui.inspectorWidth
              << ",\"timelineHeight\":" << ui.timelineHeight
              << ",\"statusBarHeight\":" << ui.statusBarHeight
              << ",\"viewportUnobstructed\":" << (ui.viewportUnobstructed ? "true" : "false")
              << ",\"autoDemo\":" << (options->autoDemo ? "true" : "false")
              << ",\"initialFocusApplied\":" << (ui.initialFocusApplied ? "true" : "false")
              << ",\"menuOpenObserved\":" << (ui.menuOpenObserved ? "true" : "false")
              << ",\"dialogOpenObserved\":" << (ui.dialogOpenObserved ? "true" : "false")
              << ",\"dialogDismissed\":" << (ui.dialogDismissed ? "true" : "false")
              << ",\"commandActivations\":" << ui.commandActivations
              << ",\"documentSwitches\":" << ui.documentSwitches
              << ",\"timelineCollapsed\":" << (ui.timelineCollapsed ? "true" : "false")
              << ",\"inspectorVisible\":" << (ui.inspectorVisible ? "true" : "false")
              << ",\"logicalPixelWidth\":" << counters.logicalPixelWidth
              << ",\"logicalPixelHeight\":" << counters.logicalPixelHeight
              << ",\"framebufferPixelWidth\":" << counters.framebufferPixelWidth
              << ",\"framebufferPixelHeight\":" << counters.framebufferPixelHeight
              << ",\"contentScaleX\":" << counters.contentScaleX
              << ",\"contentScaleY\":" << counters.contentScaleY
              << ",\"stateEnters\":" << counters.stateEnters
              << ",\"uiRootsReleased\":" << counters.uiRootsReleased << ",\"exit\":";
    writeJsonString(std::cout, exitReasonName(*result));
    std::cout << "}\n";
    return 0;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&) {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::OutOfMemory,
                                     "The desktop shell ran out of memory"});
        return 1;
    } catch (const std::exception& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the desktop shell boundary"};
        error.addContext("tina_sample_desktop_shell",
                         exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...) {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::Internal,
                                     "A non-standard exception crossed the desktop shell boundary"});
        return 1;
    }
}
