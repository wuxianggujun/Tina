#include <tina/asset_format/LocalizationTablePayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/text/Utf8.hpp>

#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u64;
using Core::u8;
using Core::usize;

[[nodiscard]] u8 readU8(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::to_integer<u8>(bytes[offset]);
}

[[nodiscard]] u16 readU16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u16>(readU8(bytes, offset)) |
           static_cast<u16>(static_cast<u16>(readU8(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    u32 value = 0;
    for (usize index = 0; index < 4U; ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

[[nodiscard]] u64 readU64(std::span<const std::byte> bytes, usize offset) noexcept
{
    u64 value = 0;
    for (usize index = 0; index < 8U; ++index)
    {
        value |= static_cast<u64>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value) noexcept
{
    for (usize index = 0; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void writeU64(std::vector<std::byte>& bytes, usize offset, u64 value) noexcept
{
    for (usize index = 0; index < 8U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

// Structural offsets of the fixed header. Stated once so the writer and the parser cannot drift.
constexpr usize SchemaVersionOffset = 0U;
constexpr usize LocaleTagLengthOffset = 2U;
constexpr usize EntryCountOffset = 4U;
constexpr usize TextBytesOffset = 8U;
constexpr usize Reserved0Offset = 12U;
constexpr usize LocaleTagOffset = 16U;
constexpr usize Reserved1Offset = LocaleTagOffset + LocalizationTableWire::LocaleTagBytes;

constexpr usize EntryKeyHashOffset = 0U;
constexpr usize EntryTextOffsetOffset = 8U;
constexpr usize EntryTextLengthOffset = 12U;

static_assert(Reserved1Offset + 4U == LocalizationTableWire::HeaderBytes);
static_assert(EntryTextLengthOffset + 4U == LocalizationTableWire::EntryBytes);

// BCP-47 shape check. Deliberately structural rather than a registry lookup: the cooker cannot carry
// the IANA subtag registry, and an unregistered-but-well-formed tag is a content problem, not a wire
// problem. Rejecting non-ASCII here also keeps the field byte-comparable at runtime.
[[nodiscard]] bool isWellFormedLocaleTag(std::string_view tag) noexcept
{
    if (tag.empty() || tag.size() > LocalizationTableWire::MaximumLocaleTagBytes)
    {
        return false;
    }
    if (tag.front() == '-' || tag.back() == '-')
    {
        return false;
    }
    bool previousWasSeparator = false;
    for (const char character : tag)
    {
        const auto value = static_cast<unsigned char>(character);
        const bool isDigit = value >= '0' && value <= '9';
        const bool isLower = value >= 'a' && value <= 'z';
        const bool isUpper = value >= 'A' && value <= 'Z';
        if (character == '-')
        {
            // An empty subtag ("zh--CN") is not a well-formed tag, and allowing it would let two
            // spellings of the same locale cook to different bytes.
            if (previousWasSeparator)
            {
                return false;
            }
            previousWasSeparator = true;
            continue;
        }
        if (!isDigit && !isLower && !isUpper)
        {
            return false;
        }
        previousWasSeparator = false;
    }
    return true;
}

struct TableLayout final {
    u32 entryCount = 0;
    u32 textBytes = 0;
};

// Shared by the writer and the parser so a payload can never satisfy one and not the other. The
// parser passes the decoded ranges back in as a desc, which is why this validates offsets rather
// than assuming the writer's own packing.
[[nodiscard]] Core::Result<TableLayout> validateLayout(const LocalizationTablePayloadDesc& desc)
{
    if (!isWellFormedLocaleTag(desc.localeTag))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "localization locale tag is not a well-formed BCP-47 tag");
    }
    if (desc.entries.empty() || desc.entries.size() > LocalizationTableWire::MaximumEntryCount)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "localization entry count is outside the schema limits");
    }

    u32 textBytes = 0;
    u64 previousKeyHash = 0;
    bool hasPrevious = false;
    for (const LocalizationTableEntryDesc& entry : desc.entries)
    {
        if (entry.key.empty() || entry.key.size() > LocalizationTableWire::MaximumKeyBytes ||
            !Core::isStrictUtf8WithoutNul(entry.key))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "localization key must be non-empty UTF-8 without NUL");
        }
        if (entry.text.size() > LocalizationTableWire::MaximumValueBytes)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "localization value exceeds the per-entry byte limit");
        }
        if (!entry.text.empty() && !Core::isStrictUtf8WithoutNul(entry.text))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "localization value must be UTF-8 without NUL");
        }
        const u64 keyHash = localizationKeyHash(entry.key);
        if (hasPrevious && keyHash <= previousKeyHash)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "localization entries must be strictly ascending by key hash");
        }
        previousKeyHash = keyHash;
        hasPrevious = true;

        if (entry.text.size() > LocalizationTableWire::MaximumTextBytes - textBytes)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "localization text blob exceeds the aggregate byte limit");
        }
        textBytes += static_cast<u32>(entry.text.size());
    }

    return TableLayout{
        .entryCount = static_cast<u32>(desc.entries.size()),
        .textBytes = textBytes,
    };
}

[[nodiscard]] usize entryRecordOffset(u32 index) noexcept
{
    return LocalizationTableWire::HeaderBytes + static_cast<usize>(index) * LocalizationTableWire::EntryBytes;
}

[[nodiscard]] LocalizationTableEntryView decodeEntry(std::span<const std::byte> entriesBytes,
                                                    u32 index) noexcept
{
    const usize offset = static_cast<usize>(index) * LocalizationTableWire::EntryBytes;
    return LocalizationTableEntryView{
        .keyHash = readU64(entriesBytes, offset + EntryKeyHashOffset),
        .textOffset = readU32(entriesBytes, offset + EntryTextOffsetOffset),
        .textLength = readU32(entriesBytes, offset + EntryTextLengthOffset),
    };
}

// The wire-form invariant enforcer, run by the writer over the bytes it just produced and by the
// parser over bytes it did not produce. validateLayout above cannot serve both sides: the key text
// is not a wire field, so a parser has no desc to hand back. This is the check that both sides do
// share, and it is the stronger of the two -- it validates the encoded offsets rather than trusting
// any particular packing.
[[nodiscard]] Core::Status validateEncodedTable(std::string_view localeTag, u32 entryCount, u32 textBytes,
                                                std::span<const std::byte> entriesBytes,
                                                std::string_view textBlob)
{
    if (!isWellFormedLocaleTag(localeTag))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "localization locale tag is not a well-formed BCP-47 tag");
    }
    if (entryCount == 0U || entryCount > LocalizationTableWire::MaximumEntryCount ||
        textBytes > LocalizationTableWire::MaximumTextBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "localization table counts are outside the schema limits");
    }
    if (entriesBytes.size() != static_cast<usize>(entryCount) * LocalizationTableWire::EntryBytes ||
        textBlob.size() != textBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "localization block sizes do not match the header counts");
    }

    u64 previousKeyHash = 0;
    for (u32 index = 0; index < entryCount; ++index)
    {
        const LocalizationTableEntryView entry = decodeEntry(entriesBytes, index);
        if (index > 0U && entry.keyHash <= previousKeyHash)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "localization entries must be strictly ascending by key hash");
        }
        previousKeyHash = entry.keyHash;

        if (entry.textLength > LocalizationTableWire::MaximumValueBytes)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "localization value exceeds the per-entry byte limit");
        }
        // Subtraction rather than addition: textOffset + textLength can wrap u32, and a wrapped sum
        // would compare as in-bounds.
        if (entry.textOffset > textBytes || entry.textLength > textBytes - entry.textOffset)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "localization value range escapes the text blob");
        }
        const std::string_view text = textBlob.substr(entry.textOffset, entry.textLength);
        if (!text.empty() && !Core::isStrictUtf8WithoutNul(text))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "localization value must be UTF-8 without NUL");
        }
    }
    return Core::success();
}

} // namespace

std::optional<LocalizationTableEntryView> LocalizationTablePayloadView::entry(Core::u32 index) const noexcept
{
    if (index >= entryCount)
    {
        return std::nullopt;
    }
    const usize offset = static_cast<usize>(index) * LocalizationTableWire::EntryBytes;
    if (offset + LocalizationTableWire::EntryBytes > entriesBytes.size())
    {
        return std::nullopt;
    }
    return decodeEntry(entriesBytes, index);
}

std::string_view LocalizationTablePayloadView::text(Core::u32 index) const noexcept
{
    const auto entryView = entry(index);
    if (!entryView || entryView->textOffset > textBlob.size() ||
        entryView->textLength > textBlob.size() - entryView->textOffset)
    {
        return {};
    }
    return textBlob.substr(entryView->textOffset, entryView->textLength);
}

std::optional<std::string_view> LocalizationTablePayloadView::find(Core::u64 keyHash) const noexcept
{
    u32 low = 0;
    u32 high = entryCount;
    while (low < high)
    {
        const u32 middle = low + ((high - low) / 2U);
        const auto entryView = entry(middle);
        if (!entryView)
        {
            return std::nullopt;
        }
        if (entryView->keyHash == keyHash)
        {
            return text(middle);
        }
        if (entryView->keyHash < keyHash)
        {
            low = middle + 1U;
        } else
        {
            high = middle;
        }
    }
    return std::nullopt;
}

Core::Result<std::vector<std::byte>>
writeLocalizationTablePayloadBytes(const LocalizationTablePayloadDesc& desc)
{
    auto layout = validateLayout(desc);
    if (!layout)
    {
        return Core::failure(std::move(layout.error()));
    }

    const usize entryBlockBytes = static_cast<usize>(layout->entryCount) * LocalizationTableWire::EntryBytes;
    const usize payloadBytes = LocalizationTableWire::HeaderBytes + entryBlockBytes + layout->textBytes;
    if (payloadBytes > Wire::MaxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "localization payload exceeds the cooked payload limit");
    }

    try
    {
        std::vector<std::byte> bytes(payloadBytes, std::byte{0});
        writeU16(bytes, SchemaVersionOffset, LocalizationTableWire::SchemaVersion);
        writeU16(bytes, LocaleTagLengthOffset, static_cast<u16>(desc.localeTag.size()));
        writeU32(bytes, EntryCountOffset, layout->entryCount);
        writeU32(bytes, TextBytesOffset, layout->textBytes);
        writeU32(bytes, Reserved0Offset, 0U);
        std::memcpy(bytes.data() + LocaleTagOffset, desc.localeTag.data(), desc.localeTag.size());
        // Bytes from localeTag.size() to LocaleTagBytes stay zero from the vector fill, which is the
        // NUL padding the parser requires.
        writeU32(bytes, Reserved1Offset, 0U);

        const usize textBase = LocalizationTableWire::HeaderBytes + entryBlockBytes;
        u32 textOffset = 0;
        for (u32 index = 0; index < layout->entryCount; ++index)
        {
            const LocalizationTableEntryDesc& entryDesc = desc.entries[index];
            const usize recordOffset = entryRecordOffset(index);
            writeU64(bytes, recordOffset + EntryKeyHashOffset, localizationKeyHash(entryDesc.key));
            writeU32(bytes, recordOffset + EntryTextOffsetOffset, textOffset);
            writeU32(bytes, recordOffset + EntryTextLengthOffset, static_cast<u32>(entryDesc.text.size()));
            if (!entryDesc.text.empty())
            {
                std::memcpy(bytes.data() + textBase + textOffset, entryDesc.text.data(), entryDesc.text.size());
            }
            textOffset += static_cast<u32>(entryDesc.text.size());
        }

        const std::span<const std::byte> entriesBytes{bytes.data() + LocalizationTableWire::HeaderBytes,
                                                      entryBlockBytes};
        const std::string_view textBlob{reinterpret_cast<const char*>(bytes.data() + textBase),
                                        layout->textBytes};
        if (Core::Status status = validateEncodedTable(desc.localeTag, layout->entryCount, layout->textBytes,
                                                       entriesBytes, textBlob);
            !status)
        {
            return Core::failure(std::move(status.error()));
        }
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "localization payload allocation failed");
    }
}

Core::Result<LocalizationTablePayloadView>
parseLocalizationTablePayload(std::span<const std::byte> payload)
{
    if (payload.size() < LocalizationTableWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "localization payload is smaller than its header");
    }
    LocalizationTablePayloadView view{
        .schemaVersion = readU16(payload, SchemaVersionOffset),
        .entryCount = readU32(payload, EntryCountOffset),
        .textBytes = readU32(payload, TextBytesOffset),
    };
    if (view.schemaVersion != LocalizationTableWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported localization table payload schema");
    }
    if (readU32(payload, Reserved0Offset) != 0U || readU32(payload, Reserved1Offset) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "localization reserved fields must be zero");
    }

    const u16 localeTagLength = readU16(payload, LocaleTagLengthOffset);
    if (localeTagLength == 0U || localeTagLength > LocalizationTableWire::MaximumLocaleTagBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "localization locale tag length is outside the wire field");
    }
    // The pad is part of the canonical encoding: leaving it unchecked would let two byte sequences
    // decode to the same table, which content-hash determinism forbids.
    for (u32 index = localeTagLength; index < LocalizationTableWire::LocaleTagBytes; ++index)
    {
        if (readU8(payload, LocaleTagOffset + index) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "localization locale tag padding must be zero");
        }
    }
    view.localeTag = std::string_view{reinterpret_cast<const char*>(payload.data() + LocaleTagOffset),
                                      localeTagLength};

    if (view.entryCount == 0U || view.entryCount > LocalizationTableWire::MaximumEntryCount ||
        view.textBytes > LocalizationTableWire::MaximumTextBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "localization table counts are outside the schema limits");
    }
    const usize entryBlockBytes = static_cast<usize>(view.entryCount) * LocalizationTableWire::EntryBytes;
    const usize expectedBytes = LocalizationTableWire::HeaderBytes + entryBlockBytes + view.textBytes;
    if (payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "localization payload byte count is inconsistent");
    }

    view.entriesBytes = payload.subspan(LocalizationTableWire::HeaderBytes, entryBlockBytes);
    const usize textBase = LocalizationTableWire::HeaderBytes + entryBlockBytes;
    view.textBlob = std::string_view{reinterpret_cast<const char*>(payload.data() + textBase),
                                     view.textBytes};

    if (Core::Status status = validateEncodedTable(view.localeTag, view.entryCount, view.textBytes,
                                                   view.entriesBytes, view.textBlob);
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return view;
}

Core::Result<std::vector<std::byte>>
writeCookedLocalizationTableAsset(Core::AssetId assetId, const LocalizationTablePayloadDesc& desc,
                                  TargetPlatform platform)
{
    auto payload = writeLocalizationTablePayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::LocalizationTable,
        .assetTypeVersion = LocalizationTableWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
