#include <tina/audio/AudioEncode.hpp>

#include "../decode/OggValidation.hpp"
#include <tina/audio/AudioErrors.hpp>

#include <opus/opus.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <span>
#include <vector>

namespace Tina::Audio {
namespace {

constexpr Core::u32 OpusSampleRate = 48000;
constexpr int OpusFrameSamples = 960; // 20 ms at 48 kHz
constexpr Core::u32 DefaultMonoBitrate = 64000;
constexpr Core::u32 DefaultStereoBitrate = 96000;
constexpr Core::u32 OggSerial = 1;

void appendU8(std::vector<std::byte>& bytes, Core::u8 value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

void appendU16(std::vector<std::byte>& bytes, Core::u16 value)
{
    appendU8(bytes, static_cast<Core::u8>(value & 0xFFU));
    appendU8(bytes, static_cast<Core::u8>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::byte>& bytes, Core::u32 value)
{
    for (Core::u32 index = 0; index < 4U; ++index)
    {
        appendU8(bytes, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void appendU64(std::vector<std::byte>& bytes, Core::u64 value)
{
    for (Core::u32 index = 0; index < 8U; ++index)
    {
        appendU8(bytes, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void appendBytes(std::vector<std::byte>& bytes, std::span<const std::byte> source)
{
    bytes.insert(bytes.end(), source.begin(), source.end());
}

void writeU32At(std::vector<std::byte>& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::u32 index = 0; index < 4U; ++index)
    {
        bytes.at(offset + index) = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] Core::Status appendOggPage(std::vector<std::byte>& out, std::span<const std::byte> packet,
                                         Core::u8 flags, Core::u64 granule, Core::u32 sequence)
{
    if (packet.size() > 255U * 255U)
    {
        return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Opus packet exceeds one Ogg page");
    }
    std::array<Core::u8, 255> segments{};
    Core::u8 segmentCount = 0;
    Core::usize remaining = packet.size();
    while (remaining >= 255U)
    {
        segments[segmentCount++] = 255;
        remaining -= 255U;
    }
    segments[segmentCount++] = static_cast<Core::u8>(remaining);

    const Core::usize pageOffset = out.size();
    constexpr char magic[] = {'O', 'g', 'g', 'S'};
    appendBytes(out, std::as_bytes(std::span{magic}));
    appendU8(out, 0);
    appendU8(out, flags);
    appendU64(out, granule);
    appendU32(out, OggSerial);
    appendU32(out, sequence);
    const Core::usize crcOffset = out.size();
    appendU32(out, 0);
    appendU8(out, segmentCount);
    for (Core::u8 index = 0; index < segmentCount; ++index)
    {
        appendU8(out, segments[index]);
    }
    appendBytes(out, packet);
    const auto crc = Detail::oggPageCrc(std::span<const std::byte>{out.data() + pageOffset, out.size() - pageOffset});
    writeU32At(out, crcOffset, crc);
    return Core::success();
}

struct EncoderOwner final {
    OpusEncoder* encoder = nullptr;
    ~EncoderOwner()
    {
        if (encoder != nullptr)
        {
            opus_encoder_destroy(encoder);
        }
    }
};

[[nodiscard]] Core::u32 resolvedBitrate(Core::u32 channels, OpusEncodeConfig config) noexcept
{
    if (config.bitrateBps != 0)
    {
        return config.bitrateBps;
    }
    return channels == 1U ? DefaultMonoBitrate : DefaultStereoBitrate;
}

template <typename ReadPcm>
[[nodiscard]] Core::Result<std::vector<std::byte>>
encodeFrames(OpusEncoder& encoder, Core::u32 channels, Core::u16 preSkip, ReadPcm&& readPcm)
{
    std::vector<std::byte> ogg;
    std::array<std::byte, 19> head{};
    std::memcpy(head.data(), "OpusHead", 8);
    head[8] = std::byte{1};
    head[9] = static_cast<std::byte>(channels);
    head[10] = static_cast<std::byte>(preSkip & 0xFFU);
    head[11] = static_cast<std::byte>((preSkip >> 8U) & 0xFFU);
    const Core::u32 inputRate = OpusSampleRate;
    for (Core::u32 index = 0; index < 4U; ++index)
    {
        head[12 + index] = static_cast<std::byte>((inputRate >> (index * 8U)) & 0xFFU);
    }
    if (auto status = appendOggPage(ogg, head, 2, 0, 0); !status) { return Core::failure(std::move(status.error())); }

    std::vector<std::byte> tags;
    constexpr char vendor[] = "Tina";
    tags.insert(tags.end(), {std::byte{'O'}, std::byte{'p'}, std::byte{'u'}, std::byte{'s'},
                             std::byte{'T'}, std::byte{'a'}, std::byte{'g'}, std::byte{'s'}});
    appendU32(tags, 4);
    appendBytes(tags, std::as_bytes(std::span{vendor, 4}));
    appendU32(tags, 0);
    if (auto status = appendOggPage(ogg, tags, 0, 0, 1); !status) { return Core::failure(std::move(status.error())); }

    std::vector<float> frame(static_cast<Core::usize>(OpusFrameSamples) * channels, 0.0F);
    std::vector<float> remainder;
    remainder.reserve(frame.size());
    std::array<unsigned char, 4000> packet{};
    Core::u32 sequence = 2;
    Core::u64 granule = preSkip;
    bool anyAudio = false;
    bool emittedEos = false;
    for (;;)
    {
        auto read = readPcm(frame);
        if (!read) { return Core::failure(std::move(read.error())); }
        Core::u64 framesRead = *read;
        if (framesRead == 0 && remainder.empty())
        {
            break;
        }
        if (framesRead > 0)
        {
            remainder.insert(remainder.end(), frame.begin(),
                             frame.begin() + static_cast<std::ptrdiff_t>(framesRead * channels));
        }
        while (remainder.size() >= frame.size() || (framesRead == 0 && !remainder.empty()))
        {
            std::vector<float> opusFrame(frame.size(), 0.0F);
            const auto take = (std::min)(remainder.size(), frame.size());
            std::copy(remainder.begin(), remainder.begin() + static_cast<std::ptrdiff_t>(take), opusFrame.begin());
            remainder.erase(remainder.begin(), remainder.begin() + static_cast<std::ptrdiff_t>(take));
            const int packetBytes = opus_encode_float(&encoder, opusFrame.data(), OpusFrameSamples,
                                                      packet.data(), static_cast<int>(packet.size()));
            if (packetBytes < 0)
            {
                return Core::failure(AudioErrorCode::DecodeFailed, "Opus encoder failed");
            }
            granule += OpusFrameSamples;
            const bool last = framesRead == 0 && remainder.empty();
            if (auto status = appendOggPage(ogg,
                                            std::as_bytes(std::span{packet.data(), static_cast<Core::usize>(packetBytes)}),
                                            last ? Core::u8{4} : Core::u8{0}, granule, sequence++);
                !status)
            {
                return Core::failure(std::move(status.error()));
            }
            anyAudio = true;
            emittedEos = last;
            if (last) { break; }
        }
        if (framesRead == 0) { break; }
    }
    if (!anyAudio)
    {
        return Core::failure(AudioErrorCode::DecodeFailed, "Opus encoder produced no audio packets");
    }
    if (!emittedEos)
    {
        if (auto status = appendOggPage(ogg, {}, 4, granule, sequence); !status)
        {
            return Core::failure(std::move(status.error()));
        }
    }
    return ogg;
}

} // namespace

Core::Result<std::vector<std::byte>> encodeOpusOggFromDecoder(AudioDecoder& decoder,
                                                              OpusEncodeConfig config) noexcept
try
{
    const auto channels = decoder.channels();
    if (channels != 1U && channels != 2U)
    {
        return Core::failure(AudioErrorCode::NotSupported, "Opus encode requires mono or stereo");
    }
    if (decoder.sampleRate() != OpusSampleRate)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "Opus encode requires a 48000 Hz decoder output");
    }
    EncoderOwner owner;
    int opusError = OPUS_OK;
    owner.encoder = opus_encoder_create(static_cast<int>(OpusSampleRate), static_cast<int>(channels),
                                        OPUS_APPLICATION_AUDIO, &opusError);
    if (owner.encoder == nullptr || opusError != OPUS_OK)
    {
        return Core::failure(AudioErrorCode::ConstructionFailed, "Opus encoder could not be created");
    }
    const auto bitrate = resolvedBitrate(channels, config);
    if (opus_encoder_ctl(owner.encoder, OPUS_SET_BITRATE(static_cast<int>(bitrate))) != OPUS_OK)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "Opus bitrate is not supported");
    }
    int lookahead = 0;
    if (opus_encoder_ctl(owner.encoder, OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK || lookahead < 0)
    {
        lookahead = 384;
    }
    return encodeFrames(*owner.encoder, channels, static_cast<Core::u16>(lookahead),
                        [&decoder](std::span<float> out) { return decoder.readPcm(out); });
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Opus encode allocation failed");
}

Core::Result<std::vector<std::byte>> encodeOpusOgg(std::span<const float> interleavedPcm, Core::u32 channels,
                                                   Core::u32 sampleRate, OpusEncodeConfig config) noexcept
{
    if (interleavedPcm.empty() || channels == 0 || interleavedPcm.size() % channels != 0)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "Opus encode PCM geometry is invalid");
    }
    std::vector<std::byte> wav;
    // Reuse the decoder path: wrap PCM as a memory decode via a temporary WAV is heavier
    // than feeding frames directly. Build a tiny in-memory decoder by encoding through
    // a synthetic AudioDecoder is not available, so resample via decodeAudioMemory of WAV
    // is avoided. Direct frame feed:
    EncoderOwner owner;
    if (sampleRate != OpusSampleRate)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "encodeOpusOgg requires 48000 Hz PCM; resample with AudioDecoder first");
    }
    int opusError = OPUS_OK;
    owner.encoder = opus_encoder_create(static_cast<int>(OpusSampleRate), static_cast<int>(channels),
                                        OPUS_APPLICATION_AUDIO, &opusError);
    if (owner.encoder == nullptr || opusError != OPUS_OK)
    {
        return Core::failure(AudioErrorCode::ConstructionFailed, "Opus encoder could not be created");
    }
    const auto bitrate = resolvedBitrate(channels, config);
    if (opus_encoder_ctl(owner.encoder, OPUS_SET_BITRATE(static_cast<int>(bitrate))) != OPUS_OK)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "Opus bitrate is not supported");
    }
    int lookahead = 0;
    if (opus_encoder_ctl(owner.encoder, OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK || lookahead < 0)
    {
        lookahead = 384;
    }
    Core::u64 cursor = 0;
    const Core::u64 totalFrames = interleavedPcm.size() / channels;
    return encodeFrames(*owner.encoder, channels, static_cast<Core::u16>(lookahead),
                        [&](std::span<float> out) -> Core::Result<Core::u64> {
                            const Core::u64 want = out.size() / channels;
                            const Core::u64 left = totalFrames - cursor;
                            const Core::u64 take = (std::min)(want, left);
                            if (take == 0) { return Core::u64{0}; }
                            const auto samples = take * channels;
                            std::copy(interleavedPcm.begin() + static_cast<std::ptrdiff_t>(cursor * channels),
                                      interleavedPcm.begin() + static_cast<std::ptrdiff_t>(cursor * channels + samples),
                                      out.begin());
                            cursor += take;
                            return take;
                        });
}

} // namespace Tina::Audio
