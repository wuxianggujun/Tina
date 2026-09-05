#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/LocalizationTablePayload.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::AssetFormat {
namespace {

using Core::u32;
using Core::u64;
using Core::usize;

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::AssetId::fromBytes(bytes);
}

// The schema demands ascending key-hash order, and FNV-1a order has nothing to do with
// alphabetical order, so tests must sort rather than hand-order their entries.
[[nodiscard]] std::vector<LocalizationTableEntryDesc> sortedEntries(
    std::vector<LocalizationTableEntryDesc> entries)
{
    std::ranges::sort(entries, [](const LocalizationTableEntryDesc& left,
                                  const LocalizationTableEntryDesc& right) {
        return localizationKeyHash(left.key) < localizationKeyHash(right.key);
    });
    return entries;
}

// Byte-escaped rather than written literally: this is a wire-format test, and the exact UTF-8 bytes
// are the thing under assertion. "\xE5\xBC\x80\xE5\xA7\x8B" is U+5F00 U+59CB.
constexpr std::string_view MultiByteText = "\xE5\xBC\x80\xE5\xA7\x8B";

[[nodiscard]] std::vector<LocalizationTableEntryDesc> validEntries()
{
    std::vector<LocalizationTableEntryDesc> entries{
        LocalizationTableEntryDesc{.key = "ui.menu.play", .text = MultiByteText},
        LocalizationTableEntryDesc{.key = "ui.menu.quit", .text = "Quit"},
        LocalizationTableEntryDesc{.key = "ui.menu.options", .text = ""},
        LocalizationTableEntryDesc{.key = "hud.score", .text = "Score: {0}"},
    };
    return sortedEntries(std::move(entries));
}

[[nodiscard]] LocalizationTablePayloadDesc validDesc(
    const std::vector<LocalizationTableEntryDesc>& entries) noexcept
{
    return {.localeTag = "zh-CN", .entries = entries};
}

void putU16(std::vector<std::byte>& bytes, usize offset, Core::u16 value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void putU32(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    for (usize index = 0; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void putU64(std::vector<std::byte>& bytes, usize offset, u64 value)
{
    for (usize index = 0; index < 8U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] usize entryOffset(u32 index) noexcept
{
    return LocalizationTableWire::HeaderBytes + static_cast<usize>(index) * LocalizationTableWire::EntryBytes;
}

void expectParseError(const std::vector<std::byte>& payload, Core::ErrorCode expected)
{
    const auto result = parseLocalizationTablePayload(payload);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, expected) << result.error().message;
}

void expectWriteError(const LocalizationTablePayloadDesc& desc, Core::ErrorCode expected,
                      std::string_view label = {})
{
    const auto result = writeLocalizationTablePayloadBytes(desc);
    ASSERT_FALSE(result.has_value()) << label;
    EXPECT_EQ(result.error().code, expected) << label << ' ' << result.error().message;
}

TEST(LocalizationTablePayloadTests, RoundTripsSortedTableAndCookedIdentity)
{
    const auto entries = validEntries();
    auto payload = writeLocalizationTablePayloadBytes(validDesc(entries));
    ASSERT_TRUE(payload) << payload.error().message;

    usize expectedTextBytes = 0;
    for (const auto& entry : entries)
    {
        expectedTextBytes += entry.text.size();
    }
    ASSERT_EQ(payload->size(), LocalizationTableWire::HeaderBytes +
                                   entries.size() * LocalizationTableWire::EntryBytes +
                                   expectedTextBytes);

    auto view = parseLocalizationTablePayload(*payload);
    ASSERT_TRUE(view) << view.error().message;
    EXPECT_EQ(view->schemaVersion, LocalizationTableWire::SchemaVersion);
    EXPECT_EQ(view->localeTag, "zh-CN");
    EXPECT_EQ(view->entryCount, static_cast<u32>(entries.size()));
    EXPECT_EQ(view->textBytes, static_cast<u32>(expectedTextBytes));

    for (u32 index = 0; index < view->entryCount; ++index)
    {
        const auto entry = view->entry(index);
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->keyHash, localizationKeyHash(entries[index].key));
        EXPECT_EQ(view->text(index), entries[index].text);
        const auto found = view->find(entry->keyHash);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, entries[index].text);
    }
    // An absent key is distinguishable from the empty value "ui.menu.options" carries.
    EXPECT_FALSE(view->find(localizationKeyHash("ui.menu.absent")).has_value());
    EXPECT_TRUE(view->find(localizationKeyHash("ui.menu.options")).has_value());
    EXPECT_TRUE(view->find(localizationKeyHash("ui.menu.options"))->empty());
    EXPECT_FALSE(view->entry(view->entryCount).has_value());
    EXPECT_TRUE(view->text(view->entryCount).empty());

    const Core::AssetId id = assetId(19U);
    auto cooked = writeCookedLocalizationTableAsset(id, validDesc(entries));
    ASSERT_TRUE(cooked) << cooked.error().message;
    auto file = parseCookedAssetView(*cooked);
    ASSERT_TRUE(file) << file.error().message;
    EXPECT_EQ(file->header().assetKind, AssetKind::LocalizationTable);
    EXPECT_EQ(file->header().assetTypeVersion, LocalizationTableWire::SchemaVersion);
    EXPECT_EQ(file->header().assetId, id);
    EXPECT_EQ(file->header().dependencyCount, 0U);
    EXPECT_TRUE(verifyCookedAssetContentHash(*file));

    // The cooked kind must survive the range check every reader funnels through; a stale upper
    // bound rejects the payload here rather than at compile time.
    auto artifactPath = makeCookedArtifactPath(AssetKind::LocalizationTable, id);
    ASSERT_TRUE(artifactPath) << artifactPath.error().message;
}

TEST(LocalizationTablePayloadTests, WriterRejectsUnsortedAndDuplicateKeys)
{
    auto entries = validEntries();
    std::ranges::reverse(entries);
    expectWriteError(validDesc(entries), AssetFormatErrorCode::InvalidLayout, "reversed order");

    const std::vector<LocalizationTableEntryDesc> duplicated{
        {.key = "ui.menu.play", .text = "A"},
        {.key = "ui.menu.play", .text = "B"},
    };
    expectWriteError(validDesc(duplicated), AssetFormatErrorCode::InvalidLayout, "duplicate key");
}

TEST(LocalizationTablePayloadTests, WriterRejectsInvalidKeysValuesAndLocaleTags)
{
    const auto entries = validEntries();
    auto desc = validDesc(entries);

    desc.entries = {};
    expectWriteError(desc, AssetFormatErrorCode::SizeLimitExceeded, "empty table");

    const std::array<std::string_view, 6> badTags{
        "", "-zh", "zh-", "zh_CN", "zh--CN", "zh-\xC4\x80",
    };
    for (const std::string_view tag : badTags)
    {
        expectWriteError({.localeTag = tag, .entries = entries}, AssetFormatErrorCode::InvalidLayout, tag);
    }
    const std::string overlongTag(LocalizationTableWire::MaximumLocaleTagBytes + 1U, 'a');
    expectWriteError({.localeTag = overlongTag, .entries = entries}, AssetFormatErrorCode::InvalidLayout,
                     "overlong tag");
    // The advertised maximum must itself be usable.
    const std::string maximumTag(LocalizationTableWire::MaximumLocaleTagBytes, 'a');
    EXPECT_TRUE(writeLocalizationTablePayloadBytes({.localeTag = maximumTag, .entries = entries}));

    const std::vector<LocalizationTableEntryDesc> emptyKey{{.key = "", .text = "value"}};
    expectWriteError(validDesc(emptyKey), AssetFormatErrorCode::InvalidLayout, "empty key");

    const std::vector<LocalizationTableEntryDesc> badKeyUtf8{
        {.key = std::string_view{"bad\xFFkey", 7U}, .text = "value"},
    };
    expectWriteError(validDesc(badKeyUtf8), AssetFormatErrorCode::InvalidLayout, "bad key utf8");

    const std::string overlongKey(LocalizationTableWire::MaximumKeyBytes + 1U, 'k');
    const std::vector<LocalizationTableEntryDesc> longKey{{.key = overlongKey, .text = "value"}};
    expectWriteError(validDesc(longKey), AssetFormatErrorCode::InvalidLayout, "overlong key");

    // A truncated multi-byte sequence and an embedded NUL are both rejected.
    const std::vector<LocalizationTableEntryDesc> truncatedValue{
        {.key = "ui.menu.play", .text = std::string_view{"\xE4\xB8", 2U}},
    };
    expectWriteError(validDesc(truncatedValue), AssetFormatErrorCode::InvalidLayout, "truncated value");
    const std::vector<LocalizationTableEntryDesc> nulValue{
        {.key = "ui.menu.play", .text = std::string_view{"a\0b", 3U}},
    };
    expectWriteError(validDesc(nulValue), AssetFormatErrorCode::InvalidLayout, "nul value");

    const std::string overlongValue(LocalizationTableWire::MaximumValueBytes + 1U, 'x');
    const std::vector<LocalizationTableEntryDesc> overlong{
        {.key = "ui.menu.play", .text = overlongValue},
    };
    expectWriteError(validDesc(overlong), AssetFormatErrorCode::SizeLimitExceeded, "overlong value");
    const std::string maximumValue(LocalizationTableWire::MaximumValueBytes, 'x');
    const std::vector<LocalizationTableEntryDesc> atMaximum{
        {.key = "ui.menu.play", .text = maximumValue},
    };
    EXPECT_TRUE(writeLocalizationTablePayloadBytes(validDesc(atMaximum)));
}

TEST(LocalizationTablePayloadTests, WriterFailsClosedOnEntryAndTextLimits)
{
    // Entry count: one past the cap must fail, and the cap itself must be reachable. Both tables
    // are built from distinct keys so the strict-ordering rule is not what rejects them.
    std::vector<std::string> keys;
    keys.reserve(LocalizationTableWire::MaximumEntryCount + 1U);
    for (u32 index = 0; index <= LocalizationTableWire::MaximumEntryCount; ++index)
    {
        keys.push_back("key." + std::to_string(index));
    }
    std::vector<LocalizationTableEntryDesc> entries;
    entries.reserve(keys.size());
    for (const std::string& key : keys)
    {
        entries.push_back({.key = key, .text = "v"});
    }
    entries = sortedEntries(std::move(entries));
    // FNV-1a over these distinct keys must not collide, or the ordering rule would mask the cap.
    for (usize index = 1; index < entries.size(); ++index)
    {
        ASSERT_LT(localizationKeyHash(entries[index - 1U].key), localizationKeyHash(entries[index].key));
    }

    expectWriteError(validDesc(entries), AssetFormatErrorCode::SizeLimitExceeded, "entry cap");

    entries.pop_back();
    ASSERT_EQ(entries.size(), LocalizationTableWire::MaximumEntryCount);
    auto atCap = writeLocalizationTablePayloadBytes(validDesc(entries));
    ASSERT_TRUE(atCap) << atCap.error().message;
    EXPECT_EQ(atCap->size(), LocalizationTableWire::HeaderBytes +
                                 static_cast<usize>(LocalizationTableWire::MaximumEntryCount) *
                                     LocalizationTableWire::EntryBytes +
                                 LocalizationTableWire::MaximumEntryCount);

    // Aggregate text budget: many maximum-size values sum past MaximumTextBytes even though each
    // one passes the per-entry check.
    constexpr u32 ValueCount = (LocalizationTableWire::MaximumTextBytes /
                                LocalizationTableWire::MaximumValueBytes) +
                               1U;
    const std::string chunk(LocalizationTableWire::MaximumValueBytes, 'x');
    std::vector<std::string> bulkKeys;
    bulkKeys.reserve(ValueCount);
    for (u32 index = 0; index < ValueCount; ++index)
    {
        bulkKeys.push_back("bulk." + std::to_string(index));
    }
    std::vector<LocalizationTableEntryDesc> bulk;
    bulk.reserve(ValueCount);
    for (const std::string& key : bulkKeys)
    {
        bulk.push_back({.key = key, .text = chunk});
    }
    bulk = sortedEntries(std::move(bulk));
    expectWriteError(validDesc(bulk), AssetFormatErrorCode::SizeLimitExceeded, "text cap");
}

TEST(LocalizationTablePayloadTests, ParserRejectsCorruptedHeaderFields)
{
    const auto entries = validEntries();
    const auto valid = writeLocalizationTablePayloadBytes(validDesc(entries));
    ASSERT_TRUE(valid) << valid.error().message;

    auto wrongSchema = *valid;
    putU16(wrongSchema, 0U, LocalizationTableWire::SchemaVersion + 1U);
    expectParseError(wrongSchema, AssetFormatErrorCode::UnsupportedSchema);

    // Both reserved words must be checked; the second one sits after the locale tag field.
    auto reserved0 = *valid;
    putU32(reserved0, 12U, 1U);
    expectParseError(reserved0, AssetFormatErrorCode::InvalidLayout);

    auto reserved1 = *valid;
    putU32(reserved1, 16U + LocalizationTableWire::LocaleTagBytes, 1U);
    expectParseError(reserved1, AssetFormatErrorCode::InvalidLayout);

    auto zeroTag = *valid;
    putU16(zeroTag, 2U, 0U);
    expectParseError(zeroTag, AssetFormatErrorCode::InvalidLayout);

    auto overlongTag = *valid;
    putU16(overlongTag, 2U, static_cast<Core::u16>(LocalizationTableWire::LocaleTagBytes));
    expectParseError(overlongTag, AssetFormatErrorCode::InvalidLayout);

    // Non-zero padding past localeTagLength is a second spelling of the same table.
    auto dirtyPad = *valid;
    dirtyPad[16U + LocalizationTableWire::LocaleTagBytes - 1U] = std::byte{'x'};
    expectParseError(dirtyPad, AssetFormatErrorCode::InvalidLayout);

    // A bad tag inside the declared length is rejected even though the length itself is legal.
    auto badTagBytes = *valid;
    badTagBytes[16U] = std::byte{'-'};
    expectParseError(badTagBytes, AssetFormatErrorCode::InvalidLayout);

    auto zeroEntries = *valid;
    putU32(zeroEntries, 4U, 0U);
    expectParseError(zeroEntries, AssetFormatErrorCode::SizeLimitExceeded);

    auto tooManyEntries = *valid;
    putU32(tooManyEntries, 4U, LocalizationTableWire::MaximumEntryCount + 1U);
    expectParseError(tooManyEntries, AssetFormatErrorCode::SizeLimitExceeded);

    auto tooMuchText = *valid;
    putU32(tooMuchText, 8U, LocalizationTableWire::MaximumTextBytes + 1U);
    expectParseError(tooMuchText, AssetFormatErrorCode::SizeLimitExceeded);

    // A count that no longer matches the byte total is caught before any span is formed, so an
    // inflated entryCount cannot be used to read past the buffer.
    auto inflatedCount = *valid;
    putU32(inflatedCount, 4U, static_cast<u32>(entries.size()) + 1U);
    expectParseError(inflatedCount, AssetFormatErrorCode::InvalidLayout);
}

TEST(LocalizationTablePayloadTests, ParserRejectsTruncatedAndTrailingBytes)
{
    const auto entries = validEntries();
    const auto valid = writeLocalizationTablePayloadBytes(validDesc(entries));
    ASSERT_TRUE(valid) << valid.error().message;

    auto truncated = *valid;
    truncated.pop_back();
    expectParseError(truncated, AssetFormatErrorCode::InvalidLayout);

    // Shorter than the fixed header is a distinct failure from a short variable block.
    std::vector<std::byte> headerShort(valid->begin(),
                                       valid->begin() + LocalizationTableWire::HeaderBytes - 1U);
    expectParseError(headerShort, AssetFormatErrorCode::InvalidHeader);
    expectParseError({}, AssetFormatErrorCode::InvalidHeader);

    auto trailing = *valid;
    trailing.push_back(std::byte{0});
    expectParseError(trailing, AssetFormatErrorCode::InvalidLayout);
}

TEST(LocalizationTablePayloadTests, ParserRejectsCorruptedEntryRecords)
{
    const auto entries = validEntries();
    const auto valid = writeLocalizationTablePayloadBytes(validDesc(entries));
    ASSERT_TRUE(valid) << valid.error().message;
    ASSERT_GE(entries.size(), 3U);

    // Swapping two adjacent key hashes breaks the ascending order without changing any size.
    auto unsorted = *valid;
    const u64 firstHash = localizationKeyHash(entries[0].key);
    const u64 secondHash = localizationKeyHash(entries[1].key);
    putU64(unsorted, entryOffset(0U), secondHash);
    putU64(unsorted, entryOffset(1U), firstHash);
    expectParseError(unsorted, AssetFormatErrorCode::InvalidLayout);

    auto duplicate = *valid;
    putU64(duplicate, entryOffset(1U), firstHash);
    expectParseError(duplicate, AssetFormatErrorCode::InvalidLayout);

    // A length that runs off the end of the blob.
    auto escapingLength = *valid;
    putU32(escapingLength, entryOffset(0U) + 12U, LocalizationTableWire::MaximumValueBytes);
    expectParseError(escapingLength, AssetFormatErrorCode::InvalidLayout);

    // An offset past the blob with a zero length: caught by the offset bound, not the sum.
    auto escapingOffset = *valid;
    const u32 textBytes = [&] {
        u32 total = 0;
        for (const auto& entry : entries)
        {
            total += static_cast<u32>(entry.text.size());
        }
        return total;
    }();
    putU32(escapingOffset, entryOffset(0U) + 8U, textBytes + 1U);
    putU32(escapingOffset, entryOffset(0U) + 12U, 0U);
    expectParseError(escapingOffset, AssetFormatErrorCode::InvalidLayout);

    // offset + length overflows u32; the wrapped sum must not read as in-bounds.
    auto wrapping = *valid;
    putU32(wrapping, entryOffset(0U) + 8U, 0xFFFFFFF0U);
    putU32(wrapping, entryOffset(0U) + 12U, 0x20U);
    expectParseError(wrapping, AssetFormatErrorCode::InvalidLayout);

    // A per-entry length past MaximumValueBytes is a limit failure, not a range failure, even when
    // the blob is far too small to hold it.
    auto overlongValue = *valid;
    putU32(overlongValue, entryOffset(0U) + 12U, LocalizationTableWire::MaximumValueBytes + 1U);
    expectParseError(overlongValue, AssetFormatErrorCode::SizeLimitExceeded);
}

TEST(LocalizationTablePayloadTests, ParserRejectsInvalidUtf8InTheTextBlob)
{
    const std::vector<LocalizationTableEntryDesc> entries{{.key = "ui.menu.play", .text = "ok"}};
    const auto valid = writeLocalizationTablePayloadBytes(validDesc(entries));
    ASSERT_TRUE(valid) << valid.error().message;

    const usize textBase = LocalizationTableWire::HeaderBytes + LocalizationTableWire::EntryBytes;
    auto invalidUtf8 = *valid;
    invalidUtf8[textBase] = std::byte{0xFF};
    expectParseError(invalidUtf8, AssetFormatErrorCode::InvalidLayout);

    auto embeddedNul = *valid;
    embeddedNul[textBase] = std::byte{0};
    expectParseError(embeddedNul, AssetFormatErrorCode::InvalidLayout);

    // A truncated multi-byte sequence: valid as a prefix, invalid as a whole value.
    auto truncatedSequence = *valid;
    truncatedSequence[textBase] = std::byte{0xE4};
    truncatedSequence[textBase + 1U] = std::byte{0xB8};
    expectParseError(truncatedSequence, AssetFormatErrorCode::InvalidLayout);
}

TEST(LocalizationTablePayloadTests, SharedTextRangesAreAcceptedAndDeduplicate)
{
    // The parser validates ranges rather than a packing, so a producer may point two entries at the
    // same bytes. This documents that the round trip survives it.
    const auto entries = sortedEntries(std::vector<LocalizationTableEntryDesc>{
        LocalizationTableEntryDesc{.key = "ui.ok.a", .text = "OK"},
        LocalizationTableEntryDesc{.key = "ui.ok.b", .text = "OK"},
    });
    auto payload = writeLocalizationTablePayloadBytes(validDesc(entries));
    ASSERT_TRUE(payload) << payload.error().message;

    // Repoint the second entry at the first entry's bytes and shrink the blob accordingly.
    std::vector<std::byte> deduplicated(payload->begin(), payload->end() - 2);
    putU32(deduplicated, 8U, 2U);
    putU32(deduplicated, entryOffset(1U) + 8U, 0U);
    putU32(deduplicated, entryOffset(1U) + 12U, 2U);

    auto view = parseLocalizationTablePayload(deduplicated);
    ASSERT_TRUE(view) << view.error().message;
    EXPECT_EQ(view->textBytes, 2U);
    EXPECT_EQ(view->text(0U), "OK");
    EXPECT_EQ(view->text(1U), "OK");
}


} // namespace
} // namespace Tina::AssetFormat
