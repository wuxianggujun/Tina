// Reports what Tina's MP4 demuxer extracts from a clip, and fails if the result could
// not drive a hardware decoder.
//
// This is the CPU half of video playback and nothing more: it opens no window, creates
// no device and decodes no picture. That boundary is deliberate: driving the decode SPI
// needs a real window surface, which this program has no reason to open. The sample that
// does decode is samples/video_playback. What can be verified without a device is that
// the container work is correct, and this program verifies exactly that much.
//
// The checks are the preconditions the decode SPI documents, so a clip that passes here
// is one whose failure would be the decoder's, not the demuxer's.
#include "Mp4Demux.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/core/text/JsonWriter.hpp>

#include <cstddef>
#include <exception>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Tina::Core::u32;
using Tina::Core::u8;

// H.264/H.265 NAL header: the low 5 bits of the byte after the start code are the unit
// type for H.264. Only used to report which parameter sets were found.
constexpr u8 H264NalTypeSps = 7;
constexpr u8 H264NalTypePps = 8;

struct ParameterSetSummary final {
    u32 nalCount = 0;
    bool hasSps = false;
    bool hasPps = false;
};

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<u32>(code.domain)) + "." + std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_video_demux");
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

// Walks the Annex B parameter set blob and reports which NAL types it contains. The
// demuxer writes 3-byte start codes, so this scans for those rather than accepting a
// count on faith.
[[nodiscard]] ParameterSetSummary summarizeParameterSets(std::span<const std::byte> parameterSets,
                                                         Tina::Render::VideoCodec codec)
{
    ParameterSetSummary summary{};

    for (std::size_t index = 0; index + 3 < parameterSets.size(); ++index)
    {
        const bool atStartCode = parameterSets[index] == std::byte{0} &&
                                 parameterSets[index + 1] == std::byte{0} &&
                                 parameterSets[index + 2] == std::byte{1};
        if (!atStartCode)
        {
            continue;
        }

        ++summary.nalCount;
        const auto header = static_cast<u8>(parameterSets[index + 3]);
        if (codec == Tina::Render::VideoCodec::H264)
        {
            const u8 nalType = header & 0x1f;
            summary.hasSps = summary.hasSps || nalType == H264NalTypeSps;
            summary.hasPps = summary.hasPps || nalType == H264NalTypePps;
        }
        else
        {
            // H.265 packs the type into bits 1..6 of the first header byte. 33 is SPS
            // and 34 is PPS.
            const u8 nalType = static_cast<u8>((header >> 1) & 0x3f);
            summary.hasSps = summary.hasSps || nalType == 33;
            summary.hasPps = summary.hasPps || nalType == 34;
        }
    }

    return summary;
}

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

[[nodiscard]] std::string_view chromaName(Tina::Render::VideoChromaSubsampling chroma) noexcept
{
    switch (chroma)
    {
    case Tina::Render::VideoChromaSubsampling::Yuv420:
        return "yuv420";
    case Tina::Render::VideoChromaSubsampling::Yuv422:
        return "yuv422";
    case Tina::Render::VideoChromaSubsampling::Yuv444:
        return "yuv444";
    }
    return "unknown";
}

// Only the two conditions the demuxer does not already enforce.
//
// A non-empty parameter set blob, a non-empty access unit list, a keyframe first access
// unit and a non-zero extent are all rejected inside demuxMp4Video, so asserting them
// again here could never report false: the failure arrives as an error instead. Annex B
// framing is likewise not checked, because this demuxer writes the start codes itself --
// verifying them would only be testing the line above.
//
// What remains genuinely varies per clip, which is why it is worth reporting.
struct Checks final {
    // avcC states its SPS and PPS counts separately, so a record carrying only one kind
    // still yields a non-empty blob. A decoder given that has no complete configuration.
    bool hasSpsAndPps = false;
    // A decoder cannot start on a stream whose first submitted access unit is not a
    // clean IDR, which is exactly what VideoDecodeSubmission::isSeekDiscontinuity
    // documents. Access units come out in decode order, so this is the first one.
    bool startsOnKeyframe = false;
    // An empty access unit slices a zero-length bitstream, which the SPI rejects.
    bool everyAccessUnitHasBytes = false;
    // Composition times must be distinct or two decoded pictures compete to be the
    // one the presentation clock selects, and which wins is unspecified.
    bool presentationTimesAreDistinct = false;

    [[nodiscard]] bool allPassed() const noexcept
    {
        return hasSpsAndPps && startsOnKeyframe && everyAccessUnitHasBytes
            && presentationTimesAreDistinct;
    }
};

[[nodiscard]] Checks runChecks(const Tina::Sample::DemuxedVideo& video,
                               const ParameterSetSummary& parameterSets)
{
    Checks checks{};
    checks.hasSpsAndPps = parameterSets.hasSps && parameterSets.hasPps;

    if (!video.accessUnits.empty())
    {
        checks.startsOnKeyframe = video.accessUnits.front().isKeyframe;

        bool everyUnitHasBytes = true;
        std::set<Tina::Core::i64> presentationTimes;
        for (const Tina::Sample::DemuxedAccessUnit& accessUnit : video.accessUnits)
        {
            if (accessUnit.bytes.empty())
            {
                everyUnitHasBytes = false;
            }
            presentationTimes.insert(accessUnit.presentationTimeUs);
        }
        checks.everyAccessUnitHasBytes = everyUnitHasBytes;
        checks.presentationTimesAreDistinct =
            presentationTimes.size() == video.accessUnits.size();
    }

    return checks;
}

void writeReport(std::string_view clipPath,
                 const Tina::Sample::DemuxedVideo& video,
                 const ParameterSetSummary& parameterSets,
                 const Checks& checks)
{
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("sample", "tina_sample_video_demux");
    writer.member("clip", clipPath);
    writer.member("codec", codecName(video.codec));
    writer.member("chroma", chromaName(video.chroma));
    // JsonWriter excludes u8 so it cannot be emitted as a character; these are counts,
    // so they are widened rather than quoted.
    writer.member("bitDepth", static_cast<u32>(video.bitDepth));
    writer.member("width", video.width);
    writer.member("height", video.height);
    writer.member("maxDpbSlots", static_cast<u32>(video.maxDpbSlots));
    writer.member("maxActiveReferences", static_cast<u32>(video.maxActiveReferences));
    writer.member("parameterSetBytes", static_cast<u32>(video.parameterSets.size()));
    writer.member("parameterSetNalCount", parameterSets.nalCount);
    writer.member("accessUnitCount", static_cast<u32>(video.accessUnits.size()));
    // A player drives its own presentation clock but the decoder matches decoded pictures
    // against these values, so a clock that never enters this range displays nothing while
    // every decode still succeeds. The origin and step are what a caller has to honour.
    if (!video.accessUnits.empty())
    {
        writer.member("firstAccessUnitPtsUs", video.accessUnits.front().presentationTimeUs);
        writer.member("lastAccessUnitPtsUs", video.accessUnits.back().presentationTimeUs);
        writer.member("firstAccessUnitByteSize",
                      static_cast<u32>(video.accessUnits.front().bytes.size()));
    }

    writer.beginObjectMember("checks");
    writer.member("hasSpsAndPps", checks.hasSpsAndPps);
    writer.member("startsOnKeyframe", checks.startsOnKeyframe);
    writer.member("everyAccessUnitHasBytes", checks.everyAccessUnitHasBytes);
    writer.member("presentationTimesAreDistinct", checks.presentationTimesAreDistinct);
    writer.endObject();

    writer.member("allChecksPassed", checks.allPassed());
    writer.endObject();
    std::cout << '\n';
}

[[nodiscard]] int runDemux(std::string_view clipPath)
{
    auto demuxed = Tina::Sample::demuxMp4Video(clipPath);
    if (!demuxed)
    {
        writeError(demuxed.error());
        return 1;
    }

    const ParameterSetSummary parameterSets =
        summarizeParameterSets(demuxed->parameterSets, demuxed->codec);
    const Checks checks = runChecks(*demuxed, parameterSets);

    writeReport(clipPath, *demuxed, parameterSets, checks);
    return checks.allPassed() ? 0 : 3;
}

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
                                    "The video demux sample accepts only --clip="};
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
                                "The video demux sample requires --clip=<path to an MP4>"};
        return Tina::Core::failure(std::move(error));
    }
    return clipPath;
}

} // namespace

int runVideoDemuxSample(int argumentCount, char** arguments)
{
    try
    {
        auto clipPath = parseClipPath(argumentCount, arguments);
        if (!clipPath)
        {
            writeError(clipPath.error());
            return 2;
        }
        return runDemux(*clipPath);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The video demux sample ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the video demux sample boundary"};
        error.addContext("tina_sample_video_demux", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the video demux sample boundary"};
        writeError(error);
        return 1;
    }
}
