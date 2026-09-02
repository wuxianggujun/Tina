#include "BgfxVideoDecode.hpp"

#include <bx/bx.h>

#include <cstring>
#include <limits>

namespace Tina::Render::Bgfx {
namespace {

// bgfx identifies a video-decode creation blob and a decode submission by these
// leading magics rather than by a texture flag or a dedicated entry point.
constexpr u32 VideoDecoderInitMagic = BX_MAKEFOURCC('V', 'D', 'I', 0x0);
constexpr u32 VideoDecoderFrameMagic = BX_MAKEFOURCC('V', 'D', 'F', 0x0);

[[nodiscard]] Core::Error videoDecodeUnsupported(const char* message) noexcept
{
    return Core::Error{RenderErrorCode::VideoDecodeUnsupported, message};
}

[[nodiscard]] VideoDecodeCapabilities::CodecSupport readCodecSupport(u32 codecCaps) noexcept
{
    return VideoDecodeCapabilities::CodecSupport{
        .bitDepth8 = (codecCaps & BGFX_CAPS_VIDEO_CODEC_BIT_8) != 0,
        .bitDepth10 = (codecCaps & BGFX_CAPS_VIDEO_CODEC_BIT_10) != 0,
        .bitDepth12 = (codecCaps & BGFX_CAPS_VIDEO_CODEC_BIT_12) != 0,
        .yuv420 = (codecCaps & BGFX_CAPS_VIDEO_CODEC_CHROMA_420) != 0,
        .yuv422 = (codecCaps & BGFX_CAPS_VIDEO_CODEC_CHROMA_422) != 0,
        .yuv444 = (codecCaps & BGFX_CAPS_VIDEO_CODEC_CHROMA_444) != 0,
    };
}

[[nodiscard]] bool hasBitDepthSupport(const VideoDecodeCapabilities::CodecSupport& support,
                                      u8 bitDepth) noexcept
{
    switch (bitDepth)
    {
    case 8:
        return support.bitDepth8;
    case 10:
        return support.bitDepth10;
    case 12:
        return support.bitDepth12;
    default:
        return false;
    }
}

[[nodiscard]] bool hasChromaSupport(const VideoDecodeCapabilities::CodecSupport& support,
                                     VideoChromaSubsampling chroma) noexcept
{
    switch (chroma)
    {
    case VideoChromaSubsampling::Yuv420:
        return support.yuv420;
    case VideoChromaSubsampling::Yuv422:
        return support.yuv422;
    case VideoChromaSubsampling::Yuv444:
        return support.yuv444;
    }
    return false;
}

[[nodiscard]] u8 toSubmissionFlags(const VideoDecodeSubmission& submission) noexcept
{
    u8 flags = BGFX_VIDEO_DECODE_FRAME_NONE;
    if (submission.isSeekDiscontinuity)
    {
        flags |= BGFX_VIDEO_DECODE_FRAME_SET;
    }
    if (submission.suppressPresentation)
    {
        flags |= BGFX_VIDEO_DECODE_FRAME_NO_BLIT;
    }
    if (submission.isFinalAccessUnit)
    {
        flags |= BGFX_VIDEO_DECODE_FRAME_FINAL;
    }
    return flags;
}

} // namespace

bgfx::VideoCodec::Enum toBgfxVideoCodec(VideoCodec codec) noexcept
{
    switch (codec)
    {
    case VideoCodec::H264:
        return bgfx::VideoCodec::H264;
    case VideoCodec::H265:
        return bgfx::VideoCodec::H265;
    case VideoCodec::Av1:
        return bgfx::VideoCodec::AV1;
    }
    return bgfx::VideoCodec::Count;
}

u8 toBgfxChromaCode(VideoChromaSubsampling chroma) noexcept
{
    switch (chroma)
    {
    case VideoChromaSubsampling::Yuv420:
        return 0;
    case VideoChromaSubsampling::Yuv422:
        return 2;
    case VideoChromaSubsampling::Yuv444:
        return 4;
    }
    return 0;
}

VideoDecodeCapabilities readVideoDecodeCapabilities() noexcept
{
    VideoDecodeCapabilities capabilities{};

    const bgfx::Caps* const caps = bgfx::getCaps();
    if (caps == nullptr)
    {
        return capabilities;
    }

    capabilities.supported = (caps->supported & BGFX_CAPS_VIDEO_DECODE) != 0;
    if (!capabilities.supported)
    {
        // Leave the per-codec entries false. A renderer without a decoder still
        // publishes a codecs[] array, and reporting its contents would advertise
        // codecs that cannot be used.
        return capabilities;
    }

    capabilities.destinationFormatSupported =
        (caps->formats[VideoDecodeDestinationFormat] & BGFX_CAPS_FORMAT_TEXTURE_VIDEO_DECODE_DST) != 0;

    static_assert(VideoDecodeCapabilities::CodecCount <= static_cast<usize>(bgfx::VideoCodec::Count),
                  "Tina advertises more video codecs than bgfx reports capabilities for");
    for (usize index = 0; index < VideoDecodeCapabilities::CodecCount; ++index)
    {
        capabilities.codecs[index] = readCodecSupport(caps->codecs[index]);
    }
    return capabilities;
}

bool isVideoDecodeSupported(const VideoDecodeTextureDesc& desc) noexcept
{
    if (!validateVideoDecodeTextureDesc(desc))
    {
        return false;
    }

    const VideoDecodeCapabilities capabilities = readVideoDecodeCapabilities();
    if (!capabilities.supported || !capabilities.destinationFormatSupported)
    {
        return false;
    }

    const bgfx::VideoCodec::Enum codec = toBgfxVideoCodec(desc.codec);
    if (codec == bgfx::VideoCodec::Count)
    {
        return false;
    }

    const VideoDecodeCapabilities::CodecSupport& support = capabilities.codec(desc.codec);
    if (!hasBitDepthSupport(support, desc.bitDepth) || !hasChromaSupport(support, desc.chroma))
    {
        return false;
    }

    // The capability bits say nothing about extent or picture-buffer limits, so the
    // per-stream probe is required rather than merely confirmatory.
    return bgfx::isVideoCodecValid(codec,
                                   toBgfxChromaCode(desc.chroma),
                                   desc.bitDepth,
                                   desc.codedWidth,
                                   desc.codedHeight,
                                   desc.maxDpbSlots,
                                   desc.maxActiveReferences);
}

Core::Result<bgfx::TextureHandle> createVideoDecodeTexture(const VideoDecodeTextureDesc& desc)
{
    if (Core::Status status = validateVideoDecodeTextureCreation(desc); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!isVideoDecodeSupported(desc))
    {
        return Core::failure(
            RenderErrorCode::VideoCodecUnsupported,
            "This device cannot hardware decode the requested codec, bit depth, chroma format or extent");
    }

    const u32 parameterSetsBytes = static_cast<u32>(desc.parameterSets.size());
    const u32 totalBytes = static_cast<u32>(sizeof(bgfx::VideoDecoderInit)) + parameterSetsBytes;

    const bgfx::Memory* memory = bgfx::alloc(totalBytes);
    if (memory == nullptr)
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx failed to allocate the video decoder initialization blob");
    }

    // The parameter sets live immediately after the struct inside the same
    // allocation, so the pointer stays valid for as long as bgfx holds the blob.
    u8* const parameterSetsDestination = memory->data + sizeof(bgfx::VideoDecoderInit);
    std::memcpy(parameterSetsDestination, desc.parameterSets.data(), parameterSetsBytes);

    auto* const init = reinterpret_cast<bgfx::VideoDecoderInit*>(memory->data);
    init->magic = VideoDecoderInitMagic;
    init->codec = toBgfxVideoCodec(desc.codec);
    init->parameterSets = parameterSetsDestination;
    init->parameterSetsSize = parameterSetsBytes;
    // Zero selects bgfx's default streaming cache size. Tina submits access units
    // just ahead of the playback clock rather than bulk-loading a clip, so the
    // retain cache (which is unbounded) is deliberately not requested.
    init->cachedAuBytes = 0;
    init->flags = BGFX_VIDEO_DECODER_INIT_NONE;

    const bgfx::TextureHandle texture = bgfx::createTexture2D(desc.codedWidth,
                                                              desc.codedHeight,
                                                              false,
                                                              1,
                                                              VideoDecodeDestinationFormat,
                                                              BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                                              memory);
    if (!bgfx::isValid(texture))
    {
        return Core::failure(videoDecodeUnsupported("bgfx rejected the video decode destination texture"));
    }
    return texture;
}

Core::Status submitVideoDecodeFrame(bgfx::TextureHandle texture,
                                    u16 codedWidth,
                                    u16 codedHeight,
                                    const VideoDecodeSubmission& submission)
{
    if (!bgfx::isValid(texture))
    {
        return Core::failure(RenderErrorCode::VideoDecodeTextureNotFound,
                             "The video decode destination texture handle is invalid");
    }
    if (Core::Status status = validateVideoDecodeSubmission(submission); !status)
    {
        return status;
    }

    const u32 accessUnitCount = static_cast<u32>(submission.accessUnits.size());

    if (accessUnitCount == 0)
    {
        // Presentation-only tick: nothing is referenced by pointer, so a plain copy
        // of the struct is sufficient.
        bgfx::VideoDecoderFrame frame{};
        frame.magic = VideoDecoderFrameMagic;
        frame.bitstream = nullptr;
        frame.aus = nullptr;
        frame.numAus = 0;
        frame.presentationTimeUs = submission.presentationTimeUs;
        frame.flags = toSubmissionFlags(submission);

        const bgfx::Memory* memory = bgfx::copy(&frame, sizeof(frame));
        if (memory == nullptr)
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx failed to allocate the video presentation tick blob");
        }
        bgfx::updateTexture2D(texture, 0, 0, 0, 0, codedWidth, codedHeight, memory);
        return Core::success();
    }

    const u64 bitstreamBytes = static_cast<u64>(submission.bitstream.size());
    const u64 accessUnitBytes =
        static_cast<u64>(accessUnitCount) * static_cast<u64>(sizeof(bgfx::VideoDecoderAu));
    const u64 totalBytes64 =
        static_cast<u64>(sizeof(bgfx::VideoDecoderFrame)) + accessUnitBytes + bitstreamBytes;
    if (totalBytes64 > static_cast<u64>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(RenderErrorCode::InvalidVideoDecodeSubmission,
                             "The video decode submission is too large for a single bgfx allocation");
    }

    // bgfx deep-copies only the VideoDecoderFrame struct, never the buffers it
    // points at, so the access-unit table and the bitstream must live inside the
    // same allocation as the struct. Laying the 8-byte-aligned structs out first
    // keeps both correctly aligned; the bitstream is byte data and needs none.
    const bgfx::Memory* memory = bgfx::alloc(static_cast<u32>(totalBytes64));
    if (memory == nullptr)
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx failed to allocate the video decode submission blob");
    }

    auto* const frame = reinterpret_cast<bgfx::VideoDecoderFrame*>(memory->data);
    auto* const accessUnits =
        reinterpret_cast<bgfx::VideoDecoderAu*>(memory->data + sizeof(bgfx::VideoDecoderFrame));
    u8* const bitstream = memory->data + sizeof(bgfx::VideoDecoderFrame) + accessUnitBytes;

    for (u32 index = 0; index < accessUnitCount; ++index)
    {
        accessUnits[index].size = submission.accessUnits[index].byteSize;
        accessUnits[index].ptsUs = submission.accessUnits[index].presentationTimeUs;
    }
    if (bitstreamBytes != 0)
    {
        std::memcpy(bitstream, submission.bitstream.data(), static_cast<usize>(bitstreamBytes));
    }

    frame->magic = VideoDecoderFrameMagic;
    frame->bitstream = bitstream;
    frame->aus = accessUnits;
    frame->numAus = accessUnitCount;
    frame->presentationTimeUs = submission.presentationTimeUs;
    frame->flags = toSubmissionFlags(submission);

    bgfx::updateTexture2D(texture, 0, 0, 0, 0, codedWidth, codedHeight, memory);
    return Core::success();
}

} // namespace Tina::Render::Bgfx
