#include "SaveFormat.hpp"

#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/save/SaveErrors.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <limits>
#include <new>
#include <string>
#include <type_traits>

namespace Tina::Save::Detail {
namespace {

constexpr std::array<std::byte, 8> SaveMagic{
    std::byte{'T'}, std::byte{'I'}, std::byte{'N'}, std::byte{'A'},
    std::byte{'S'}, std::byte{'A'}, std::byte{'V'}, std::byte{'E'},
};

template <std::unsigned_integral Value>
void appendLittleEndian(std::vector<std::byte>& bytes, Value value)
{
    for (Core::usize index = 0; index < sizeof(Value); ++index)
    {
        bytes.push_back(static_cast<std::byte>(value & static_cast<Value>(0xFFU)));
        value >>= 8U;
    }
}

template <std::unsigned_integral Value>
[[nodiscard]] bool readLittleEndian(
    std::span<const std::byte> bytes, Core::usize& offset, Value& value) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(Value))
    {
        return false;
    }
    Value decoded = 0;
    for (Core::usize index = 0; index < sizeof(Value); ++index)
    {
        decoded |= static_cast<Value>(std::to_integer<unsigned int>(bytes[offset + index]))
                   << (index * 8U);
    }
    offset += sizeof(Value);
    value = decoded;
    return true;
}

void appendStringBytes(std::vector<std::byte>& bytes, std::string_view text)
{
    const auto raw = std::as_bytes(std::span<const char>{text.data(), text.size()});
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

[[nodiscard]] Core::Result<Core::usize> checkedEncodedSize(
    Core::usize gameIdBytes,
    Core::usize displayNameBytes,
    Core::usize payloadBytes)
{
    constexpr Core::usize FixedBytes = SaveWireHeaderBytes + SaveWireDigestBytes;
    if (gameIdBytes > (std::numeric_limits<Core::usize>::max)() - FixedBytes)
    {
        return Core::failure(SaveErrorCode::PayloadTooLarge, "save envelope size overflowed");
    }
    Core::usize total = FixedBytes + gameIdBytes;
    if (displayNameBytes > (std::numeric_limits<Core::usize>::max)() - total)
    {
        return Core::failure(SaveErrorCode::PayloadTooLarge, "save envelope size overflowed");
    }
    total += displayNameBytes;
    if (payloadBytes > (std::numeric_limits<Core::usize>::max)() - total)
    {
        return Core::failure(SaveErrorCode::PayloadTooLarge, "save envelope size overflowed");
    }
    return total + payloadBytes;
}

} // namespace

Core::Result<std::vector<std::byte>> encodeSaveFile(
    const SaveSlotMetadata& metadata,
    std::span<const std::byte> payload)
try
{
    if (metadata.dataVersion == 0 || metadata.revision == 0)
    {
        return Core::failure(SaveErrorCode::InvalidMetadata,
                             "save metadata requires non-zero dataVersion and revision");
    }
    if (metadata.gameId.empty() || metadata.gameId.size() > MaxSaveGameIdBytes ||
        !Core::isStrictUtf8WithoutNul(metadata.gameId))
    {
        return Core::failure(SaveErrorCode::InvalidMetadata,
                             "save gameId must be bounded strict UTF-8 without NUL");
    }
    if (metadata.displayName.size() > MaxSaveDisplayNameBytes ||
        !Core::isStrictUtf8WithoutNul(metadata.displayName))
    {
        return Core::failure(SaveErrorCode::InvalidMetadata,
                             "save displayName must be bounded strict UTF-8 without NUL");
    }
    if (metadata.payloadBytes != static_cast<Core::u64>(payload.size()))
    {
        return Core::failure(SaveErrorCode::InvalidMetadata,
                             "save metadata payload size does not match the provided payload");
    }
    if (metadata.gameId.size() > (std::numeric_limits<Core::u32>::max)() ||
        metadata.displayName.size() > (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(SaveErrorCode::InvalidMetadata,
                             "save metadata text exceeds the wire representation");
    }

    auto totalSize = checkedEncodedSize(metadata.gameId.size(), metadata.displayName.size(), payload.size());
    if (!totalSize)
    {
        return Core::failure(std::move(totalSize.error()));
    }

    std::vector<std::byte> bytes;
    bytes.reserve(*totalSize);
    bytes.insert(bytes.end(), SaveMagic.begin(), SaveMagic.end());
    appendLittleEndian(bytes, SaveWireSchemaVersion);
    appendLittleEndian(bytes, SaveWireHeaderBytes);
    appendLittleEndian(bytes, metadata.slot.value);
    appendLittleEndian(bytes, metadata.dataVersion);
    appendLittleEndian(bytes, metadata.revision);
    appendLittleEndian(bytes, metadata.savedAtUnixMilliseconds);
    appendLittleEndian(bytes, metadata.playTimeMilliseconds);
    appendLittleEndian(bytes, static_cast<Core::u32>(metadata.gameId.size()));
    appendLittleEndian(bytes, static_cast<Core::u32>(metadata.displayName.size()));
    appendLittleEndian(bytes, metadata.payloadBytes);
    appendLittleEndian(bytes, static_cast<Core::u8>(Core::ContentHashAlgorithm::Xxh3_128V1));
    appendLittleEndian(bytes, Core::u8{0});
    appendLittleEndian(bytes, Core::u16{0});

    if (bytes.size() != SaveWireHeaderBytes)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "save wire encoder produced an invalid fixed header size");
    }
    appendStringBytes(bytes, metadata.gameId);
    appendStringBytes(bytes, metadata.displayName);
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    auto digest = Core::digestContentHashV1(bytes);
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext("encodeSaveFile", "digest"));
    }
    const auto& digestBytes = digest->bytes();
    bytes.insert(bytes.end(), digestBytes.begin(), digestBytes.end());
    return bytes;
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "save envelope allocation failed");
}

Core::Result<ParsedSaveFile> parseSaveFile(
    std::span<const std::byte> bytes,
    SaveSlotId expectedSlot,
    std::string_view expectedGameId,
    Core::u64 maxPayloadBytes)
try
{
    if (bytes.size() < SaveWireHeaderBytes + SaveWireDigestBytes)
    {
        return Core::failure(SaveErrorCode::CorruptData, "save file is shorter than its fixed envelope");
    }
    if (!std::equal(SaveMagic.begin(), SaveMagic.end(), bytes.begin()))
    {
        return Core::failure(SaveErrorCode::CorruptData, "save file magic does not match TINASAVE");
    }

    Core::usize offset = SaveMagic.size();
    Core::u16 schemaVersion = 0;
    Core::u16 headerBytes = 0;
    Core::u32 slot = 0;
    Core::u32 dataVersion = 0;
    Core::u64 revision = 0;
    Core::u64 savedAtUnixMilliseconds = 0;
    Core::u64 playTimeMilliseconds = 0;
    Core::u32 gameIdBytes = 0;
    Core::u32 displayNameBytes = 0;
    Core::u64 payloadBytes = 0;
    Core::u8 hashAlgorithm = 0;
    Core::u8 flags = 0;
    Core::u16 reserved = 0;

    if (!readLittleEndian(bytes, offset, schemaVersion) ||
        !readLittleEndian(bytes, offset, headerBytes) ||
        !readLittleEndian(bytes, offset, slot) ||
        !readLittleEndian(bytes, offset, dataVersion) ||
        !readLittleEndian(bytes, offset, revision) ||
        !readLittleEndian(bytes, offset, savedAtUnixMilliseconds) ||
        !readLittleEndian(bytes, offset, playTimeMilliseconds) ||
        !readLittleEndian(bytes, offset, gameIdBytes) ||
        !readLittleEndian(bytes, offset, displayNameBytes) ||
        !readLittleEndian(bytes, offset, payloadBytes) ||
        !readLittleEndian(bytes, offset, hashAlgorithm) ||
        !readLittleEndian(bytes, offset, flags) ||
        !readLittleEndian(bytes, offset, reserved))
    {
        return Core::failure(SaveErrorCode::CorruptData, "save file header is truncated");
    }

    if (schemaVersion != SaveWireSchemaVersion)
    {
        return Core::failure(SaveErrorCode::UnsupportedSchema,
                             "save file uses an unsupported wire schema");
    }
    if (headerBytes != SaveWireHeaderBytes || offset != SaveWireHeaderBytes)
    {
        return Core::failure(SaveErrorCode::CorruptData, "save file declares an invalid header size");
    }
    if (hashAlgorithm != static_cast<Core::u8>(Core::ContentHashAlgorithm::Xxh3_128V1) ||
        flags != 0 || reserved != 0)
    {
        return Core::failure(SaveErrorCode::UnsupportedSchema,
                             "save file requires unsupported envelope features");
    }
    if (slot != expectedSlot.value)
    {
        return Core::failure(SaveErrorCode::CorruptData,
                             "save file slot does not match its generated filename");
    }
    if (dataVersion == 0 || revision == 0)
    {
        return Core::failure(SaveErrorCode::CorruptData,
                             "save file has zero dataVersion or revision");
    }
    if (gameIdBytes == 0 || gameIdBytes > MaxSaveGameIdBytes ||
        displayNameBytes > MaxSaveDisplayNameBytes)
    {
        return Core::failure(SaveErrorCode::CorruptData,
                             "save file metadata lengths exceed the format limits");
    }
    if (payloadBytes > maxPayloadBytes || payloadBytes > MaxSavePayloadBytes ||
        payloadBytes > static_cast<Core::u64>((std::numeric_limits<Core::usize>::max)()))
    {
        return Core::failure(SaveErrorCode::PayloadTooLarge,
                             "save file payload exceeds the configured limit");
    }

    auto expectedSize = checkedEncodedSize(
        static_cast<Core::usize>(gameIdBytes),
        static_cast<Core::usize>(displayNameBytes),
        static_cast<Core::usize>(payloadBytes));
    if (!expectedSize || *expectedSize != bytes.size())
    {
        return Core::failure(SaveErrorCode::CorruptData,
                             "save file length does not match its envelope lengths");
    }

    const auto dataEnd = bytes.size() - SaveWireDigestBytes;
    auto digest = Core::digestContentHashV1(bytes.first(dataEnd));
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext("parseSaveFile", "digest"));
    }
    if (!std::equal(digest->bytes().begin(), digest->bytes().end(), bytes.begin() + dataEnd))
    {
        return Core::failure(SaveErrorCode::CorruptData, "save file content hash does not match");
    }

    const char* text = reinterpret_cast<const char*>(bytes.data() + SaveWireHeaderBytes);
    std::string gameId{text, static_cast<Core::usize>(gameIdBytes)};
    std::string displayName{
        text + gameIdBytes, static_cast<Core::usize>(displayNameBytes)};
    if (!Core::isStrictUtf8WithoutNul(gameId) || !Core::isStrictUtf8WithoutNul(displayName))
    {
        return Core::failure(SaveErrorCode::CorruptData,
                             "save file metadata is not strict UTF-8 without NUL");
    }
    if (gameId != expectedGameId)
    {
        return Core::failure(SaveErrorCode::WrongGameId,
                             "save file belongs to a different gameId");
    }

    return ParsedSaveFile{
        .metadata = SaveSlotMetadata{
            .slot = SaveSlotId{slot},
            .dataVersion = dataVersion,
            .revision = revision,
            .savedAtUnixMilliseconds = savedAtUnixMilliseconds,
            .playTimeMilliseconds = playTimeMilliseconds,
            .gameId = std::move(gameId),
            .displayName = std::move(displayName),
            .payloadBytes = payloadBytes,
        },
        .payloadOffset = SaveWireHeaderBytes + static_cast<Core::usize>(gameIdBytes) +
                         static_cast<Core::usize>(displayNameBytes),
    };
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "save metadata allocation failed");
}

} // namespace Tina::Save::Detail
