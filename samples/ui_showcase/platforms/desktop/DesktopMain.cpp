#include "ShowcaseUI.hpp"
#include "ShowcaseImageFixture.hpp"
#include "ShowcaseRenderDevice.hpp"

#include "SampleSpriteFrameResource.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
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

inline constexpr u64 ImageAtlasInvalidationFrame = 10;
inline constexpr u64 ImageResolverUnbindFrame = 20;
inline constexpr u32 ShowcaseLogicalWidth = 1280;
inline constexpr u32 ShowcaseLogicalHeight = 800;

struct SampleOptions final {
    u64 targetFrameCount = 0;
    u32 frameDelayMilliseconds = 0;
    u32 windowLogicalWidth = ShowcaseLogicalWidth;
    u32 windowLogicalHeight = ShowcaseLogicalHeight;
    Tina::SampleUI::ShowcaseTheme initialTheme = Tina::SampleUI::ShowcaseTheme::Dark;
    Tina::UI::UIDensity initialDensity = Tina::UI::UIDensity::Comfortable;
    bool autoDemo = false;
    bool imageLifecycleDemo = false;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 uiRootsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 densityRebuilds = 0;
    u64 imageResolverCalls = 0;
    u64 imageResolverHits = 0;
    u64 imageResolverUnavailable = 0;
    u64 imageResolverCallsAtUnbind = 0;
    u64 windowMetricsEvents = 0;
    u32 logicalPixelWidth = ShowcaseLogicalWidth;
    u32 logicalPixelHeight = ShowcaseLogicalHeight;
    u32 framebufferPixelWidth = ShowcaseLogicalWidth;
    u32 framebufferPixelHeight = ShowcaseLogicalHeight;
    float contentScaleX = 1.0F;
    float contentScaleY = 1.0F;
    u32 imageFrameBorrowsAtRelease = 0;
    bool imageAtlasUploaded = false;
    bool imageAtlasReleased = false;
    bool imageAtlasValidatedBeforeRelease = false;
    bool imageAtlasInvalidatedAfterRelease = false;
    bool imageResolverUnbound = false;
    Tina::SampleUI::ShowcaseUISnapshot finalUI{};
};

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_ui_showcase");
    writer.member("domain", static_cast<std::uint16_t>(error.code.domain));
    writer.member("code", error.code.value);
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
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
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasTheme = false;
    bool hasDensity = false;
    bool hasWidth = false;
    bool hasHeight = false;
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(FramesPrefix)) {
            if (hasFrames) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "Duplicate --frames argument");
            }
            const std::string_view value = argument.substr(FramesPrefix.size());
            if (!Tina::Core::parseArgUnsigned(value, options.targetFrameCount) || options.targetFrameCount == 0) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must be an unsigned integer greater than zero");
            }
            hasFrames = true;
            continue;
        }
        if (argument.starts_with(DelayPrefix)) {
            if (hasDelay) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --frame-delay-ms argument");
            }
            const std::string_view value = argument.substr(DelayPrefix.size());
            if (!Tina::Core::parseArgUnsigned(value, options.frameDelayMilliseconds)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must be an unsigned integer");
            }
            hasDelay = true;
            continue;
        }
        if (argument.starts_with(ThemePrefix)) {
            if (hasTheme) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "Duplicate --theme argument");
            }
            const std::string_view value = argument.substr(ThemePrefix.size());
            if (value == "dark") {
                options.initialTheme = Tina::SampleUI::ShowcaseTheme::Dark;
            } else if (value == "light") {
                options.initialTheme = Tina::SampleUI::ShowcaseTheme::Light;
            } else {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "--theme must be dark or light");
            }
            hasTheme = true;
            continue;
        }
        if (argument.starts_with(DensityPrefix)) {
            if (hasDensity) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --density argument");
            }
            const std::string_view value = argument.substr(DensityPrefix.size());
            if (value == "compact") {
                options.initialDensity = Tina::UI::UIDensity::Compact;
            } else if (value == "comfortable") {
                options.initialDensity = Tina::UI::UIDensity::Comfortable;
            } else {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--density must be compact or comfortable");
            }
            hasDensity = true;
            continue;
        }
        if (argument == "--auto-demo") {
            if (options.autoDemo) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --auto-demo argument");
            }
            options.autoDemo = true;
            continue;
        }
        if (argument.starts_with(WidthPrefix)) {
            if (hasWidth) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "Duplicate --width argument");
            }
            const std::string_view value = argument.substr(WidthPrefix.size());
            if (!Tina::Core::parseArgUnsigned(value, options.windowLogicalWidth) || options.windowLogicalWidth < 960U ||
                options.windowLogicalWidth > 3840U) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                            "--width must be in the range 960..3840");
            }
            hasWidth = true;
            continue;
        }
        if (argument.starts_with(HeightPrefix)) {
            if (hasHeight) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "Duplicate --height argument");
            }
            const std::string_view value = argument.substr(HeightPrefix.size());
            if (!Tina::Core::parseArgUnsigned(value, options.windowLogicalHeight) ||
                options.windowLogicalHeight < 640U || options.windowLogicalHeight > 2160U) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                            "--height must be in the range 640..2160");
            }
            hasHeight = true;
            continue;
        }
        if (argument == "--image-lifecycle-demo") {
            if (options.imageLifecycleDemo) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --image-lifecycle-demo argument");
            }
            options.imageLifecycleDemo = true;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Unsupported UI showcase command-line argument");
    }

    if (options.autoDemo && options.targetFrameCount != 0 && options.targetFrameCount < 120) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--auto-demo requires at least 120 frames when --frames is supplied");
    }
    if (options.imageLifecycleDemo && options.autoDemo) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--image-lifecycle-demo cannot be combined with --auto-demo");
    }
    if (options.imageLifecycleDemo && options.targetFrameCount < 30) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--image-lifecycle-demo requires --frames of at least 30");
    }
    if (hasWidth != hasHeight) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--width and --height must be supplied together");
    }
    return options;
}

[[nodiscard]] std::string_view themeName(Tina::SampleUI::ShowcaseTheme theme) noexcept
{
    return theme == Tina::SampleUI::ShowcaseTheme::Dark ? "dark" : "light";
}

[[nodiscard]] std::string_view densityName(Tina::UI::UIDensity density) noexcept
{
    return density == Tina::UI::UIDensity::Compact ? "compact" : "comfortable";
}

[[nodiscard]] std::string_view qualityName(Tina::SampleUI::ShowcaseQuality quality) noexcept
{
    switch (quality) {
    case Tina::SampleUI::ShowcaseQuality::Performance:
        return "performance";
    case Tina::SampleUI::ShowcaseQuality::Balanced:
        return "balanced";
    case Tina::SampleUI::ShowcaseQuality::Quality:
        return "quality";
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

class ShowcaseImageResources final {
  public:
    ShowcaseImageResources(Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess,
                           LifecycleCounters& counters) noexcept
        : deviceAccess_(&deviceAccess), counters_(&counters)
    {
    }

    ShowcaseImageResources(const ShowcaseImageResources&) = delete;
    ShowcaseImageResources& operator=(const ShowcaseImageResources&) = delete;

    ~ShowcaseImageResources() noexcept
    {
        if (texture_) {
            release();
        }
        if (frameResource_.frameBorrowCount() != 0) {
            std::terminate();
        }
    }

    [[nodiscard]] Tina::Core::Status initialize()
    {
        if (texture_ || bindingKey_ != 0) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "UI showcase image resources are already initialized");
        }
        Tina::Render::IRenderDevice* device = deviceAccess_->get();
        if (device == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "UI showcase render device is unavailable during state enter");
        }

        static constexpr Tina::SampleUI::ShowcaseAtlasPixels AtlasPixels =
            Tina::SampleUI::makeShowcaseAtlasPixels();
        const std::array<Tina::Render::Texture2DUploadLevel, 1> levels{
            Tina::Render::Texture2DUploadLevel{
                .width = static_cast<Tina::Core::u16>(Tina::SampleUI::ShowcaseAtlasWidth),
                .height = static_cast<Tina::Core::u16>(Tina::SampleUI::ShowcaseAtlasHeight),
                .bytes = AtlasPixels}};
        auto uploaded =
            device->createTexture2D(Tina::Render::Texture2DUploadDesc{.levels = levels});
        if (!uploaded) {
            return Tina::Core::failure(std::move(uploaded.error()));
        }
        texture_ = *uploaded;

        auto binding = device->createTexture2DBinding(texture_);
        if (!binding) {
            (void)device->destroyTexture2D(texture_);
            texture_ = {};
            return Tina::Core::failure(std::move(binding.error()));
        }
        bindingKey_ = *binding;
        counters_->imageAtlasUploaded = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Render::Texture2DFrameResourceResolver resolver() noexcept
    {
        return Tina::Render::Texture2DFrameResourceResolver{
            .userData = this,
            .resolve = &ShowcaseImageResources::resolve,
        };
    }

    void release() noexcept
    {
        counters_->imageFrameBorrowsAtRelease = frameResource_.frameBorrowCount();
        Tina::Render::IRenderDevice* device = deviceAccess_->get();
        if (texture_ && device != nullptr) {
            counters_->imageAtlasValidatedBeforeRelease =
                static_cast<bool>(device->validateTexture2D(texture_));
            counters_->imageAtlasReleased = static_cast<bool>(device->destroyTexture2D(texture_));
            if (counters_->imageAtlasReleased) {
                counters_->imageAtlasInvalidatedAfterRelease =
                    !device->validateTexture2D(texture_);
            }
        }
        texture_ = {};
        bindingKey_ = 0;
    }

  private:
    [[nodiscard]] static Tina::Core::Result<std::optional<Tina::Render::Texture2DFrameResourceResolution>>
    resolve(void* userData, Tina::Core::AssetId asset,
            Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto* resources = static_cast<ShowcaseImageResources*>(userData);
        if (resources == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "UI showcase image resolver received null user data");
        }
        ++resources->counters_->imageResolverCalls;
        if (asset != Tina::SampleUI::showcaseAtlasAssetId() || resources->bindingKey_ == 0) {
            ++resources->counters_->imageResolverUnavailable;
            return std::optional<Tina::Render::Texture2DFrameResourceResolution>{};
        }

        auto frameResource = resources->frameResource_.intern(sink, resources->bindingKey_);
        if (!frameResource) {
            return Tina::Core::failure(std::move(frameResource.error()));
        }
        ++resources->counters_->imageResolverHits;
        return std::optional<Tina::Render::Texture2DFrameResourceResolution>{
            Tina::Render::Texture2DFrameResourceResolution{
                .resource = *frameResource,
                .pixelWidth = Tina::SampleUI::ShowcaseAtlasWidth,
                .pixelHeight = Tina::SampleUI::ShowcaseAtlasHeight,
            }};
    }

    Tina::SampleUI::ShowcaseRenderDeviceAccess* deviceAccess_ = nullptr;
    LifecycleCounters* counters_ = nullptr;
    Tina::Render::GpuTextureId texture_{};
    u32 bindingKey_ = 0;
    Tina::Samples::SampleSpriteFrameResource frameResource_{};
};

void mergeShowcaseSnapshot(Tina::SampleUI::ShowcaseUISnapshot& aggregate,
                           Tina::SampleUI::ShowcaseUISnapshot latest) noexcept
{
    latest.themeSwitches += aggregate.themeSwitches;
    latest.densitySwitchRequests += aggregate.densitySwitchRequests;
    latest.buttonActivations += aggregate.buttonActivations;
    latest.sliderChanges += aggregate.sliderChanges;
    latest.treeExpansionChanges += aggregate.treeExpansionChanges;
    latest.styleTokenUpdates += aggregate.styleTokenUpdates;
    latest.motionBegins += aggregate.motionBegins;
    latest.stylesheetInstalled =
        latest.stylesheetInstalled || aggregate.stylesheetInstalled;
    latest.multilineNotesScrolled =
        latest.multilineNotesScrolled || aggregate.multilineNotesScrolled;
    latest.desktopWorkbench = latest.desktopWorkbench || aggregate.desktopWorkbench;
    aggregate = latest;
}

class ShowcaseState final : public Tina::IGameState {
  public:
    ShowcaseState(SampleOptions options, Tina::SampleUI::ShowcaseUIState& uiState,
                   LifecycleCounters& counters,
                   Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess) noexcept
        : options_(options), uiState_(uiState), counters_(counters),
          deviceAccess_(deviceAccess), imageResources_(deviceAccess, counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_.stateEnters;
        if (Tina::Core::Status status = imageResources_.initialize(); !status) {
            return status;
        }
        if (Tina::Core::Status status =
                ui_.build(context, uiState_, options_.initialTheme,
                          options_.initialDensity, imageResources_.resolver());
            !status) {
            imageResources_.release();
            return status;
        }
        ++counters_.uiRootsCreated;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++counters_.stateExits;
        ui_.release();
        imageResources_.release();
        mergeShowcaseSnapshot(counters_.finalUI, ui_.snapshot());
        ++counters_.uiRootsReleased;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_.frameUpdates;
        if (options_.autoDemo) {
            ui_.requestAutomatedStep(counters_.frameUpdates);
        }
        if (auto density = ui_.takeDensityRebuildRequest(); density.has_value()) {
            uiState_.density = *density;
            ++counters_.densityRebuilds;
            auto replacement = std::make_unique<ShowcaseState>(
                options_, uiState_, counters_, deviceAccess_);
            // Density binds at root construction and the Context rejects a
            // density change while any root is live. GameState replacement
            // enters the replacement before exiting this state, so this root is
            // released first and the replacement builds at zero live roots.
            // Application-owned ShowcaseUIState carries the UI state across.
            ui_.release();
            densityHandoffRequested_ = true;
            return context.requestReplace(std::move(replacement));
        }
        if (options_.imageLifecycleDemo && counters_.frameUpdates == ImageAtlasInvalidationFrame) {
            imageResources_.release();
        }
        if (options_.imageLifecycleDemo && counters_.frameUpdates == ImageResolverUnbindFrame) {
            counters_.imageResolverCallsAtUnbind = counters_.imageResolverCalls;
            counters_.imageResolverUnbound = ui_.unbindImageResolver();
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
        // Reached only when a requested density handoff did not commit: this
        // state already released its root and the replacement never took over.
        // Fail closed instead of presenting a rootless window.
        if (densityHandoffRequested_) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "UI showcase density handoff did not commit; the root was already released");
        }
        return ui_.update(context);
    }

  private:
    SampleOptions options_{};
    Tina::SampleUI::ShowcaseUIState& uiState_;
    LifecycleCounters& counters_;
    Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess_;
    ShowcaseImageResources imageResources_;
    Tina::SampleUI::ShowcaseUI ui_{};
    bool densityHandoffRequested_ = false;
};

class ShowcaseApplication final : public Tina::IGameApplication {
  public:
    ShowcaseApplication(SampleOptions options, LifecycleCounters& counters,
                         Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess) noexcept
        : options_(options), counters_(counters), deviceAccess_(deviceAccess)
    {
        uiState_.theme = options.initialTheme;
        uiState_.density = options.initialDensity;
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext& context) override
    {
        const auto& window = context.engineConfig().primaryWindow;
        counters_.logicalPixelWidth = window.initialLogicalExtent.width;
        counters_.logicalPixelHeight = window.initialLogicalExtent.height;
        counters_.framebufferPixelWidth = window.initialLogicalExtent.width;
        counters_.framebufferPixelHeight = window.initialLogicalExtent.height;
        counters_.contentScaleX = 1.0F;
        counters_.contentScaleY = 1.0F;
        auto subscription = context.platformEventSubscriptions().subscribe(
            [this](const Tina::PlatformEventNotification& notification) {
                if (!std::holds_alternative<Tina::Platform::WindowMetricsChangedEvent>(notification.event().payload)) {
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
            std::make_unique<ShowcaseState>(options_, uiState_, counters_, deviceAccess_);
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
    Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess_;
    Tina::SampleUI::ShowcaseUIState uiState_{};
    std::optional<Tina::PlatformEventSubscription> platformEvents_{};
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(const SampleOptions& options)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Modern Desktop Workbench";
    config.primaryWindow.title = "Tina Modern Desktop Workbench";
    config.primaryWindow.initialLogicalExtent = {options.windowLogicalWidth, options.windowLogicalHeight};
    config.primaryWindow.resizable = true;
    config.primaryWindow.initiallyVisible = true;

    config.primaryWindowUICapacities = Tina::UI::UIContextCapacityConfig{
        .nodeCapacity = 2048,
        // A density rebuild releases the outgoing root before the replacement
        // state builds, because the Context rejects a density change while any
        // root is live. Exactly one root is therefore ever live.
        .rootCapacity = 1,
        .dirtyQueueCapacity = 256,
        .layoutSnapshotCapacity = 256,
        .hitSnapshotCapacity = 256,
        .paintSnapshotCapacity = 2048,
        .routePathCapacity = 128,
        .routedPointerListenerCapacity = 64,
        .buttonActionCapacity = 64,
        .textByteCapacity = 32U * 1024U,
        .applyDefaultProductChrome = true,
    };
    config.primaryWindowUIDisplayListCapacities = Tina::PrimaryWindowUIDisplayListCapacityConfig{
        .commandCapacity = 4096,
        .clipCapacity = 512,
        .batchCapacity = 1024,
    };
    return config;
}

[[nodiscard]] Tina::Core::Status verifyRun(Tina::RunExitReason reason, const SampleOptions& options,
                                           const LifecycleCounters& counters,
                                           const Tina::SampleUI::ShowcaseRenderEvidence& renderEvidence)
{
    const auto surfaceAxisMatches = [](u32 logicalPixels, u32 framebufferPixels, float contentScale) noexcept {
        if (!std::isfinite(contentScale) || contentScale <= 0.0F || framebufferPixels == 0) {
            return false;
        }
        const double logical = static_cast<double>(logicalPixels);
        const double framebuffer = static_cast<double>(framebufferPixels);
        return std::abs(framebuffer - logical * static_cast<double>(contentScale)) <= 2.0 ||
               std::abs(framebuffer - logical) <= 2.0;
    };
    const u64 expectedLifecycleCount = 1 + counters.densityRebuilds;
    if (counters.stateEnters != expectedLifecycleCount ||
        counters.stateExits != expectedLifecycleCount || counters.applicationShutdowns != 1 ||
        counters.uiRootsCreated != expectedLifecycleCount ||
        counters.uiRootsReleased != expectedLifecycleCount || counters.finalUI.rootAlive ||
        counters.finalUI.controlCount != 24 || counters.finalUI.imageProductCount != 5 ||
        counters.finalUI.asymmetricCornerProductCount != 3 ||
        counters.finalUI.componentProfileCount != 3 ||
        counters.finalUI.workbenchBandCount != 5 || !counters.finalUI.desktopWorkbench ||
        !counters.finalUI.stylesheetInstalled || counters.finalUI.styleTokenUpdates == 0) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "UI workbench lifecycle, structure, stylesheet, or component inventory verification failed");
    }
    // The window manager may clamp the requested logical extent to the usable
    // desktop area (a high logical height at 200% scale exceeds the work area).
    // A clamp is a host constraint, not a product defect, so only reject an
    // extent larger than requested or a broken logical/framebuffer relation.
    if (counters.logicalPixelWidth > options.windowLogicalWidth ||
        counters.logicalPixelHeight > options.windowLogicalHeight ||
        counters.logicalPixelWidth == 0 || counters.logicalPixelHeight == 0 ||
        !surfaceAxisMatches(counters.logicalPixelWidth, counters.framebufferPixelWidth, counters.contentScaleX) ||
        !surfaceAxisMatches(counters.logicalPixelHeight, counters.framebufferPixelHeight, counters.contentScaleY)) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "UI showcase logical/framebuffer/content-scale verification failed: logical=" +
                std::to_string(counters.logicalPixelWidth) + "x" + std::to_string(counters.logicalPixelHeight) +
                " framebuffer=" + std::to_string(counters.framebufferPixelWidth) + "x" +
                std::to_string(counters.framebufferPixelHeight) + " scale=" +
                std::to_string(counters.contentScaleX) + "x" + std::to_string(counters.contentScaleY) +
                " requested=" + std::to_string(options.windowLogicalWidth) + "x" +
                std::to_string(options.windowLogicalHeight));
    }
    if (!counters.imageAtlasUploaded || !counters.imageAtlasReleased ||
        !counters.imageAtlasValidatedBeforeRelease || !counters.imageAtlasInvalidatedAfterRelease ||
        counters.imageFrameBorrowsAtRelease != 0 || counters.imageResolverCalls == 0 ||
        renderEvidence.submittedFrames == 0 || renderEvidence.submittedImageFrames == 0 ||
        renderEvidence.maxImageQuadCommands < 12 ||
        renderEvidence.maxImageBatches < 3 || renderEvidence.maxUniqueImageResources != 1 ||
        renderEvidence.lastPaintOrderChecksum == 0 || !renderEvidence.sawLinearSampling ||
        !renderEvidence.sawNearestSampling) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "UI showcase image product or render evidence verification failed");
    }
    if (options.imageLifecycleDemo) {
        constexpr u64 ExpectedImageFrames = ImageAtlasInvalidationFrame - 1;
        constexpr u64 ExpectedUnavailableFrames = ImageResolverUnbindFrame - ImageAtlasInvalidationFrame;
        constexpr u64 ExpectedResolverCalls = ImageResolverUnbindFrame - 1;
        if (!counters.imageResolverUnbound || counters.imageResolverCallsAtUnbind != ExpectedResolverCalls ||
            counters.imageResolverCalls != ExpectedResolverCalls || counters.imageResolverHits != ExpectedImageFrames ||
            counters.imageResolverUnavailable != ExpectedUnavailableFrames ||
            renderEvidence.submittedFrames != counters.frameUpdates ||
            renderEvidence.submittedImageFrames != ExpectedImageFrames ||
            renderEvidence.submittedImageFreeFrames != counters.frameUpdates - ExpectedImageFrames) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "UI showcase image lifecycle verification failed");
        }
    } else if (counters.imageResolverHits != counters.imageResolverCalls ||
               counters.imageResolverUnavailable != 0 || counters.imageResolverUnbound ||
               (options.targetFrameCount != 0 && renderEvidence.submittedFrames != counters.frameUpdates) ||
               (options.targetFrameCount != 0 && renderEvidence.submittedImageFrames != counters.frameUpdates) ||
               renderEvidence.submittedImageFreeFrames != 0) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "UI showcase steady image verification failed");
    }
    if (options.targetFrameCount != 0) {
        if (reason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
            counters.frameUpdates != options.targetFrameCount) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "UI showcase frame-bounded run did not reach its requested exit");
        }
    } else if (reason != Tina::RunExitReason::PrimaryWindowRequestedClose) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Interactive UI showcase stopped without a window close request");
    }
    if (options.autoDemo) {
        constexpr Tina::UI::UIListViewItemKey ExpectedListSelectionKey = 1'007;
        constexpr Tina::UI::UITreeViewItemKey ExpectedTreeSelectionKey = 4;
        // Density rebuilds replace the sole root while preserving the application-owned state.
        // motionBegins: 2 theme switches x 6 sections/inspector panels.
        if (counters.finalUI.themeSwitches != 2 || counters.finalUI.sliderChanges == 0 ||
            std::lround(counters.finalUI.progressValue) != 84 || counters.finalUI.theme != options.initialTheme ||
            counters.finalUI.density != options.initialDensity ||
            counters.densityRebuilds != 2 || counters.finalUI.densitySwitchRequests != 2 ||
            counters.finalUI.treeExpansionChanges != 2 ||
            counters.finalUI.listSelectionKey != ExpectedListSelectionKey ||
            counters.finalUI.treeSelectionKey != ExpectedTreeSelectionKey || counters.finalUI.dropdownSelection != 1 ||
            std::lround(counters.finalUI.scrollOffset) != 80 || counters.finalUI.styleTokenUpdates < 3 ||
            std::lround(counters.finalUI.componentScrollOffset) != 240 ||
            counters.finalUI.motionBegins < 12 || counters.finalUI.dialogOpen) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "UI workbench automated theme, density rebuild, component, dialog, collection, or scrolling exercise did not complete");
        }
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

    Tina::SampleUI::ShowcaseRenderDeviceAccess deviceAccess{};
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&deviceAccess](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
            return Tina::SampleUI::wrapShowcaseRenderDevice(std::move(device), deviceAccess);
        };
    auto host = Tina::Desktop::CreateEngine(createEngineConfig(*options), std::move(desktopOptions));
    if (!host) {
        writeError(host.error());
        return 1;
    }

    LifecycleCounters counters{};
    ShowcaseApplication application{*options, counters, deviceAccess};
    auto result = (*host)->run(application);
    if (!result) {
        writeError(result.error());
        return 1;
    }
    const Tina::SampleUI::ShowcaseRenderEvidence& renderEvidence = deviceAccess.evidence();
    if (Tina::Core::Status status = verifyRun(*result, *options, counters, renderEvidence); !status) {
        writeError(status.error());
        return 1;
    }

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_sample_ui_showcase");
        writer.member("frames", counters.frameUpdates);
        writer.member("targetFrames", options->targetFrameCount);
        writer.member("autoDemo", options->autoDemo);
        writer.member("initialTheme", themeName(options->initialTheme));
        writer.member("finalTheme", themeName(counters.finalUI.theme));
        writer.member("initialDensity", densityName(options->initialDensity));
        writer.member("finalDensity", densityName(counters.finalUI.density));
        writer.member("themeSwitches", counters.finalUI.themeSwitches);
        writer.member("densitySwitchRequests", counters.finalUI.densitySwitchRequests);
        writer.member("densityRebuilds", counters.densityRebuilds);
        writer.member("sliderChanges", counters.finalUI.sliderChanges);
        writer.member("treeExpansionChanges", counters.finalUI.treeExpansionChanges);
        writer.member("progressValue", counters.finalUI.progressValue);
        writer.member("buttonActivations", counters.finalUI.buttonActivations);
        writer.member("listSelectionKey", counters.finalUI.listSelectionKey);
        writer.member("treeSelectionKey", counters.finalUI.treeSelectionKey);
        writer.member("dropdownSelection", counters.finalUI.dropdownSelection);
        writer.member("scrollOffset", counters.finalUI.scrollOffset);
        writer.member("componentScrollOffset", counters.finalUI.componentScrollOffset);
        writer.member("controls", counters.finalUI.controlCount);
        writer.member("imageProducts", counters.finalUI.imageProductCount);
        writer.member("asymmetricCornerProducts", counters.finalUI.asymmetricCornerProductCount);
        writer.member("componentProfiles", counters.finalUI.componentProfileCount);
        writer.member("workbenchBands", counters.finalUI.workbenchBandCount);
        writer.member("desktopWorkbench", counters.finalUI.desktopWorkbench);
        writer.member("dialogOpen", counters.finalUI.dialogOpen);
        writer.member("stylesheetInstalled", counters.finalUI.stylesheetInstalled);
        writer.member("styleTokenUpdates", counters.finalUI.styleTokenUpdates);
        writer.member("motionBegins", counters.finalUI.motionBegins);
        writer.member("imageAtlasUploaded", counters.imageAtlasUploaded);
        writer.member("imageAtlasReleased", counters.imageAtlasReleased);
        writer.member("imageAtlasInvalidated", counters.imageAtlasInvalidatedAfterRelease);
        writer.member("imageFrameBorrowsAtRelease", counters.imageFrameBorrowsAtRelease);
        writer.member("imageLifecycleDemo", options->imageLifecycleDemo);
        writer.member("imageResolverCalls", counters.imageResolverCalls);
        writer.member("imageResolverHits", counters.imageResolverHits);
        writer.member("imageResolverUnavailable", counters.imageResolverUnavailable);
        writer.member("imageResolverUnbound", counters.imageResolverUnbound);
        writer.member("imageFrames", renderEvidence.submittedImageFrames);
        writer.member("imageFreeFrames", renderEvidence.submittedImageFreeFrames);
        writer.member("maxImageQuads", renderEvidence.maxImageQuadCommands);
        writer.member("maxImageBatches", renderEvidence.maxImageBatches);
        writer.member("maxUniqueImageResources", renderEvidence.maxUniqueImageResources);
        writer.member("imageLinear", renderEvidence.sawLinearSampling);
        writer.member("imageNearest", renderEvidence.sawNearestSampling);
        writer.member("paintOrderChecksum", renderEvidence.lastPaintOrderChecksum);
        writer.member("logicalPixelWidth", counters.logicalPixelWidth);
        writer.member("logicalPixelHeight", counters.logicalPixelHeight);
        writer.member("framebufferPixelWidth", counters.framebufferPixelWidth);
        writer.member("framebufferPixelHeight", counters.framebufferPixelHeight);
        writer.member("contentScaleX", counters.contentScaleX);
        writer.member("contentScaleY", counters.contentScaleY);
        writer.member("windowMetricsEvents", counters.windowMetricsEvents);
        writer.member("quality", qualityName(counters.finalUI.quality));
        writer.member("notificationsEnabled", counters.finalUI.notificationsEnabled);
        writer.member("multilineNotesScrolled", counters.finalUI.multilineNotesScrolled);
        writer.member("stateEnters", counters.stateEnters);
        writer.member("stateExits", counters.stateExits);
        writer.member("uiRootsCreated", counters.uiRootsCreated);
        writer.member("uiRootsReleased", counters.uiRootsReleased);
        writer.member("exit", exitReasonName(*result));
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&) {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::OutOfMemory, "The UI showcase ran out of memory"});
        return 1;
    } catch (const std::exception& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal, "An exception crossed the UI showcase boundary"};
        error.addContext("tina_sample_ui_showcase", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...) {
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::Internal,
                                     "A non-standard exception crossed the UI showcase boundary"});
        return 1;
    }
}
