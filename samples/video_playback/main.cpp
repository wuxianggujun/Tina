// Decodes a real MP4 on the GPU and proves the decoded picture reached the screen.
//
// The programs next to this one each stop one step short: tina_sample_video_demux
// parses a clip but never touches a GPU, and tina_sample_video_probe creates a device
// but decodes nothing. This one closes the gap, which is the only way to find out
// whether the decode SPI works rather than merely returns success.
//
// The evidence is two captures of the same scene. Both draw one sprite sampling the
// same decode texture with identical geometry; the only difference is that no access
// unit has been submitted before the first and many have before the second. A decode
// target starts cleared, so if submission does nothing the two captures are identical.
// That makes the criterion falsifiable without inventing a threshold for what a
// "real" picture looks like.
#include "Mp4Demux.hpp"
#include "PlaybackDeviceProbe.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderFrameCapture.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PhaseContexts.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Tina::Core::i64;
using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

// The first capture on this host is always blank regardless of what was drawn, so the
// baseline is taken well after startup rather than on frame 0.
// Far enough past the first frame to clear the blank-first-capture behaviour, and
// before the baseline so the control texture is unbound again by then.
inline constexpr u64 ControlCaptureFrame = 4;
inline constexpr u64 BaselineCaptureFrame = 8;
// One access unit per frame keeps the submission clock trivially monotonic. Forty is
// more than enough to move past the first IDR on any real clip.
inline constexpr u64 AccessUnitsToSubmit = 40;
// Submission runs on the frames after the baseline, so the last one lands here. The
// decoded capture is requested two frames later to leave the decoder a frame of slack.
// Presentation ticks only start once submission stops, and the first displayed picture
// needs several of them, so the decoded capture leaves a margin of ticks rather than the
// one frame a decode would need.
inline constexpr u64 PresentationTicksBeforeCapture = 12;
inline constexpr u64 DecodedCaptureFrame =
    BaselineCaptureFrame + AccessUnitsToSubmit + PresentationTicksBeforeCapture;
// One frame past the decoded capture so frame DecodedCaptureFrame + 1 can collect it.
inline constexpr u64 TotalFrames = DecodedCaptureFrame + 3;

// A 30fps clip advances 33333us per frame. The exact rate does not matter here: the
// clock only has to move forward, which is what the decode SPI requires.
inline constexpr i64 PresentationStepUs = 33'333;

// How far the presentation clock trails the newest submitted access unit. Reordered
// streams decode pictures ahead of display, so a clock level with the newest decode has
// nothing to select; two frames of lag leaves reordering somewhere to reach back to.
inline constexpr i64 ClockLagUs = PresentationStepUs * 2;

// A solid-magenta 2x2 RGBA8 texture. The proof draws one frame with this bound in
// place of the decode target, which separates "the sprite never draws" from "the
// sprite draws but the decode texture has no picture in it": both look like a frame
// of pure clear colour, and no capture of the decode path alone can tell them apart.
inline constexpr u8 ControlRed = 255;
inline constexpr u8 ControlGreen = 0;
inline constexpr u8 ControlBlue = 255;

struct CaptureSummary final {
    u32 width = 0;
    u32 height = 0;
    u64 distinctColors = 0;
    // A one-colour frame is ambiguous on its own: the sprite fills the camera, so a
    // sprite sampling a cleared decode texture and a frame with no sprite at all both
    // report one colour. The actual value separates them.
    u32 centerPixel = 0;
    u32 cornerPixel = 0;
    bool captured = false;
};

// The engine destroys the game state during shutdown, so the state cannot be read
// after run() returns. It writes its evidence here instead, into storage main owns.
struct ProofResults final {
    // The same sprite drawn with an ordinary uploaded texture. If this frame is the
    // clear colour too, nothing about the decode path has been measured yet.
    CaptureSummary control;
    CaptureSummary baseline;
    CaptureSummary decoded;
    u64 accessUnitsSubmitted = 0;
    // Zero here means nothing was ever displayed no matter how many access units decoded.
    u64 presentationTicks = 0;
    // Distinguishes "the sprite was never offered to the renderer" from "it was offered
    // and dropped somewhere downstream", which a pixel comparison alone cannot tell.
    u64 spritesExtracted = 0;
    // Read off the frame the device was actually handed, which is downstream of the
    // scene writer accepting the sprite.
    u64 maxSpritesSubmittedToDevice = 0;
    u64 framesWithCamera2D = 0;
    u64 sprite2DDrawsSubmitted = 0;
    u64 framesPresented = 0;
    // A failed capture leaves the previous summary untouched, which looks exactly like a
    // frame that drew nothing. Without this the report cannot tell those apart.
    u64 captureFailures = 0;
    std::string firstCaptureFailure;
    bool textureCreated = false;
};

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "."
        + std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_video_playback");
    writer.member("code", errorCodeName(error.code));
    writer.member("message", error.message);
    writer.beginArrayMember("context");
    for (const Tina::Core::ErrorContext& context : error.context)
    {
        writer.beginObjectElement();
        writer.member("operation", context.operation);
        writer.member("detail", context.detail);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    std::cerr << '\n';
}

// Counts distinct RGBA quads. A cleared decode target makes the sprite one flat
// colour, so the count separates "a picture arrived" from "the texture is still
// whatever it was created as" without asserting anything about the picture's content.
[[nodiscard]] CaptureSummary summarize(const Tina::Render::Rgba8FrameCapture& capture)
{
    CaptureSummary summary;
    if (capture.empty())
    {
        return summary;
    }
    summary.captured = true;
    summary.width = capture.width;
    summary.height = capture.height;

    const auto packAt = [&capture](std::size_t index) -> u32 {
        const std::byte* const pixel = capture.rgba8Pixels.data() + (index * 4);
        return static_cast<u32>(std::to_integer<unsigned>(pixel[0])) << 24
            | static_cast<u32>(std::to_integer<unsigned>(pixel[1])) << 16
            | static_cast<u32>(std::to_integer<unsigned>(pixel[2])) << 8
            | static_cast<u32>(std::to_integer<unsigned>(pixel[3]));
    };

    std::set<u32> colors;
    const std::size_t pixelCount = capture.rgba8Pixels.size() / 4;
    for (std::size_t index = 0; index < pixelCount; ++index)
    {
        colors.insert(packAt(index));
    }
    summary.distinctColors = static_cast<u64>(colors.size());

    summary.cornerPixel = packAt(0);
    const std::size_t centerIndex =
        (static_cast<std::size_t>(capture.height) / 2) * static_cast<std::size_t>(capture.width)
        + (static_cast<std::size_t>(capture.width) / 2);
    if (centerIndex < pixelCount)
    {
        summary.centerPixel = packAt(centerIndex);
    }
    return summary;
}

class PlaybackState final : public Tina::IGameState {
  public:
    PlaybackState(const Tina::Sample::DemuxedVideo& video, Tina::Sample::PlaybackDeviceProbe& probe,
                  ProofResults& results) noexcept
        : video_(&video), probe_(&probe), results_(&results)
    {
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        const u64 frameIndex = frames_;
        ++frames_;

        if (probe_->get() == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "The render device wrap never ran, so nothing can be decoded");
        }
        if (!texture_)
        {
            if (auto status = createDecodeTarget(); !status)
            {
                return status;
            }
            if (auto status = createControlTexture(); !status)
            {
                return status;
            }
        }

        // A capture requested on frame N is taken inside that frame's present, so it is
        // only readable from frame N+1. Collecting first keeps the captures from
        // overwriting each other in the probe's single slot.
        if (frameIndex == ControlCaptureFrame + 1)
        {
            collectCapture(results_->control);
            useControlTexture_ = false;
        }
        else if (frameIndex == BaselineCaptureFrame + 1)
        {
            collectCapture(results_->baseline);
        }
        else if (frameIndex == DecodedCaptureFrame + 1)
        {
            collectCapture(results_->decoded);
        }

        if (frameIndex == ControlCaptureFrame)
        {
            useControlTexture_ = true;
            probe_->requestCaptureNextPresent();
        }
        else if (frameIndex == BaselineCaptureFrame || frameIndex == DecodedCaptureFrame)
        {
            probe_->requestCaptureNextPresent();
        }

        // Starts after the baseline frame has been drawn, so the baseline provably
        // reflects a frame rendered while no access unit had been enqueued.
        if (frameIndex > BaselineCaptureFrame && submitted_ < AccessUnitsToSubmit
            && submitted_ < static_cast<u64>(video_->accessUnits.size()))
        {
            if (auto status = submitNextAccessUnit(); !status)
            {
                return status;
            }
        }
        else if (submitted_ != 0)
        {
            // A submission carrying access units only decodes them. Selecting which
            // decoded picture to show, and converting it into the destination texture,
            // happens on a submission that carries none. Feeding access units without
            // ever ticking the clock decodes the whole clip and displays nothing, with
            // every call reporting success.
            if (auto status = submitPresentationTick(); !status)
            {
                return status;
            }
        }

        if (frames_ >= TotalFrames)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        const u32 activeKey = useControlTexture_ ? controlBindingKey_ : bindingKey_;
        if (activeKey == 0)
        {
            return Tina::Core::success();
        }

        Tina::Render::FrameResourceDescriptor descriptor;
        descriptor.kind = Tina::Render::FrameResourceKind::Texture2D;
        descriptor.deviceBindingKey = activeKey;

        // The sample owns both textures for its whole run and retires them in onExit,
        // so the packet needs no release callback -- only a pin the sink accepts.
        // FramePin runs nothing when its release function is null.
        Tina::Render::FramePin pin{Tina::Render::FramePinKind::Custom, activeKey, nullptr, nullptr};
        auto ref = context.frameResourceSink().intern(descriptor, std::move(pin));
        if (!ref)
        {
            return Tina::Core::failure(std::move(ref.error()));
        }

        Tina::Render::RenderCamera2DInput camera;
        camera.stableCameraKey = 1;
        camera.worldWidth = 16.0F;
        camera.worldHeight = 9.0F;
        camera.actualPixelsPerMeter = 40.0F;
        if (auto status = context.renderSceneWriter().setCamera2D(camera); !status)
        {
            return status;
        }

        // Fills the camera so the capture is dominated by decoded pixels rather than
        // by the clear colour around a small quad.
        Tina::Render::RenderSprite2DInput sprite;
        sprite.texture = *ref;
        sprite.stableEntityKey = 1;
        sprite.widthMeters = 16.0F;
        sprite.heightMeters = 9.0F;
        if (auto status = context.renderSceneWriter().addSprite2D(sprite); !status)
        {
            return status;
        }
        ++results_->spritesExtracted;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        Tina::Render::IRenderDevice* const device = probe_->get();
        if (device == nullptr)
        {
            return;
        }
        retire(*device, texture_, bindingKey_);
        retire(*device, controlTexture_, controlBindingKey_);
    }

  private:
    static void retire(Tina::Render::IRenderDevice& device, Tina::Render::GpuTextureId& texture,
                       u32& bindingKey) noexcept
    {
        if (!texture)
        {
            return;
        }
        if (bindingKey != 0)
        {
            (void)device.setTexture2DBinding(bindingKey, Tina::Render::GpuTextureId{});
            bindingKey = 0;
        }
        Tina::Render::FramePin pin{Tina::Render::FramePinKind::Custom, 0, nullptr, nullptr};
        (void)device.retireTexture2D(texture, pin);
        texture = Tina::Render::GpuTextureId{};
    }

    [[nodiscard]] Tina::Core::Status createDecodeTarget()
    {
        Tina::Render::VideoDecodeTextureDesc desc;
        desc.codec = video_->codec;
        desc.chroma = video_->chroma;
        desc.bitDepth = video_->bitDepth;
        desc.codedWidth = video_->width;
        desc.codedHeight = video_->height;
        desc.maxDpbSlots = video_->maxDpbSlots;
        desc.maxActiveReferences = video_->maxActiveReferences;
        desc.parameterSets = video_->parameterSets;

        auto textureResult = probe_->get()->createVideoDecodeTexture(desc);
        if (!textureResult)
        {
            return Tina::Core::failure(std::move(textureResult.error()));
        }
        texture_ = *textureResult;
        results_->textureCreated = true;

        auto keyResult = probe_->get()->createTexture2DBinding(texture_);
        if (!keyResult)
        {
            Tina::Render::FramePin pin{Tina::Render::FramePinKind::Custom, 0, nullptr, nullptr};
            (void)probe_->get()->retireTexture2D(texture_, pin);
            texture_ = Tina::Render::GpuTextureId{};
            results_->textureCreated = false;
            return Tina::Core::failure(std::move(keyResult.error()));
        }
        bindingKey_ = *keyResult;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status createControlTexture()
    {
        constexpr u16 Extent = 2;
        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(Extent) * Extent; ++pixel)
        {
            controlPixels_[(pixel * 4) + 0] = static_cast<std::byte>(ControlRed);
            controlPixels_[(pixel * 4) + 1] = static_cast<std::byte>(ControlGreen);
            controlPixels_[(pixel * 4) + 2] = static_cast<std::byte>(ControlBlue);
            controlPixels_[(pixel * 4) + 3] = static_cast<std::byte>(255);
        }

        const Tina::Render::Texture2DUploadLevel level{
            .width = Extent,
            .height = Extent,
            .bytes = std::span<const std::byte>{controlPixels_},
        };
        controlLevels_[0] = level;

        Tina::Render::Texture2DUploadDesc desc;
        desc.format = Tina::Render::GpuTextureFormat::Rgba8Unorm;
        desc.levels = std::span<const Tina::Render::Texture2DUploadLevel>{controlLevels_};

        auto textureResult = probe_->get()->createTexture2D(desc);
        if (!textureResult)
        {
            return Tina::Core::failure(std::move(textureResult.error()));
        }
        controlTexture_ = *textureResult;

        auto keyResult = probe_->get()->createTexture2DBinding(controlTexture_);
        if (!keyResult)
        {
            Tina::Render::FramePin pin{Tina::Render::FramePinKind::Custom, 0, nullptr, nullptr};
            (void)probe_->get()->retireTexture2D(controlTexture_, pin);
            controlTexture_ = Tina::Render::GpuTextureId{};
            return Tina::Core::failure(std::move(keyResult.error()));
        }
        controlBindingKey_ = *keyResult;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status submitNextAccessUnit()
    {
        const Tina::Sample::DemuxedAccessUnit& unit =
            video_->accessUnits[static_cast<std::size_t>(submitted_)];

        Tina::Render::VideoDecodeAccessUnit entry;
        entry.byteSize = static_cast<u32>(unit.bytes.size());
        entry.presentationTimeUs = unit.presentationTimeUs;
        accessUnitTable_[0] = entry;

        Tina::Render::VideoDecodeSubmission submission;
        submission.bitstream = unit.bytes;
        submission.accessUnits = std::span<const Tina::Render::VideoDecodeAccessUnit>{accessUnitTable_};
        // The clock has to sit inside the clip's own timeline, which does not start at
        // zero, and has to trail the newest submitted picture so reordering has decoded
        // pictures to choose from. A clock ahead of every decode selects nothing and
        // displays nothing, while every decode call still reports success.
        submission.presentationTimeUs = clockUs_;
        submission.isFinalAccessUnit =
            submitted_ + 1 == static_cast<u64>(video_->accessUnits.size());

        // The device copies both the table and the bitstream into its own allocation
        // before returning, so neither has to outlive this call.
        if (auto status = probe_->get()->submitVideoDecodeFrame(texture_, submission); !status)
        {
            return status;
        }
        ++submitted_;
        results_->accessUnitsSubmitted = submitted_;
        // Derived from the clip rather than counted up from zero, so the clock is inside
        // the timeline from the first submission instead of arriving several frames late.
        clockUs_ = unit.presentationTimeUs - ClockLagUs;
        return Tina::Core::success();
    }

    // Advances the presentation clock without decoding anything. This is the submission
    // that actually puts a picture into the destination texture.
    [[nodiscard]] Tina::Core::Status submitPresentationTick()
    {
        Tina::Render::VideoDecodeSubmission submission;
        submission.presentationTimeUs = clockUs_;
        if (auto status = probe_->get()->submitVideoDecodeFrame(texture_, submission); !status)
        {
            return status;
        }
        ++presentationTicks_;
        results_->presentationTicks = presentationTicks_;
        clockUs_ += PresentationStepUs;
        return Tina::Core::success();
    }

    // Reads what the probe took during the previous frame's present. A missing capture
    // leaves the summary un-captured rather than failing: the report distinguishes the
    // two, and a lost capture is not a decode defect.
    void collectCapture(CaptureSummary& into) noexcept
    {
        if (const Tina::Render::Rgba8FrameCapture* captured = probe_->lastCapture();
            captured != nullptr)
        {
            into = summarize(*captured);
        }
        probe_->clearLastCapture();
    }

    const Tina::Sample::DemuxedVideo* video_ = nullptr;
    Tina::Sample::PlaybackDeviceProbe* probe_ = nullptr;
    ProofResults* results_ = nullptr;
    Tina::Render::GpuTextureId texture_{};
    Tina::Render::GpuTextureId controlTexture_{};
    std::array<std::byte, 16> controlPixels_{};
    std::array<Tina::Render::Texture2DUploadLevel, 1> controlLevels_{};
    u32 controlBindingKey_ = 0;
    bool useControlTexture_ = false;
    u32 bindingKey_ = 0;
    u64 frames_ = 0;
    u64 submitted_ = 0;
    i64 clockUs_ = 0;
    u64 presentationTicks_ = 0;
    std::array<Tina::Render::VideoDecodeAccessUnit, 1> accessUnitTable_{};
};

class PlaybackApplication final : public Tina::IGameApplication {
  public:
    PlaybackApplication(const Tina::Sample::DemuxedVideo& video,
                        Tina::Sample::PlaybackDeviceProbe& probe, ProofResults& results) noexcept
        : video_(&video), probe_(&probe), results_(&results)
    {
    }

    [[nodiscard]] Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        std::unique_ptr<Tina::IGameState> state =
            std::make_unique<PlaybackState>(*video_, *probe_, *results_);
        return state;
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override {}

  private:
    const Tina::Sample::DemuxedVideo* video_ = nullptr;
    Tina::Sample::PlaybackDeviceProbe* probe_ = nullptr;
    ProofResults* results_ = nullptr;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(Tina::Render::RendererApi api)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Video Playback Proof";
    config.primaryWindow.title = "Tina Video Playback Proof";
    config.primaryWindow.initialLogicalExtent = {640, 360};
    config.rendererApi = api;
    return config;
}

[[nodiscard]] bool writeResult(const ProofResults& results)
{
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("sample", "tina_sample_video_playback");
    writer.member("textureCreated", results.textureCreated);
    writer.member("accessUnitsSubmitted", results.accessUnitsSubmitted);
    writer.member("presentationTicks", results.presentationTicks);
    writer.member("spritesExtracted", results.spritesExtracted);
    writer.member("maxSpritesSubmittedToDevice", results.maxSpritesSubmittedToDevice);
    writer.member("framesWithCamera2D", results.framesWithCamera2D);
    writer.member("sprite2DDrawsSubmitted", results.sprite2DDrawsSubmitted);
    writer.member("framesPresented", results.framesPresented);
    writer.member("captureFailures", results.captureFailures);
    writer.member("firstCaptureFailure", results.firstCaptureFailure);

    const auto writeCapture = [&writer](std::string_view name, const CaptureSummary& summary) {
        writer.beginObjectMember(name);
        writer.member("captured", summary.captured);
        writer.member("width", summary.width);
        writer.member("height", summary.height);
        writer.member("distinctColors", summary.distinctColors);
        writer.member("centerPixel", summary.centerPixel);
        writer.member("cornerPixel", summary.cornerPixel);
        writer.endObject();
    };
    writeCapture("control", results.control);
    writeCapture("baseline", results.baseline);
    writeCapture("decoded", results.decoded);

    const bool success = results.baseline.captured && results.decoded.captured
        && results.decoded.distinctColors > results.baseline.distinctColors;
    writer.member("success", success);
    writer.endObject();
    std::cout << '\n';
    return success;
}

[[nodiscard]] int runPlayback(const Tina::Sample::DemuxedVideo& video, Tina::Render::RendererApi api)
{
    Tina::Sample::PlaybackDeviceProbe probe;
    ProofResults results;

    Tina::Desktop::CreateEngineOptions options;
    options.wrapWindowSurfaceRenderDevice =
        [&probe](std::unique_ptr<Tina::Render::IRenderDevice> device)
        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
        if (!device)
        {
            return device;
        }
        return Tina::Sample::wrapProbingRenderDevice(std::move(device), probe);
    };

    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig(api), std::move(options));
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    PlaybackApplication application{video, probe, results};
    auto runResult = (*hostResult)->run(application);
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    results.maxSpritesSubmittedToDevice = probe.maxSubmittedSpriteCount();
    results.framesWithCamera2D = probe.framesWithCamera2D();
    results.sprite2DDrawsSubmitted = probe.statistics().sprite2DDrawsSubmitted;
    results.framesPresented = probe.statistics().presented;
    results.captureFailures = probe.captureFailures();
    results.firstCaptureFailure = probe.firstCaptureFailure();

    // A failing comparison is a real negative result, not a crash, so it still writes
    // the full report and only the exit code distinguishes it.
    return writeResult(results) ? 0 : 1;
}

// Same flag form as tina_sample_video_demux so a clip can be moved between the two
// without editing the command line.
[[nodiscard]] Tina::Core::Result<std::string> parseClipPath(int argumentCount, char** arguments)
{
    constexpr std::string_view ClipPrefix = "--clip=";
    std::string clipPath;

    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (!argument.starts_with(ClipPrefix))
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "The video playback proof accepts only --clip="};
            error.addContext("parseClipPath", argument);
            return Tina::Core::failure(std::move(error));
        }
        if (!clipPath.empty())
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "Duplicate --clip argument"};
            error.addContext("parseClipPath", argument);
            return Tina::Core::failure(std::move(error));
        }
        clipPath = std::string{argument.substr(ClipPrefix.size())};
    }

    if (clipPath.empty())
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                "The video playback proof requires --clip=<path to an MP4>"};
        return Tina::Core::failure(std::move(error));
    }
    return clipPath;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try
    {
        auto clipPath = parseClipPath(argumentCount, arguments);
        if (!clipPath)
        {
            writeError(clipPath.error());
            return 2;
        }

        auto videoResult = Tina::Sample::demuxMp4Video(*clipPath);
        if (!videoResult)
        {
            writeError(videoResult.error());
            return 1;
        }

        return runPlayback(*videoResult, Tina::Render::RendererApi::Automatic);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The video playback proof ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the video playback proof boundary"};
        error.addContext("tina_sample_video_playback",
                         exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the video playback proof boundary"};
        writeError(error);
        return 1;
    }
}

