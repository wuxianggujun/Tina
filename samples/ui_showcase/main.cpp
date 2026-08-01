#include "ShowcaseUI.hpp"
#include "ShowcaseImageFixture.hpp"
#include "ShowcaseRenderDevice.hpp"

#include "SampleSpriteFrameResource.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <array>
#include <charconv>
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

namespace {

using Tina::Core::u32;
using Tina::Core::u64;

struct SampleOptions final {
    u64 targetFrameCount = 0;
    u32 frameDelayMilliseconds = 0;
    Tina::SampleUI::ShowcaseTheme initialTheme = Tina::SampleUI::ShowcaseTheme::Dark;
    bool autoDemo = false;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 uiRootsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 imageResolverCalls = 0;
    u64 imageResolverHits = 0;
    u64 imageResolverUnavailable = 0;
    u32 imageFrameBorrowsAtRelease = 0;
    bool imageAtlasUploaded = false;
    bool imageAtlasReleased = false;
    Tina::SampleUI::ShowcaseUISnapshot finalUI{};
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
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_ui_showcase\",\"domain\":"
              << static_cast<std::uint16_t>(error.code.domain) << ",\"code\":" << error.code.value << ",\"message\":";
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

    SampleOptions options{};
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasTheme = false;
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(FramesPrefix)) {
            if (hasFrames) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "Duplicate --frames argument");
            }
            const std::string_view value = argument.substr(FramesPrefix.size());
            if (!parseUnsigned(value, options.targetFrameCount) || options.targetFrameCount == 0) {
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
            if (!parseUnsigned(value, options.frameDelayMilliseconds)) {
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
        if (argument == "--auto-demo") {
            if (options.autoDemo) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --auto-demo argument");
            }
            options.autoDemo = true;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Unsupported UI showcase command-line argument");
    }

    if (options.autoDemo && options.targetFrameCount != 0 && options.targetFrameCount < 120) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--auto-demo requires at least 120 frames when --frames is supplied");
    }
    return options;
}

[[nodiscard]] std::string_view themeName(Tina::SampleUI::ShowcaseTheme theme) noexcept
{
    return theme == Tina::SampleUI::ShowcaseTheme::Dark ? "dark" : "light";
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
        auto uploaded = device->createTexture2DRgba8(Tina::Render::Texture2DUploadDesc{
            .width = static_cast<Tina::Core::u16>(Tina::SampleUI::ShowcaseAtlasWidth),
            .height = static_cast<Tina::Core::u16>(Tina::SampleUI::ShowcaseAtlasHeight),
            .rgba8Pixels = AtlasPixels,
        });
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
            counters_->imageAtlasReleased = static_cast<bool>(device->destroyTexture2D(texture_));
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

class ShowcaseState final : public Tina::IGameState {
  public:
    ShowcaseState(SampleOptions options, LifecycleCounters& counters,
                  Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess) noexcept
        : options_(options), counters_(counters), imageResources_(deviceAccess, counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_.stateEnters;
        if (Tina::Core::Status status = imageResources_.initialize(); !status) {
            return status;
        }
        if (Tina::Core::Status status =
                ui_.build(context, options_.initialTheme, imageResources_.resolver());
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
    LifecycleCounters& counters_;
    ShowcaseImageResources imageResources_;
    Tina::SampleUI::ShowcaseUI ui_{};
};

class ShowcaseApplication final : public Tina::IGameApplication {
  public:
    ShowcaseApplication(SampleOptions options, LifecycleCounters& counters,
                        Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess) noexcept
        : options_(options), counters_(counters), deviceAccess_(deviceAccess)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        std::unique_ptr<Tina::IGameState> state =
            std::make_unique<ShowcaseState>(options_, counters_, deviceAccess_);
        return state;
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_.applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters& counters_;
    Tina::SampleUI::ShowcaseRenderDeviceAccess& deviceAccess_;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina UI Showcase";
    config.primaryWindow.title = "Tina UI Showcase - Complete Retained Controls";
    config.primaryWindow.initialLogicalExtent = {1280, 980};
    config.primaryWindow.resizable = false;
    config.primaryWindow.initiallyVisible = true;

    config.primaryWindowUICapacities = Tina::UI::UIContextCapacityConfig{
        .nodeCapacity = 2048,
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
    if (counters.stateEnters != 1 || counters.stateExits != 1 || counters.applicationShutdowns != 1 ||
        counters.uiRootsCreated != 1 || counters.uiRootsReleased != 1 || counters.finalUI.rootAlive ||
        counters.finalUI.controlCount != 20 || counters.finalUI.imageProductCount != 4) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "UI showcase lifecycle or control inventory verification failed");
    }
    if (!counters.imageAtlasUploaded || !counters.imageAtlasReleased ||
        counters.imageFrameBorrowsAtRelease != 0 || counters.imageResolverCalls == 0 ||
        counters.imageResolverHits != counters.imageResolverCalls || counters.imageResolverUnavailable != 0 ||
        renderEvidence.submittedImageFrames == 0 || renderEvidence.maxImageQuadCommands < 12 ||
        renderEvidence.maxImageBatches < 3 || renderEvidence.maxUniqueImageResources != 1 ||
        renderEvidence.lastPaintOrderChecksum == 0 || !renderEvidence.sawLinearSampling ||
        !renderEvidence.sawNearestSampling) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "UI showcase image product or render evidence verification failed");
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
        if (counters.finalUI.themeSwitches != 2 || counters.finalUI.sliderChanges == 0 ||
            std::lround(counters.finalUI.progressValue) != 84 || counters.finalUI.theme != options.initialTheme ||
            counters.finalUI.treeExpansionChanges != 2 ||
            counters.finalUI.listSelectionKey != ExpectedListSelectionKey ||
            counters.finalUI.treeSelectionKey != ExpectedTreeSelectionKey || counters.finalUI.dropdownSelection != 1 ||
            std::lround(counters.finalUI.scrollOffset) != 80) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "UI showcase automated theme, value, popup, collection, or scrolling exercise did not complete");
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
    auto host = Tina::Desktop::CreateEngine(createEngineConfig(), std::move(desktopOptions));
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

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_ui_showcase\",\"frames\":" << counters.frameUpdates
              << ",\"targetFrames\":" << options->targetFrameCount
              << ",\"autoDemo\":" << (options->autoDemo ? "true" : "false") << ",\"initialTheme\":";
    writeJsonString(std::cout, themeName(options->initialTheme));
    std::cout << ",\"finalTheme\":";
    writeJsonString(std::cout, themeName(counters.finalUI.theme));
    std::cout << ",\"themeSwitches\":" << counters.finalUI.themeSwitches
              << ",\"sliderChanges\":" << counters.finalUI.sliderChanges
              << ",\"treeExpansionChanges\":" << counters.finalUI.treeExpansionChanges
              << ",\"progressValue\":" << counters.finalUI.progressValue
              << ",\"buttonActivations\":" << counters.finalUI.buttonActivations
              << ",\"listSelectionKey\":" << counters.finalUI.listSelectionKey
              << ",\"treeSelectionKey\":" << counters.finalUI.treeSelectionKey
              << ",\"dropdownSelection\":" << counters.finalUI.dropdownSelection
              << ",\"scrollOffset\":" << counters.finalUI.scrollOffset
              << ",\"controls\":" << counters.finalUI.controlCount
              << ",\"imageProducts\":" << counters.finalUI.imageProductCount
              << ",\"imageAtlasUploaded\":" << (counters.imageAtlasUploaded ? "true" : "false")
              << ",\"imageAtlasReleased\":" << (counters.imageAtlasReleased ? "true" : "false")
              << ",\"imageResolverCalls\":" << counters.imageResolverCalls
              << ",\"imageResolverHits\":" << counters.imageResolverHits
              << ",\"imageResolverUnavailable\":" << counters.imageResolverUnavailable
              << ",\"imageFrames\":" << renderEvidence.submittedImageFrames
              << ",\"maxImageQuads\":" << renderEvidence.maxImageQuadCommands
              << ",\"maxImageBatches\":" << renderEvidence.maxImageBatches
              << ",\"maxUniqueImageResources\":" << renderEvidence.maxUniqueImageResources
              << ",\"imageLinear\":" << (renderEvidence.sawLinearSampling ? "true" : "false")
              << ",\"imageNearest\":" << (renderEvidence.sawNearestSampling ? "true" : "false")
              << ",\"paintOrderChecksum\":" << renderEvidence.lastPaintOrderChecksum
              << ",\"quality\":";
    writeJsonString(std::cout, qualityName(counters.finalUI.quality));
    std::cout << ",\"notificationsEnabled\":" << (counters.finalUI.notificationsEnabled ? "true" : "false")
              << ",\"uiRootsCreated\":" << counters.uiRootsCreated
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
