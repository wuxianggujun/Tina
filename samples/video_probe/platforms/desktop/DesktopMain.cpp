// Reports what this machine's GPU can hardware decode. Video playback in Tina is
// built on bgfx's hardware decoder with no software fallback, so this program is the
// feasibility gate: if it reports no supported codec, no amount of asset-pipeline
// work will make a clip play here.
//
// It creates the real production Desktop device, reads capabilities through the
// diagnostics device wrap, runs one frame and exits. Nothing is decoded: the probe
// needs no clip and no parameter sets.
#include <tina/core/error/Error.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

inline constexpr u64 ProbeFrameCount = 1;

// The decoder is a per-backend implementation, so "this machine supports H.264" is
// only meaningful together with the backend that answered. IRenderDevice does not
// publish the renderer bgfx resolved, so the probe names it on the way in instead.
struct RendererChoice final {
    std::string_view name;
    Tina::Render::RendererApi api;
};

inline constexpr std::array<RendererChoice, 5> RendererChoices{{
    {"automatic", Tina::Render::RendererApi::Automatic},
    {"vulkan", Tina::Render::RendererApi::Vulkan},
    {"d3d11", Tina::Render::RendererApi::Direct3D11},
    {"d3d12", Tina::Render::RendererApi::Direct3D12},
    {"opengl", Tina::Render::RendererApi::OpenGL},
}};

// One representative stream shape per row. Extents are the coded (macroblock or CTU
// aligned) dimensions a real clip of that resolution would carry, and the DPB numbers
// match what bgfx's own reference player requests, so a row that probes false here is
// a row a real clip would also be refused for.
struct StreamProbe final {
    std::string_view name;
    Tina::Render::VideoCodec codec;
    Tina::Render::VideoChromaSubsampling chroma;
    u8 bitDepth;
    u16 codedWidth;
    u16 codedHeight;
};

inline constexpr std::array<StreamProbe, 8> StreamProbes{{
    {"h264_720p_8bit_420", Tina::Render::VideoCodec::H264, Tina::Render::VideoChromaSubsampling::Yuv420, 8, 1280, 720},
    {"h264_1080p_8bit_420", Tina::Render::VideoCodec::H264, Tina::Render::VideoChromaSubsampling::Yuv420, 8, 1920,
     1088},
    {"h264_4k_8bit_420", Tina::Render::VideoCodec::H264, Tina::Render::VideoChromaSubsampling::Yuv420, 8, 3840, 2160},
    {"h264_1080p_8bit_444", Tina::Render::VideoCodec::H264, Tina::Render::VideoChromaSubsampling::Yuv444, 8, 1920,
     1088},
    {"h265_1080p_8bit_420", Tina::Render::VideoCodec::H265, Tina::Render::VideoChromaSubsampling::Yuv420, 8, 1920,
     1088},
    {"h265_4k_10bit_420", Tina::Render::VideoCodec::H265, Tina::Render::VideoChromaSubsampling::Yuv420, 10, 3840,
     2160},
    {"av1_1080p_8bit_420", Tina::Render::VideoCodec::Av1, Tina::Render::VideoChromaSubsampling::Yuv420, 8, 1920, 1088},
    {"av1_4k_10bit_420", Tina::Render::VideoCodec::Av1, Tina::Render::VideoChromaSubsampling::Yuv420, 10, 3840, 2160},
}};

// bgfx's reference player hardcodes these for every stream it plays, so they are the
// budget a Tina clip would ask for too.
inline constexpr u8 ProbeMaxDpbSlots = 16;
inline constexpr u8 ProbeMaxActiveReferences = 4;

struct ProbeReport final {
    bool deviceObserved = false;
    Tina::Render::VideoDecodeCapabilities capabilities{};
    std::array<bool, StreamProbes.size()> streamSupported{};
};

[[nodiscard]] std::string_view codecName(Tina::Render::VideoCodec codec) noexcept
{
    switch (codec)
    {
    case Tina::Render::VideoCodec::H264:
        return "h264";
    case Tina::Render::VideoCodec::H265:
        return "h265";
    case Tina::Render::VideoCodec::Av1:
        return "av1";
    }
    return "unknown";
}

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "." + std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_video_probe");
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

class ProbeState final : public Tina::IGameState {
  public:
    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override {}

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++frames_;
        if (frames_ >= ProbeFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

  private:
    u64 frames_ = 0;
};

class ProbeApplication final : public Tina::IGameApplication {
  public:
    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        std::unique_ptr<Tina::IGameState> state = std::make_unique<ProbeState>();
        return state;
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override {}
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(Tina::Render::RendererApi api)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Video Decode Probe";
    config.primaryWindow.title = "Tina Video Decode Probe";
    config.primaryWindow.initialLogicalExtent = {640, 360};
    // A window is required to create the production device, but the probe reads
    // capabilities rather than pixels, so it never needs to be shown.
    config.primaryWindow.initiallyVisible = false;
    config.rendererApi = api;
    return config;
}

void readReport(const Tina::Render::IRenderDevice& device, ProbeReport& report)
{
    report.deviceObserved = true;
    report.capabilities = device.videoDecodeCapabilities();

    for (std::size_t index = 0; index < StreamProbes.size(); ++index)
    {
        const StreamProbe& probe = StreamProbes[index];
        Tina::Render::VideoDecodeTextureDesc desc{};
        desc.codec = probe.codec;
        desc.chroma = probe.chroma;
        desc.bitDepth = probe.bitDepth;
        desc.codedWidth = probe.codedWidth;
        desc.codedHeight = probe.codedHeight;
        desc.maxDpbSlots = ProbeMaxDpbSlots;
        desc.maxActiveReferences = ProbeMaxActiveReferences;
        report.streamSupported[index] = device.isVideoDecodeSupported(desc);
    }
}

void writeCodecSupport(Tina::Core::JsonWriter& writer, const Tina::Render::VideoDecodeCapabilities::CodecSupport& support)
{
    writer.member("anySupport", support.anySupport());
    writer.member("bitDepth8", support.bitDepth8);
    writer.member("bitDepth10", support.bitDepth10);
    writer.member("bitDepth12", support.bitDepth12);
    writer.member("yuv420", support.yuv420);
    writer.member("yuv422", support.yuv422);
    writer.member("yuv444", support.yuv444);
}

void writeReport(std::string_view rendererName, const ProbeReport& report)
{
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("sample", "tina_sample_video_probe");
    writer.member("requestedRenderer", rendererName);
    writer.member("deviceObserved", report.deviceObserved);
    writer.member("videoDecodeSupported", report.capabilities.supported);
    writer.member("destinationFormatSupported", report.capabilities.destinationFormatSupported);

    writer.beginArrayMember("codecs");
    for (std::size_t index = 0; index < Tina::Render::VideoDecodeCapabilities::CodecCount; ++index)
    {
        writer.beginObjectElement();
        writer.member("codec", codecName(static_cast<Tina::Render::VideoCodec>(index)));
        writeCodecSupport(writer, report.capabilities.codecs[index]);
        writer.endObject();
    }
    writer.endArray();

    writer.beginArrayMember("streams");
    for (std::size_t index = 0; index < StreamProbes.size(); ++index)
    {
        writer.beginObjectElement();
        writer.member("stream", StreamProbes[index].name);
        writer.member("supported", report.streamSupported[index]);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    std::cout << '\n';
}

[[nodiscard]] int runProbe(const RendererChoice& renderer)
{
    ProbeReport report;

    Tina::Desktop::CreateEngineOptions options;
    options.wrapWindowSurfaceRenderDevice =
        [&report](std::unique_ptr<Tina::Render::IRenderDevice> device)
        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
        // The device is fully initialized by the time the wrap runs, so the
        // capabilities read here are the backend's real answer rather than the
        // pre-initialization default.
        if (device)
        {
            readReport(*device, report);
        }
        return device;
    };

    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig(renderer.api), std::move(options));
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    ProbeApplication application;
    auto runResult = (*hostResult)->run(application);
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    if (!report.deviceObserved)
    {
        // Silence here would look identical to "no hardware support", which is the
        // one confusion this program exists to prevent.
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "The render device wrap never ran, so no capabilities were read"};
        writeError(error);
        return 1;
    }

    writeReport(renderer.name, report);
    return 0;
}

[[nodiscard]] Tina::Core::Result<RendererChoice> parseRenderer(int argumentCount, char** arguments)
{
    constexpr std::string_view RendererPrefix = "--renderer=";
    RendererChoice choice = RendererChoices[0];
    bool hasRenderer = false;

    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (!argument.starts_with(RendererPrefix))
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "The video decode probe accepts only --renderer="};
            error.addContext("parseRenderer", argument);
            return Tina::Core::failure(std::move(error));
        }
        if (hasRenderer)
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Duplicate --renderer argument"};
            error.addContext("parseRenderer", argument);
            return Tina::Core::failure(std::move(error));
        }

        const std::string_view name = argument.substr(RendererPrefix.size());
        bool matched = false;
        for (const RendererChoice& candidate : RendererChoices)
        {
            if (candidate.name == name)
            {
                choice = candidate;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "--renderer must be automatic, vulkan, d3d11, d3d12 or opengl"};
            error.addContext("parseRenderer", name);
            return Tina::Core::failure(std::move(error));
        }
        hasRenderer = true;
    }
    return choice;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try
    {
        auto renderer = parseRenderer(argumentCount, arguments);
        if (!renderer)
        {
            writeError(renderer.error());
            return 2;
        }
        return runProbe(*renderer);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory, "The video decode probe ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the video decode probe boundary"};
        error.addContext("tina_sample_video_probe", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the video decode probe boundary"};
        writeError(error);
        return 1;
    }
}
