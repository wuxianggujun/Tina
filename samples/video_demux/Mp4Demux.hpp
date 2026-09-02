#pragma once

// Turns one MP4 file into what IRenderDevice's video decode SPI asks for: Annex B
// parameter sets plus per-access-unit Annex B bitstreams in decode order.
//
// This is a demuxer, not a decoder. It reads the container and rewrites NAL framing;
// the pictures themselves are produced by the GPU. That split is why no software codec
// is needed here.
//
// It lives beside the sample rather than in Tina::Asset because where demuxing belongs
// is still open: cooking a clip into a VideoClip asset and streaming the original MP4
// at runtime want the same code in different libraries. Linking l-smash into
// Tina::Asset would put an MP4 parser in every game binary, which is a decision this
// program does not need to make in order to be useful.
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace Tina::Sample {

// One access unit, already converted to Annex B and self-contained: `bytes` is exactly
// what a single-AU VideoDecodeSubmission submits.
struct DemuxedAccessUnit final {
    std::vector<std::byte> bytes;
    Core::i64 presentationTimeUs = 0;
    bool isKeyframe = false;
};

// The whole clip. The stream description mirrors VideoDecodeTextureDesc because that is
// the only consumer: values are read out of the container and the SPS, never guessed.
struct DemuxedVideo final {
    Render::VideoCodec codec = Render::VideoCodec::H264;
    Render::VideoChromaSubsampling chroma = Render::VideoChromaSubsampling::Yuv420;
    Core::u8 bitDepth = 8;
    Core::u16 width = 0;
    Core::u16 height = 0;
    Core::u8 maxDpbSlots = 0;
    Core::u8 maxActiveReferences = 0;
    // Annex B SPS/PPS (H.264) or VPS/SPS/PPS (H.265), concatenated with start codes.
    std::vector<std::byte> parameterSets;
    std::vector<DemuxedAccessUnit> accessUnits;
};

// Reads the first video track of an MP4. Fails closed on anything it cannot describe
// exactly: a missing video track, a codec other than H.264/H.265, or an unparsable
// decoder configuration record.
[[nodiscard]] Core::Result<DemuxedVideo> demuxMp4Video(std::string_view utf8Path);

} // namespace Tina::Sample
