#include "Mp4Demux.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/RenderErrors.hpp>

#include <h264/h264.h>
#include <l-smash/lsmash.h>

#include <string>
#include <utility>

namespace Tina::Sample {
namespace {

using Core::i64;
using Core::u16;
using Core::u32;
using Core::u64;
using Core::u8;

// Copied from bgfx's reference player, which hardcodes the same two values. They are
// the H.264/H.265 maxima rather than this clip's requirement: the decoder
// configuration record carries no DPB size, and this demuxer does not read the SPS
// fields that would bound it. So these describe a decode target that is certainly
// large enough, not one sized to the stream.
constexpr u8 DefaultMaxDpbSlots = 16;
constexpr u8 DefaultMaxActiveReferences = 4;

// An MP4 length-prefixed NAL carries no start code; Annex B needs one per NAL. Three
// bytes is a legal start code and is what bgfx's own player emits.
void appendStartCode(std::vector<std::byte>& out)
{
    out.push_back(std::byte{0});
    out.push_back(std::byte{0});
    out.push_back(std::byte{1});
}

void appendNalAsAnnexB(std::vector<std::byte>& out, const u8* nal, u32 nalBytes)
{
    if (nal == nullptr || nalBytes == 0)
    {
        return;
    }
    appendStartCode(out);
    const auto* const bytes = reinterpret_cast<const std::byte*>(nal);
    out.insert(out.end(), bytes, bytes + nalBytes);
}

// MP4 stores each NAL as a big-endian length followed by the payload. Annex B replaces
// that length with a start code, which is the framing the hardware decoder parses.
void avccToAnnexB(std::vector<std::byte>& out, const u8* in, u32 inBytes, u32 lengthSize)
{
    u32 position = 0;
    while (position + lengthSize <= inBytes)
    {
        u32 nalBytes = 0;
        for (u32 index = 0; index < lengthSize; ++index)
        {
            nalBytes = (nalBytes << 8) | in[position + index];
        }
        position += lengthSize;
        if (position + nalBytes > inBytes)
        {
            // A truncated trailing NAL would decode as garbage, so stop at the last
            // complete one rather than submitting a partial access unit.
            break;
        }
        appendNalAsAnnexB(out, in + position, nalBytes);
        position += nalBytes;
    }
}

// An SPS is RBSP: 0x03 bytes were inserted so payload data cannot look like a start
// code. The bitstream reader needs them gone before it can read the syntax elements.
void stripEmulationPrevention(std::vector<u8>& out, const u8* source, u32 sourceBytes)
{
    out.clear();
    out.reserve(sourceBytes);

    u32 zeros = 0;
    for (u32 index = 0; index < sourceBytes; ++index)
    {
        if (zeros >= 2 && source[index] == 0x03)
        {
            zeros = 0;
            continue;
        }
        zeros = source[index] == 0 ? zeros + 1 : 0;
        out.push_back(source[index]);
    }
}

[[nodiscard]] Render::VideoChromaSubsampling chromaFromIdc(int chromaFormatIdc) noexcept
{
    switch (chromaFormatIdc)
    {
    case 3:
        return Render::VideoChromaSubsampling::Yuv444;
    case 2:
        return Render::VideoChromaSubsampling::Yuv422;
    default:
        return Render::VideoChromaSubsampling::Yuv420;
    }
}

// avcC (ISO/IEC 14496-15 AVCDecoderConfigurationRecord), preceded by its 8-byte box
// header. Yields the Annex B parameter sets, the NAL length prefix width every sample
// uses, and the chroma/bit depth the decode target must be created with.
[[nodiscard]] bool parseAvcC(DemuxedVideo& video, const u8* data, u32 dataBytes, u32& lengthSize)
{
    constexpr u32 BoxHeaderBytes = 8;
    // configurationVersion, profile, compatibility, level, lengthSizeMinusOne, numSps.
    constexpr u32 FixedFieldBytes = 6;
    if (data == nullptr || dataBytes < BoxHeaderBytes + FixedFieldBytes)
    {
        return false;
    }

    u32 position = BoxHeaderBytes;
    ++position; // configurationVersion
    ++position; // AVCProfileIndication
    ++position; // profile_compatibility
    ++position; // AVCLevelIndication
    lengthSize = static_cast<u32>(data[position++] & 0x03) + 1;
    const u32 spsCount = static_cast<u32>(data[position++] & 0x1f);

    // Absent an SPS these stay at the 8-bit 4:2:0 default, which is what the H.264
    // baseline profile means when it omits the fields.
    video.chroma = Render::VideoChromaSubsampling::Yuv420;
    video.bitDepth = 8;

    bool spsParsed = false;
    std::vector<u8> rbsp;

    for (u32 index = 0; index < spsCount; ++index)
    {
        if (position + 2 > dataBytes)
        {
            return false;
        }
        const u32 nalBytes = (static_cast<u32>(data[position]) << 8) | static_cast<u32>(data[position + 1]);
        position += 2;
        if (position + nalBytes > dataBytes)
        {
            return false;
        }

        appendNalAsAnnexB(video.parameterSets, data + position, nalBytes);

        if (!spsParsed && nalBytes >= 1)
        {
            stripEmulationPrevention(rbsp, data + position, nalBytes);

            h264::Bitstream bitstream;
            bitstream.init(rbsp.data(), rbsp.size());

            h264::NALHeader nal{};
            if (h264::read_nal_header(&nal, &bitstream) && nal.type == h264::NAL_UNIT_TYPE_SPS)
            {
                h264::SPS sps{};
                h264::read_sps(&sps, &bitstream);
                video.chroma = chromaFromIdc(sps.chroma_format_idc);
                video.bitDepth = static_cast<u8>(sps.bit_depth_luma_minus8 + 8);
                spsParsed = true;
            }
        }
        position += nalBytes;
    }

    if (position + 1 > dataBytes)
    {
        return false;
    }
    const u32 ppsCount = data[position++];
    for (u32 index = 0; index < ppsCount; ++index)
    {
        if (position + 2 > dataBytes)
        {
            return false;
        }
        const u32 nalBytes = (static_cast<u32>(data[position]) << 8) | static_cast<u32>(data[position + 1]);
        position += 2;
        if (position + nalBytes > dataBytes)
        {
            return false;
        }
        appendNalAsAnnexB(video.parameterSets, data + position, nalBytes);
        position += nalBytes;
    }

    return !video.parameterSets.empty();
}

// hvcC (ISO/IEC 14496-15 HEVCDecoderConfigurationRecord). Unlike avcC this record
// states chroma and bit depth directly, so no SPS parse is needed.
[[nodiscard]] bool parseHvcC(DemuxedVideo& video, const u8* data, u32 dataBytes, u32& lengthSize)
{
    constexpr u32 BoxHeaderBytes = 8;
    constexpr u32 FixedFieldBytes = 23;
    if (data == nullptr || dataBytes < BoxHeaderBytes + FixedFieldBytes)
    {
        return false;
    }

    u32 position = BoxHeaderBytes;
    ++position;      // configurationVersion
    ++position;      // general_profile_space/tier/idc
    position += 10;  // general_profile_compatibility_flags + constraint flags
    ++position;      // general_level_idc
    position += 3;   // min_spatial_segmentation_idc, parallelismType (packed)

    video.chroma = chromaFromIdc(static_cast<int>(data[position++] & 0x03));
    video.bitDepth = static_cast<u8>((data[position++] & 0x07) + 8);
    position += 3;   // bitDepthChromaMinus8, avgFrameRate (packed)

    lengthSize = static_cast<u32>(data[position++] & 0x03) + 1;

    if (position >= dataBytes)
    {
        return false;
    }
    const u32 arrayCount = data[position++];
    for (u32 arrayIndex = 0; arrayIndex < arrayCount; ++arrayIndex)
    {
        if (position + 3 > dataBytes)
        {
            return false;
        }
        ++position; // array_completeness + NAL_unit_type
        const u32 nalCount = (static_cast<u32>(data[position]) << 8) | static_cast<u32>(data[position + 1]);
        position += 2;

        for (u32 nalIndex = 0; nalIndex < nalCount; ++nalIndex)
        {
            if (position + 2 > dataBytes)
            {
                return false;
            }
            const u32 nalBytes = (static_cast<u32>(data[position]) << 8) | static_cast<u32>(data[position + 1]);
            position += 2;
            if (position + nalBytes > dataBytes)
            {
                return false;
            }
            appendNalAsAnnexB(video.parameterSets, data + position, nalBytes);
            position += nalBytes;
        }
    }

    return !video.parameterSets.empty();
}

// Owns the l-smash root and file parameters so every early return releases them.
class LsmashReader final {
public:
    LsmashReader() = default;

    LsmashReader(const LsmashReader&) = delete;
    LsmashReader& operator=(const LsmashReader&) = delete;
    LsmashReader(LsmashReader&&) = delete;
    LsmashReader& operator=(LsmashReader&&) = delete;

    ~LsmashReader()
    {
        if (summary_ != nullptr)
        {
            lsmash_cleanup_summary(summary_);
        }
        if (fileOpened_)
        {
            lsmash_close_file(&fileParameters_);
        }
        if (root_ != nullptr)
        {
            lsmash_destroy_root(root_);
        }
    }

    [[nodiscard]] bool open(const char* path)
    {
        root_ = lsmash_create_root();
        if (root_ == nullptr)
        {
            return false;
        }
        if (lsmash_open_file(path, 1, &fileParameters_) < 0)
        {
            return false;
        }
        fileOpened_ = true;

        lsmash_file_t* const file = lsmash_set_file(root_, &fileParameters_);
        if (file == nullptr)
        {
            return false;
        }
        return lsmash_read_file(file, &fileParameters_) >= 0;
    }

    [[nodiscard]] lsmash_root_t* root() const noexcept { return root_; }

    [[nodiscard]] lsmash_summary_t* takeSummaryFor(u32 trackId)
    {
        summary_ = lsmash_get_summary(root_, trackId, 1);
        return summary_;
    }

private:
    lsmash_root_t* root_ = nullptr;
    lsmash_file_parameters_t fileParameters_{};
    bool fileOpened_ = false;
    lsmash_summary_t* summary_ = nullptr;
};

// Owns one sample so the Annex B conversion can return early without leaking it.
class LsmashSample final {
public:
    LsmashSample(lsmash_root_t* root, u32 trackId, u32 sampleNumber)
        : sample_(lsmash_get_sample_from_media_timeline(root, trackId, sampleNumber))
    {
    }

    LsmashSample(const LsmashSample&) = delete;
    LsmashSample& operator=(const LsmashSample&) = delete;
    LsmashSample(LsmashSample&&) = delete;
    LsmashSample& operator=(LsmashSample&&) = delete;

    ~LsmashSample()
    {
        if (sample_ != nullptr)
        {
            lsmash_delete_sample(sample_);
        }
    }

    [[nodiscard]] const lsmash_sample_t* get() const noexcept { return sample_; }

private:
    lsmash_sample_t* sample_ = nullptr;
};

[[nodiscard]] Core::Error demuxFailure(Core::ErrorCode code, const char* message, std::string_view path)
{
    Core::Error error{code, message};
    error.addContext("demuxMp4Video", path);
    return error;
}

[[nodiscard]] u32 findVideoTrackId(lsmash_root_t* root, u32 trackCount)
{
    for (u32 index = 0; index < trackCount; ++index)
    {
        const u32 trackId = lsmash_get_track_ID(root, index + 1);
        if (trackId == 0)
        {
            continue;
        }

        lsmash_media_parameters_t media{};
        lsmash_initialize_media_parameters(&media);
        if (lsmash_get_media_parameters(root, trackId, &media) < 0)
        {
            continue;
        }
        if (media.handler_type == ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK)
        {
            return trackId;
        }
    }
    return 0;
}

// The decoder configuration record is stored per sample entry and may be in l-smash's
// structured form, which has to be converted back to the raw box bytes before parsing.
[[nodiscard]] bool readParameterSets(DemuxedVideo& video,
                                     lsmash_summary_t* summary,
                                     lsmash_codec_specific_data_type wanted,
                                     u32& lengthSize)
{
    for (u32 index = 0;; ++index)
    {
        lsmash_codec_specific_t* const specific = lsmash_get_codec_specific_data(summary, index + 1);
        if (specific == nullptr)
        {
            return false;
        }
        if (specific->type != wanted)
        {
            continue;
        }

        lsmash_codec_specific_t* const raw =
            specific->format == LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED
                ? specific
                : lsmash_convert_codec_specific_format(specific, LSMASH_CODEC_SPECIFIC_FORMAT_UNSTRUCTURED);
        if (raw == nullptr)
        {
            continue;
        }

        const bool parsed = video.codec == Render::VideoCodec::H264
                                ? parseAvcC(video, raw->data.unstructured, raw->size, lengthSize)
                                : parseHvcC(video, raw->data.unstructured, raw->size, lengthSize);

        if (raw != specific)
        {
            lsmash_destroy_codec_specific_data(raw);
        }
        return parsed;
    }
}

} // namespace

Core::Result<DemuxedVideo> demuxMp4Video(std::string_view utf8Path)
{
    // l-smash takes a NUL-terminated path.
    const std::string path{utf8Path};

    LsmashReader reader;
    if (!reader.open(path.c_str()))
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io, "Could not open the MP4 file as an ISO base media file", utf8Path));
    }

    lsmash_movie_parameters_t movie{};
    lsmash_initialize_movie_parameters(&movie);
    if (lsmash_get_movie_parameters(reader.root(), &movie) < 0)
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io, "The MP4 file has no readable movie header", utf8Path));
    }

    const u32 trackId = findVideoTrackId(reader.root(), movie.number_of_tracks);
    if (trackId == 0)
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::NotFound, "The MP4 file contains no video track", utf8Path));
    }

    lsmash_media_parameters_t media{};
    lsmash_initialize_media_parameters(&media);
    if (lsmash_get_media_parameters(reader.root(), trackId, &media) < 0)
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io, "The video track has no readable media header", utf8Path));
    }

    lsmash_summary_t* const summary = reader.takeSummaryFor(trackId);
    if (summary == nullptr)
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io, "The video track has no sample description", utf8Path));
    }

    DemuxedVideo video{};
    lsmash_codec_specific_data_type wanted{};
    if (lsmash_check_codec_type_identical(summary->sample_type, ISOM_CODEC_TYPE_AVC1_VIDEO) ||
        lsmash_check_codec_type_identical(summary->sample_type, ISOM_CODEC_TYPE_AVC3_VIDEO))
    {
        video.codec = Render::VideoCodec::H264;
        wanted = LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264;
    }
    else if (lsmash_check_codec_type_identical(summary->sample_type, ISOM_CODEC_TYPE_HVC1_VIDEO))
    {
        video.codec = Render::VideoCodec::H265;
        wanted = LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_HEVC;
    }
    else
    {
        // AV1 in MP4 uses av1C, which l-smash does not model; there is no point
        // reporting a codec Tina would then fail to find parameter sets for.
        return Core::failure(demuxFailure(Render::RenderErrorCode::VideoCodecUnsupported,
                                          "The video track is neither H.264 nor H.265",
                                          utf8Path));
    }

    const auto* const videoSummary = reinterpret_cast<const lsmash_video_summary_t*>(summary);
    video.width = static_cast<u16>(videoSummary->width);
    video.height = static_cast<u16>(videoSummary->height);
    if (video.width == 0 || video.height == 0)
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io, "The video track declares a zero extent", utf8Path));
    }

    u32 lengthSize = 4;
    if (!readParameterSets(video, summary, wanted, lengthSize))
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io,
                                          "The video track has no parsable decoder configuration record",
                                          utf8Path));
    }

    video.maxDpbSlots = DefaultMaxDpbSlots;
    video.maxActiveReferences = DefaultMaxActiveReferences;

    if (lsmash_construct_timeline(reader.root(), trackId) < 0)
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io, "The video track has no constructible timeline", utf8Path));
    }

    const u64 timescale = media.timescale != 0 ? media.timescale : 1;
    const u32 sampleCount = lsmash_get_sample_count_in_media_timeline(reader.root(), trackId);
    if (sampleCount == 0)
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::NotFound, "The video track contains no samples", utf8Path));
    }

    video.accessUnits.reserve(sampleCount);
    for (u32 index = 0; index < sampleCount; ++index)
    {
        const LsmashSample sample{reader.root(), trackId, index + 1};
        const lsmash_sample_t* const raw = sample.get();
        if (raw == nullptr)
        {
            break;
        }

        DemuxedAccessUnit accessUnit{};
        // cts is composition (presentation) time; the decoder's picker matches on it.
        accessUnit.presentationTimeUs =
            static_cast<i64>(static_cast<u64>(raw->cts) * 1000000ULL / timescale);
        accessUnit.isKeyframe = (raw->prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC) != 0;
        avccToAnnexB(accessUnit.bytes, raw->data, raw->length, lengthSize);
        if (accessUnit.bytes.empty())
        {
            continue;
        }
        video.accessUnits.push_back(std::move(accessUnit));
    }

    if (video.accessUnits.empty())
    {
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io, "No video access unit survived Annex B conversion", utf8Path));
    }
    if (!video.accessUnits.front().isKeyframe)
    {
        // The first submission is a seek discontinuity, which the decode SPI requires
        // to start on a clean IDR. A clip that does not is not playable from the start.
        return Core::failure(demuxFailure(Core::CoreErrorCode::Io,
                                          "The first video access unit is not a keyframe",
                                          utf8Path));
    }

    return video;
}

} // namespace Tina::Sample
