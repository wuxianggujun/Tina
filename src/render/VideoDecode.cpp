#include <tina/render/RenderDevice.hpp>

#include <limits>

namespace Tina::Render {
namespace {

[[nodiscard]] bool isKnownCodec(VideoCodec codec) noexcept
{
    switch (codec)
    {
    case VideoCodec::H264:
    case VideoCodec::H265:
    case VideoCodec::Av1:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownChroma(VideoChromaSubsampling chroma) noexcept
{
    switch (chroma)
    {
    case VideoChromaSubsampling::Yuv420:
    case VideoChromaSubsampling::Yuv422:
    case VideoChromaSubsampling::Yuv444:
        return true;
    }
    return false;
}

} // namespace

Core::Status validateVideoDecodeTextureDesc(const VideoDecodeTextureDesc& desc) noexcept
{
    if (!isKnownCodec(desc.codec))
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "A video decode target requires a supported Render::VideoCodec value");
    }
    if (!isKnownChroma(desc.chroma))
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "A video decode target requires a supported Render::VideoChromaSubsampling value");
    }
    if (desc.bitDepth != 8 && desc.bitDepth != 10 && desc.bitDepth != 12)
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "A video decode target accepts only 8, 10 or 12 bits per component");
    }
    if (desc.codedWidth == 0 || desc.codedHeight == 0)
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "A video decode target requires non-zero coded dimensions");
    }
    if (desc.codedWidth > VideoDecodeTextureDesc::MaximumDimension ||
        desc.codedHeight > VideoDecodeTextureDesc::MaximumDimension)
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "A video decode target exceeds the maximum supported coded dimension");
    }
    // A zero slot count would let the device decide the reference-picture budget,
    // which makes the same stream behave differently per backend.
    if (desc.maxDpbSlots == 0)
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "A video decode target requires at least one decoded-picture-buffer slot");
    }
    if (desc.maxActiveReferences > desc.maxDpbSlots)
    {
        return Core::failure(
            RenderErrorCode::VideoCodecUnsupported,
            "A video decode target cannot hold more active references than decoded-picture-buffer slots");
    }
    if (desc.parameterSets.size() > static_cast<usize>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "The video codec parameter sets are too large to describe");
    }
    return Core::success();
}

Core::Status validateVideoDecodeTextureCreation(const VideoDecodeTextureDesc& desc) noexcept
{
    if (Core::Status status = validateVideoDecodeTextureDesc(desc); !status)
    {
        return status;
    }
    if (desc.parameterSets.empty())
    {
        return Core::failure(RenderErrorCode::VideoCodecUnsupported,
                             "Creating a video decode target requires the codec parameter sets");
    }
    return Core::success();
}

Core::Status validateVideoDecodeSubmission(const VideoDecodeSubmission& submission) noexcept
{
    if (submission.accessUnits.empty())
    {
        // A clock-only tick carries no bitstream. Carrying one anyway means the
        // caller built a batch and lost its access-unit table, which would silently
        // decode nothing.
        if (!submission.bitstream.empty())
        {
            return Core::failure(
                RenderErrorCode::InvalidVideoDecodeSubmission,
                "A video decode submission with no access units must carry no bitstream");
        }
        if (submission.isSeekDiscontinuity)
        {
            return Core::failure(
                RenderErrorCode::InvalidVideoDecodeSubmission,
                "A seek discontinuity must submit the access units starting at the target keyframe");
        }
        return Core::success();
    }

    if (submission.bitstream.empty())
    {
        return Core::failure(RenderErrorCode::InvalidVideoDecodeSubmission,
                             "A video decode submission with access units requires their bitstream");
    }
    if (submission.accessUnits.size() > static_cast<usize>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(RenderErrorCode::InvalidVideoDecodeSubmission,
                             "A video decode submission holds too many access units to describe");
    }

    u64 totalBytes = 0;
    for (const VideoDecodeAccessUnit& accessUnit : submission.accessUnits)
    {
        if (accessUnit.byteSize == 0)
        {
            return Core::failure(RenderErrorCode::InvalidVideoDecodeSubmission,
                                 "A video decode access unit requires a non-zero byte size");
        }
        totalBytes += static_cast<u64>(accessUnit.byteSize);
    }
    // The sizes index into the bitstream, so a mismatch would make the device read
    // past the last access unit or silently drop the tail of the batch.
    if (totalBytes != static_cast<u64>(submission.bitstream.size()))
    {
        return Core::failure(
            RenderErrorCode::InvalidVideoDecodeSubmission,
            "The video decode access-unit sizes must sum to exactly the submitted bitstream size");
    }
    return Core::success();
}

} // namespace Tina::Render
