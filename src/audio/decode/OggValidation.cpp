#include "OggValidation.hpp"

#include <tina/audio/AudioErrors.hpp>

#include <array>
#include <cstring>

namespace Tina::Audio::Detail {
namespace {

constexpr Core::usize PageHeaderBytes = 27;
constexpr Core::u8 ContinuedPacket = 1;
constexpr Core::u8 BeginStream = 2;
constexpr Core::u8 EndStream = 4;

constexpr auto CrcTable = [] {
    std::array<Core::u32, 256> table{};
    for (Core::u32 index = 0; index < table.size(); ++index)
    {
        auto value = index << 24U;
        for (int bit = 0; bit < 8; ++bit)
        {
            value = (value << 1U) ^ ((value & 0x80000000U) != 0 ? 0x04c11db7U : 0U);
        }
        table[index] = value;
    }
    return table;
}();

Core::u32 readU32(std::span<const std::byte> bytes, Core::usize offset) noexcept
{
    Core::u32 value = 0;
    for (Core::u32 byte = 0; byte < 4; ++byte)
    {
        value |= static_cast<Core::u32>(std::to_integer<Core::u8>(bytes[offset + byte])) << (8U * byte);
    }
    return value;
}

Core::u32 pageCrc(std::span<const std::byte> page) noexcept
{
    Core::u32 crc = 0;
    for (Core::usize index = 0; index < page.size(); ++index)
    {
        // The checksum field is zero while computing the Ogg CRC.
        const auto byte = index >= 22 && index < 26 ? Core::u8{0} : std::to_integer<Core::u8>(page[index]);
        crc = (crc << 8U) ^ CrcTable[((crc >> 24U) ^ byte) & 0xffU];
    }
    return crc;
}

} // namespace

Core::Result<OggAudioCodec> validateOggAudio(std::span<const std::byte> encoded) noexcept
{
    Core::usize offset = 0;
    Core::u32 serial = 0;
    Core::u32 sequence = 0;
    bool continuation = false;
    bool ended = false;
    OggAudioCodec codec{};
    while (offset < encoded.size())
    {
        const auto remaining = encoded.subspan(offset);
        if (ended)
        {
            return Core::failure(AudioErrorCode::NotSupported, "Chained Ogg streams are not supported");
        }
        if (remaining.size() < PageHeaderBytes ||
            std::memcmp(remaining.data(), "OggS", 4) != 0 || remaining[4] != std::byte{0})
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "Ogg page is truncated, chained or invalid");
        }
        const auto flags = std::to_integer<Core::u8>(remaining[5]);
        const auto segments = std::to_integer<Core::u8>(remaining[26]);
        const Core::usize headerBytes = PageHeaderBytes + segments;
        if ((flags & ~Core::u8{7}) != 0 || remaining.size() < headerBytes ||
            ((flags & ContinuedPacket) != 0) != continuation)
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "Ogg page flags or packet continuation are invalid");
        }
        Core::usize bodyBytes = 0;
        for (Core::usize segment = 0; segment < segments; ++segment)
        {
            bodyBytes += std::to_integer<Core::u8>(remaining[PageHeaderBytes + segment]);
        }
        if (bodyBytes > remaining.size() - headerBytes)
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "Ogg page body is truncated");
        }
        const auto page = remaining.first(headerBytes + bodyBytes);
        if (readU32(page, 22) != pageCrc(page))
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "Ogg page checksum mismatch");
        }
        if (offset == 0)
        {
            if (flags != BeginStream || segments == 0 || readU32(page, 18) != 0)
            {
                return Core::failure(AudioErrorCode::DecodeFailed, "Ogg identification page is invalid");
            }
            const auto firstPacketBytes = std::to_integer<Core::u8>(page[PageHeaderBytes]);
            const auto body = page.subspan(headerBytes);
            if (firstPacketBytes >= 30 && body.size() >= 30 && body[0] == std::byte{1} &&
                std::memcmp(body.data() + 1, "vorbis", 6) == 0)
            {
                codec = OggAudioCodec::Vorbis;
            }
            else if (firstPacketBytes >= 19 && body.size() >= 19 &&
                     std::memcmp(body.data(), "OpusHead", 8) == 0)
            {
                codec = OggAudioCodec::Opus;
            }
            else
            {
                return Core::failure(AudioErrorCode::NotSupported, "Ogg stream is not Vorbis or Opus audio");
            }
            serial = readU32(page, 14);
        }
        else if ((flags & BeginStream) != 0 || readU32(page, 14) != serial)
        {
            return Core::failure(AudioErrorCode::NotSupported, "Chained or multiplexed Ogg streams are not supported");
        }
        if (readU32(page, 18) != sequence++)
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "Ogg page sequence has a gap");
        }
        if (segments != 0)
        {
            continuation = page[headerBytes - 1] == std::byte{255};
        }
        ended = (flags & EndStream) != 0;
        if (ended && continuation)
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "Ogg ends inside an incomplete packet");
        }
        offset += page.size();
    }
    if (!ended)
    {
        return Core::failure(AudioErrorCode::DecodeFailed, "Ogg stream has no complete end page");
    }
    return codec;
}

} // namespace Tina::Audio::Detail
