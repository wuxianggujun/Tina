#include <tina/core/hash/StringKeyHash.hpp>
#include <tina/localization/LocalizationCatalog.hpp>
#include <tina/localization/LocalizationErrors.hpp>
#include <tina/localization/LocalizedText.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::Localization {
namespace {

// ---------------------------------------------------------------------------
// Fixture table.
//
// Every key below is chosen by its FNV-1a hash, not by looking readable: the
// binary-search boundary tests need probes that provably land before the first
// entry, after the last one, and strictly between two adjacent present entries.
// The static_asserts underneath are what prove those branches are exercised --
// without them a rename could silently move a probe onto a different branch and
// the tests would still pass.
// ---------------------------------------------------------------------------

constexpr std::string_view TextBlob =
    "Start Game"                 // [0, 10)
    "Lives"                      // [10, 15)
    "Torch"                      // [15, 20)
    "Jump"                       // [20, 24)
    "Credits"                    // [24, 31)
    "\xE6\x97\xA5\xE6\x9C\xAC";  // [31, 37) U+65E5 U+672C, three bytes each
static_assert(TextBlob.size() == 37);

constexpr std::string_view KeyFirst = "menu.quit";        // 0x09be'c317'b770'dccb
constexpr std::string_view KeyLives = "hud.lives";        // 0x2986'67be'aa81'2a4b
constexpr std::string_view KeyAudio = "settings.audio";   // 0x3a9d'fae5'5e56'783c
constexpr std::string_view KeyTorch = "item.torch";       // 0x4123'4dca'e50e'6672
constexpr std::string_view KeyAbsent = "dialog.ok";       // 0x578f'5cf1'd157'd2ff
constexpr std::string_view KeyEmpty = "settings.video";   // 0x665a'0c90'7e6f'9fd9
constexpr std::string_view KeyJump = "tutorial.jump";     // 0x6cd7'ffd0'6b8d'174f
constexpr std::string_view KeyLast = "credits.title";     // 0xa7b0'b488'ec9a'd70d

// Absent from the table, positioned deliberately.
constexpr std::string_view ProbeBelowFirst = "k8";          // 0x08be'0607'b562'1576
constexpr std::string_view ProbeAboveLast = "hud.score";    // 0xfe9e'e05b'671a'f0ca
constexpr std::string_view ProbeBetweenLow = "aaa.absent";  // between KeyTorch and KeyAbsent
constexpr std::string_view ProbeBetweenHigh = "probe.a";    // between KeyJump and KeyLast

constexpr std::array<LocalizationEntryDesc, 8> BaseEntries{{
    {.keyHash = localizationKeyHash(KeyFirst), .textOffset = 0, .textLength = 10},
    {.keyHash = localizationKeyHash(KeyLives), .textOffset = 10, .textLength = 5},
    {.keyHash = localizationKeyHash(KeyAudio), .textOffset = 31, .textLength = 6},
    {.keyHash = localizationKeyHash(KeyTorch), .textOffset = 15, .textLength = 5},
    {.keyHash = localizationKeyHash(KeyAbsent),
     .textOffset = LocalizationCatalogContract::AbsentTextOffset,
     .textLength = 0},
    // Authored empty translation: present, zero length. Must resolve successfully
    // and never be confused with the absent encoding above.
    {.keyHash = localizationKeyHash(KeyEmpty), .textOffset = 24, .textLength = 0},
    {.keyHash = localizationKeyHash(KeyJump), .textOffset = 20, .textLength = 4},
    {.keyHash = localizationKeyHash(KeyLast), .textOffset = 24, .textLength = 7},
}};

static_assert(
    [] {
        for (std::size_t index = 1; index < BaseEntries.size(); ++index) {
            if (BaseEntries[index - 1U].keyHash >= BaseEntries[index].keyHash) {
                return false;
            }
        }
        return true;
    }(),
    "fixture entries must be strictly ascending by key hash");

static_assert(localizationKeyHash(ProbeBelowFirst) < localizationKeyHash(KeyFirst),
              "ProbeBelowFirst must hash below the first entry");
static_assert(localizationKeyHash(ProbeAboveLast) > localizationKeyHash(KeyLast),
              "ProbeAboveLast must hash above the last entry");
static_assert(localizationKeyHash(KeyTorch) < localizationKeyHash(ProbeBetweenLow)
                  && localizationKeyHash(ProbeBetweenLow) < localizationKeyHash(KeyAbsent),
              "ProbeBetweenLow must fall strictly between two adjacent present entries");
static_assert(localizationKeyHash(KeyJump) < localizationKeyHash(ProbeBetweenHigh)
                  && localizationKeyHash(ProbeBetweenHigh) < localizationKeyHash(KeyLast),
              "ProbeBetweenHigh must fall strictly between two adjacent present entries");

[[nodiscard]] std::span<const char> textSpan(std::string_view text) noexcept
{
    return std::span<const char>(text.data(), text.size());
}

[[nodiscard]] LocalizationTableDesc baseDesc(std::string_view locale = "en-US") noexcept
{
    return LocalizationTableDesc{.entries = BaseEntries,
                                 .text = textSpan(TextBlob),
                                 .locale = LocaleTag::parse(locale)};
}

[[nodiscard]] Core::Result<LocalizationCatalog> makeBase(std::pmr::memory_resource& memory,
                                                         std::string_view locale = "en-US")
{
    return LocalizationCatalog::Create(baseDesc(locale), LocalizationCatalogConfig{}, memory);
}

// ---------------------------------------------------------------------------
// Ingestion and resolution.
// ---------------------------------------------------------------------------

TEST(LocalizationCatalogTests, ResolvesEveryIngestedKeyToItsExactText)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = makeBase(memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    EXPECT_TRUE(static_cast<bool>(*catalog));
    EXPECT_EQ(catalog->locale().text(), "en-US");
    EXPECT_EQ(catalog->entryCount(), 8U);
    EXPECT_EQ(catalog->entryCapacity(), 1024U);
    EXPECT_EQ(catalog->textByteCount(), TextBlob.size());
    EXPECT_EQ(catalog->textByteCapacity(), Core::usize{256} * 1024U);
    EXPECT_EQ(catalog->revision(), 1U);
    EXPECT_NE(catalog->catalogIdentity(), 0U);

    const auto expectText = [&](std::string_view key, std::string_view expected) {
        const LocalizedTextId id = catalog->findTextId(key);
        ASSERT_TRUE(id.hasValue()) << key;
        const Core::Result<std::string_view> text = catalog->resolve(id);
        ASSERT_TRUE(text.has_value()) << key << ": " << text.error().message;
        EXPECT_EQ(*text, expected) << key;
    };

    expectText(KeyFirst, "Start Game");
    expectText(KeyLives, "Lives");
    expectText(KeyTorch, "Torch");
    expectText(KeyJump, "Jump");
    expectText(KeyLast, "Credits");
    // Multi-byte UTF-8 must come back byte-exact, not truncated at a lead byte.
    expectText(KeyAudio, "\xE6\x97\xA5\xE6\x9C\xAC");
    // An authored empty translation is a hit, not a miss.
    expectText(KeyEmpty, "");
}

TEST(LocalizationCatalogTests, FindTextIdAndFindTextIdByKeyHashAgree)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = makeBase(memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    for (const std::string_view key :
         {KeyFirst, KeyLives, KeyAudio, KeyTorch, KeyAbsent, KeyEmpty, KeyJump, KeyLast}) {
        const LocalizedTextId byKey = catalog->findTextId(key);
        const LocalizedTextId byHash = catalog->findTextIdByKeyHash(localizationKeyHash(key));
        EXPECT_EQ(byKey, byHash) << key;
        ASSERT_TRUE(byKey.hasValue()) << key;

        // localizationKeyHash() must forward to the one shared Core hash, otherwise a
        // cooker-side hash and a runtime-side hash could drift apart silently.
        EXPECT_EQ(localizationKeyHash(key), Core::stringKeyHash(key)) << key;

        const Core::Result<Core::u64> hash = catalog->keyHashOf(byKey);
        ASSERT_TRUE(hash.has_value()) << key;
        EXPECT_EQ(*hash, Core::stringKeyHash(key)) << key;
    }
}

TEST(LocalizationCatalogTests, UnknownKeyYieldsInvalidIdAndResolvingItFails)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = makeBase(memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    const LocalizedTextId unknown = catalog->findTextId("no.such.key");
    // Half one: the miss is an explicitly invalid handle.
    EXPECT_FALSE(unknown.hasValue());
    EXPECT_FALSE(static_cast<bool>(unknown));
    EXPECT_EQ(unknown, LocalizedTextId{});
    EXPECT_EQ(unknown.catalogIdentity(), 0U);
    EXPECT_EQ(unknown.slot(), LocalizedTextId::InvalidSlot);
    EXPECT_FALSE(catalog->contains(unknown));

    // Half two: resolving it FAILS. It must not come back as an empty string, which a
    // caller would render as a legitimately blank translation.
    const Core::Result<std::string_view> text = catalog->resolve(unknown);
    ASSERT_FALSE(text.has_value());
    EXPECT_EQ(text.error().code, LocalizationErrorCode::InvalidTextId);

    const Core::Result<Core::u64> hash = catalog->keyHashOf(unknown);
    ASSERT_FALSE(hash.has_value());
    EXPECT_EQ(hash.error().code, LocalizationErrorCode::InvalidTextId);

    // A default-constructed id behaves the same way.
    const Core::Result<std::string_view> fromDefault = catalog->resolve(LocalizedTextId{});
    ASSERT_FALSE(fromDefault.has_value());
    EXPECT_EQ(fromDefault.error().code, LocalizationErrorCode::InvalidTextId);

    EXPECT_EQ(catalog->resolveOr(unknown, "<fallback>"), "<fallback>");
}

TEST(LocalizationCatalogTests, AbsentTranslationIsValidIdButMissingText)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = makeBase(memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    const LocalizedTextId absent = catalog->findTextId(KeyAbsent);
    // The key exists, so the id is valid even though this locale carries no text.
    ASSERT_TRUE(absent.hasValue());
    EXPECT_TRUE(catalog->contains(absent));
    EXPECT_EQ(absent.catalogIdentity(), catalog->catalogIdentity());

    const Core::Result<std::string_view> text = catalog->resolve(absent);
    ASSERT_FALSE(text.has_value());
    // Distinct from InvalidTextId: "key exists, not translated" is not "no such key".
    EXPECT_EQ(text.error().code, LocalizationErrorCode::MissingText);

    // The key hash survives even with no text behind the slot, which is what would let a
    // future re-point rebuild the blob without re-interning.
    const Core::Result<Core::u64> hash = catalog->keyHashOf(absent);
    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(*hash, Core::stringKeyHash(KeyAbsent));

    EXPECT_EQ(catalog->resolveOr(absent, "<fallback>"), "<fallback>");

    // Contrast with the authored-empty slot, which resolves successfully to "".
    const LocalizedTextId empty = catalog->findTextId(KeyEmpty);
    ASSERT_TRUE(empty.hasValue());
    const Core::Result<std::string_view> emptyText = catalog->resolve(empty);
    ASSERT_TRUE(emptyText.has_value()) << emptyText.error().message;
    EXPECT_TRUE(emptyText->empty());
    EXPECT_EQ(catalog->resolveOr(empty, "<fallback>"), "");
}

// ---------------------------------------------------------------------------
// Binary-search boundaries.
// ---------------------------------------------------------------------------

TEST(LocalizationCatalogTests, BinarySearchHitsFirstAndLastEntry)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = makeBase(memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    const LocalizedTextId first = catalog->findTextId(KeyFirst);
    ASSERT_TRUE(first.hasValue());
    EXPECT_EQ(first.slot(), 0U);
    EXPECT_EQ(catalog->resolveOr(first, "<none>"), "Start Game");

    const LocalizedTextId last = catalog->findTextId(KeyLast);
    ASSERT_TRUE(last.hasValue());
    EXPECT_EQ(last.slot(), catalog->entryCount() - 1U);
    EXPECT_EQ(catalog->resolveOr(last, "<none>"), "Credits");

    // Slots follow the ascending key order, so an id is a direct index.
    for (Core::u32 slot = 0; slot < catalog->entryCount(); ++slot) {
        const LocalizedTextId id = catalog->findTextIdByKeyHash(BaseEntries[slot].keyHash);
        ASSERT_TRUE(id.hasValue()) << slot;
        EXPECT_EQ(id.slot(), slot);
    }
}

TEST(LocalizationCatalogTests, BinarySearchMissesBelowFirstAboveLastAndBetweenEntries)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = makeBase(memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    // lower_bound lands on begin().
    EXPECT_FALSE(catalog->findTextId(ProbeBelowFirst).hasValue());
    // lower_bound lands on end() -- the branch that must not dereference.
    EXPECT_FALSE(catalog->findTextId(ProbeAboveLast).hasValue());
    // lower_bound lands on a real entry whose hash differs.
    EXPECT_FALSE(catalog->findTextId(ProbeBetweenLow).hasValue());
    EXPECT_FALSE(catalog->findTextId(ProbeBetweenHigh).hasValue());

    // Exact numeric neighbours of the first and last entry, one step off in each
    // direction. These pin the comparison as an equality check, not a range check.
    EXPECT_FALSE(catalog->findTextIdByKeyHash(localizationKeyHash(KeyFirst) - 1U).hasValue());
    EXPECT_FALSE(catalog->findTextIdByKeyHash(localizationKeyHash(KeyFirst) + 1U).hasValue());
    EXPECT_FALSE(catalog->findTextIdByKeyHash(localizationKeyHash(KeyLast) - 1U).hasValue());
    EXPECT_FALSE(catalog->findTextIdByKeyHash(localizationKeyHash(KeyLast) + 1U).hasValue());
    EXPECT_FALSE(catalog->findTextIdByKeyHash(0U).hasValue());
    EXPECT_FALSE(catalog->findTextIdByKeyHash(~Core::u64{0}).hasValue());
}

TEST(LocalizationCatalogTests, SingleEntryTableSearchesBothSides)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array<LocalizationEntryDesc, 1> only{{
        {.keyHash = localizationKeyHash(KeyLives), .textOffset = 0, .textLength = 5},
    }};
    auto catalog = LocalizationCatalog::Create(
        LocalizationTableDesc{
            .entries = only, .text = textSpan("Lives"), .locale = LocaleTag::parse("en")},
        LocalizationCatalogConfig{}, memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 1U);

    const LocalizedTextId hit = catalog->findTextId(KeyLives);
    ASSERT_TRUE(hit.hasValue());
    EXPECT_EQ(hit.slot(), 0U);
    EXPECT_EQ(catalog->resolveOr(hit, "<none>"), "Lives");

    EXPECT_FALSE(catalog->findTextIdByKeyHash(localizationKeyHash(KeyLives) - 1U).hasValue());
    EXPECT_FALSE(catalog->findTextIdByKeyHash(localizationKeyHash(KeyLives) + 1U).hasValue());
}

// ---------------------------------------------------------------------------
// Capacity. Never grows; both dimensions are independent.
// ---------------------------------------------------------------------------

TEST(LocalizationCatalogTests, EntryCountCapacityExhaustionFailsWithoutGrowing)
{
    std::pmr::unsynchronized_pool_resource memory;

    // One entry short of the table size: entry capacity is the binding constraint while
    // the text capacity stays generous.
    auto tooManyEntries = LocalizationCatalog::Create(
        baseDesc(),
        LocalizationCatalogConfig{.entryCapacity = static_cast<Core::u32>(BaseEntries.size()) - 1U,
                                  .textByteCapacity = Core::usize{1} * 1024U},
        memory);
    ASSERT_FALSE(tooManyEntries.has_value());
    EXPECT_EQ(tooManyEntries.error().code, LocalizationErrorCode::CapacityExceeded);

    // Exactly at the entry capacity must succeed: an advertised limit has to be usable.
    auto exact = LocalizationCatalog::Create(
        baseDesc(),
        LocalizationCatalogConfig{.entryCapacity = static_cast<Core::u32>(BaseEntries.size()),
                                  .textByteCapacity = Core::usize{1} * 1024U},
        memory);
    ASSERT_TRUE(exact.has_value()) << exact.error().message;
    EXPECT_EQ(exact->entryCount(), BaseEntries.size());
    EXPECT_EQ(exact->entryCapacity(), BaseEntries.size());

    // A zero or over-range requested capacity is itself rejected, not clamped.
    auto zero = LocalizationCatalog::Create(baseDesc(),
                                            LocalizationCatalogConfig{.entryCapacity = 0}, memory);
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, LocalizationErrorCode::CapacityExceeded);

    auto overRange = LocalizationCatalog::Create(
        baseDesc(),
        LocalizationCatalogConfig{.entryCapacity =
                                      LocalizationCatalogContract::MaximumEntryCount + 1U},
        memory);
    ASSERT_FALSE(overRange.has_value());
    EXPECT_EQ(overRange.error().code, LocalizationErrorCode::CapacityExceeded);
}

TEST(LocalizationCatalogTests, TextByteCapacityExhaustionFailsIndependentlyOfEntryCapacity)
{
    std::pmr::unsynchronized_pool_resource memory;

    // Entry capacity is deliberately roomy, so only the byte budget can fail this.
    auto tooManyBytes = LocalizationCatalog::Create(
        baseDesc(),
        LocalizationCatalogConfig{.entryCapacity = 64,
                                  .textByteCapacity = TextBlob.size() - 1U},
        memory);
    ASSERT_FALSE(tooManyBytes.has_value());
    EXPECT_EQ(tooManyBytes.error().code, LocalizationErrorCode::CapacityExceeded);

    auto exact = LocalizationCatalog::Create(
        baseDesc(),
        LocalizationCatalogConfig{.entryCapacity = 64, .textByteCapacity = TextBlob.size()},
        memory);
    ASSERT_TRUE(exact.has_value()) << exact.error().message;
    EXPECT_EQ(exact->textByteCount(), TextBlob.size());
    EXPECT_EQ(exact->textByteCapacity(), TextBlob.size());
    EXPECT_EQ(exact->resolveOr(exact->findTextId(KeyFirst), "<none>"), "Start Game");

    auto overRange = LocalizationCatalog::Create(
        baseDesc(),
        LocalizationCatalogConfig{.entryCapacity = 64,
                                  .textByteCapacity =
                                      LocalizationCatalogContract::MaximumTextBytes + 1U},
        memory);
    ASSERT_FALSE(overRange.has_value());
    EXPECT_EQ(overRange.error().code, LocalizationErrorCode::CapacityExceeded);

    // A table with no text at all fits a zero byte budget: absent entries need no bytes.
    const std::array<LocalizationEntryDesc, 1> absentOnly{{
        {.keyHash = localizationKeyHash(KeyAbsent),
         .textOffset = LocalizationCatalogContract::AbsentTextOffset,
         .textLength = 0},
    }};
    auto noText = LocalizationCatalog::Create(
        LocalizationTableDesc{
            .entries = absentOnly, .text = {}, .locale = LocaleTag::parse("en")},
        LocalizationCatalogConfig{.entryCapacity = 1, .textByteCapacity = 0}, memory);
    ASSERT_TRUE(noText.has_value()) << noText.error().message;
    EXPECT_EQ(noText->textByteCount(), 0U);
    EXPECT_EQ(noText->resolve(noText->findTextId(KeyAbsent)).error().code,
              LocalizationErrorCode::MissingText);
}

// ---------------------------------------------------------------------------
// Ingestion validation.
// ---------------------------------------------------------------------------

TEST(LocalizationCatalogTests, RejectsUnsortedAndDuplicateKeyHashes)
{
    std::pmr::unsynchronized_pool_resource memory;

    const std::array<LocalizationEntryDesc, 3> descending{{
        {.keyHash = localizationKeyHash(KeyLast), .textOffset = 0, .textLength = 5},
        {.keyHash = localizationKeyHash(KeyLives), .textOffset = 0, .textLength = 5},
        {.keyHash = localizationKeyHash(KeyTorch), .textOffset = 0, .textLength = 5},
    }};
    auto unsorted = LocalizationCatalog::Create(
        LocalizationTableDesc{
            .entries = descending, .text = textSpan("Lives"), .locale = LocaleTag::parse("en")},
        LocalizationCatalogConfig{}, memory);
    ASSERT_FALSE(unsorted.has_value());
    EXPECT_EQ(unsorted.error().code, LocalizationErrorCode::UnsortedTable);

    // Strictly ascending, so an equal neighbour is rejected too: a duplicate key would
    // make lookup depend on which of the two the search happened to land on.
    const std::array<LocalizationEntryDesc, 3> duplicated{{
        {.keyHash = localizationKeyHash(KeyLives), .textOffset = 0, .textLength = 5},
        {.keyHash = localizationKeyHash(KeyLives), .textOffset = 0, .textLength = 5},
        {.keyHash = localizationKeyHash(KeyLast), .textOffset = 0, .textLength = 5},
    }};
    auto duplicate = LocalizationCatalog::Create(
        LocalizationTableDesc{
            .entries = duplicated, .text = textSpan("Lives"), .locale = LocaleTag::parse("en")},
        LocalizationCatalogConfig{}, memory);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, LocalizationErrorCode::UnsortedTable);

    // A duplicate on the very last pair must be caught too, not skipped by an
    // off-by-one in the scan bound.
    const std::array<LocalizationEntryDesc, 3> duplicatedTail{{
        {.keyHash = localizationKeyHash(KeyLives), .textOffset = 0, .textLength = 5},
        {.keyHash = localizationKeyHash(KeyLast), .textOffset = 0, .textLength = 5},
        {.keyHash = localizationKeyHash(KeyLast), .textOffset = 0, .textLength = 5},
    }};
    auto duplicateTail = LocalizationCatalog::Create(
        LocalizationTableDesc{
            .entries = duplicatedTail, .text = textSpan("Lives"), .locale = LocaleTag::parse("en")},
        LocalizationCatalogConfig{}, memory);
    ASSERT_FALSE(duplicateTail.has_value());
    EXPECT_EQ(duplicateTail.error().code, LocalizationErrorCode::UnsortedTable);
}

TEST(LocalizationCatalogTests, RejectsEmptyTableAndOutOfBoundsTextRanges)
{
    std::pmr::unsynchronized_pool_resource memory;

    auto empty = LocalizationCatalog::Create(
        LocalizationTableDesc{.entries = {}, .text = {}, .locale = LocaleTag::parse("en")},
        LocalizationCatalogConfig{}, memory);
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, LocalizationErrorCode::InvalidData);

    const auto oneEntry = [&](Core::u32 offset, Core::u32 length) {
        return std::array<LocalizationEntryDesc, 1>{{
            {.keyHash = localizationKeyHash(KeyLives),
             .textOffset = offset,
             .textLength = length},
        }};
    };
    const auto create = [&](const std::array<LocalizationEntryDesc, 1>& entries,
                            std::string_view text) {
        return LocalizationCatalog::Create(
            LocalizationTableDesc{
                .entries = entries, .text = textSpan(text), .locale = LocaleTag::parse("en")},
            LocalizationCatalogConfig{}, memory);
    };

    // Offset past the blob.
    const auto pastEnd = oneEntry(6, 0);
    auto pastEndResult = create(pastEnd, "Lives");
    ASSERT_FALSE(pastEndResult.has_value());
    EXPECT_EQ(pastEndResult.error().code, LocalizationErrorCode::InvalidTextRange);

    // Length runs one byte off the end.
    const auto runsOff = oneEntry(1, 5);
    auto runsOffResult = create(runsOff, "Lives");
    ASSERT_FALSE(runsOffResult.has_value());
    EXPECT_EQ(runsOffResult.error().code, LocalizationErrorCode::InvalidTextRange);

    // Length that would overflow a naive offset + length addition.
    const auto overflowing = oneEntry(1, (std::numeric_limits<Core::u32>::max)());
    auto overflowResult = create(overflowing, "Lives");
    ASSERT_FALSE(overflowResult.has_value());
    EXPECT_EQ(overflowResult.error().code, LocalizationErrorCode::InvalidTextRange);

    // Ending exactly at the blob end is legal.
    const auto exact = oneEntry(0, 5);
    auto exactResult = create(exact, "Lives");
    ASSERT_TRUE(exactResult.has_value()) << exactResult.error().message;

    // A zero-length slice at the one-past-the-end offset is legal: offset == size is
    // in range and the slice is empty.
    const auto atEnd = oneEntry(5, 0);
    auto atEndResult = create(atEnd, "Lives");
    ASSERT_TRUE(atEndResult.has_value()) << atEndResult.error().message;
    EXPECT_EQ(atEndResult->resolveOr(atEndResult->findTextId(KeyLives), "<none>"), "");

    // The absent encoding must declare a zero length; a non-zero one is contradictory.
    const auto absentWithLength =
        oneEntry(LocalizationCatalogContract::AbsentTextOffset, 3);
    auto absentWithLengthResult = create(absentWithLength, "Lives");
    ASSERT_FALSE(absentWithLengthResult.has_value());
    EXPECT_EQ(absentWithLengthResult.error().code, LocalizationErrorCode::InvalidTextRange);
}

TEST(LocalizationCatalogTests, RejectsNonUtf8AndEmbeddedNulText)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto create = [&](std::string_view text, Core::u32 length) {
        const std::array<LocalizationEntryDesc, 1> entries{{
            {.keyHash = localizationKeyHash(KeyLives), .textOffset = 0, .textLength = length},
        }};
        return LocalizationCatalog::Create(
            LocalizationTableDesc{
                .entries = entries, .text = textSpan(text), .locale = LocaleTag::parse("en")},
            LocalizationCatalogConfig{}, memory);
    };

    // Lone continuation byte.
    auto stray = create(std::string_view("\x80\x41", 2), 2);
    ASSERT_FALSE(stray.has_value());
    EXPECT_EQ(stray.error().code, LocalizationErrorCode::InvalidTextEncoding);

    // Truncated three-byte sequence: the slice cuts a valid character in half, which is
    // exactly what a wrong textLength produces.
    auto truncated = create("\xE6\x97\xA5", 2);
    ASSERT_FALSE(truncated.has_value());
    EXPECT_EQ(truncated.error().code, LocalizationErrorCode::InvalidTextEncoding);

    // Overlong encoding of '/'.
    auto overlong = create(std::string_view("\xC0\xAF", 2), 2);
    ASSERT_FALSE(overlong.has_value());
    EXPECT_EQ(overlong.error().code, LocalizationErrorCode::InvalidTextEncoding);

    // Surrogate half.
    auto surrogate = create(std::string_view("\xED\xA0\x80", 3), 3);
    ASSERT_FALSE(surrogate.has_value());
    EXPECT_EQ(surrogate.error().code, LocalizationErrorCode::InvalidTextEncoding);

    // U+0000 inside the slice. Text is not NUL-terminated here, so an embedded NUL would
    // truncate every consumer that treats the view as a C string.
    auto embeddedNul = create(std::string_view("a\0b", 3), 3);
    ASSERT_FALSE(embeddedNul.has_value());
    EXPECT_EQ(embeddedNul.error().code, LocalizationErrorCode::InvalidTextEncoding);

    // Four-byte astral character is valid and must round-trip.
    auto astral = create("\xF0\x9F\x8E\xAE", 4);
    ASSERT_TRUE(astral.has_value()) << astral.error().message;
    EXPECT_EQ(astral->resolveOr(astral->findTextId(KeyLives), "<none>"), "\xF0\x9F\x8E\xAE");
}

// ---------------------------------------------------------------------------
// Catalog identity: what stops an id from one catalog resolving against another.
// ---------------------------------------------------------------------------

TEST(LocalizationCatalogTests, IdFromOneCatalogIsRejectedByAnother)
{
    std::pmr::unsynchronized_pool_resource memory;
    // Both catalogs are built from the identical table, so the slot number in the id is a
    // perfectly valid index into the other one. Only the identity can tell them apart --
    // that is the guarantee under test, not a bounds check in disguise.
    auto first = makeBase(memory, "en-US");
    auto second = makeBase(memory, "en-US");
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;

    EXPECT_NE(first->catalogIdentity(), second->catalogIdentity());
    EXPECT_EQ(first->entryCount(), second->entryCount());

    const LocalizedTextId fromFirst = first->findTextId(KeyFirst);
    ASSERT_TRUE(fromFirst.hasValue());
    const LocalizedTextId fromSecond = second->findTextId(KeyFirst);
    ASSERT_TRUE(fromSecond.hasValue());
    // Same slot, different identity: the ids are not interchangeable.
    EXPECT_EQ(fromFirst.slot(), fromSecond.slot());
    EXPECT_NE(fromFirst, fromSecond);

    EXPECT_TRUE(first->contains(fromFirst));
    EXPECT_FALSE(second->contains(fromFirst));
    EXPECT_FALSE(first->contains(fromSecond));

    const Core::Result<std::string_view> crossed = second->resolve(fromFirst);
    ASSERT_FALSE(crossed.has_value());
    EXPECT_EQ(crossed.error().code, LocalizationErrorCode::InvalidTextId);
    EXPECT_EQ(second->resolveOr(fromFirst, "<fallback>"), "<fallback>");

    const Core::Result<Core::u64> crossedHash = second->keyHashOf(fromFirst);
    ASSERT_FALSE(crossedHash.has_value());
    EXPECT_EQ(crossedHash.error().code, LocalizationErrorCode::InvalidTextId);

    // Each still resolves against its own catalog.
    EXPECT_EQ(first->resolveOr(fromFirst, "<none>"), "Start Game");
    EXPECT_EQ(second->resolveOr(fromSecond, "<none>"), "Start Game");
}

TEST(LocalizationCatalogTests, IdentitiesAreNeverRecycledAcrossCatalogLifetimes)
{
    std::pmr::unsynchronized_pool_resource memory;
    Core::u32 destroyedIdentity = 0;
    LocalizedTextId staleId{};
    {
        auto shortLived = makeBase(memory);
        ASSERT_TRUE(shortLived.has_value()) << shortLived.error().message;
        destroyedIdentity = shortLived->catalogIdentity();
        staleId = shortLived->findTextId(KeyFirst);
        ASSERT_TRUE(staleId.hasValue());
    }

    // A recycled identity would let the stale id resolve against this new catalog.
    auto replacement = makeBase(memory);
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    EXPECT_NE(replacement->catalogIdentity(), destroyedIdentity);
    EXPECT_FALSE(replacement->contains(staleId));

    const Core::Result<std::string_view> stale = replacement->resolve(staleId);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, LocalizationErrorCode::InvalidTextId);
}

TEST(LocalizationCatalogTests, MoveTransfersIdentityAndLeavesSourceUnusable)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto source = makeBase(memory);
    ASSERT_TRUE(source.has_value()) << source.error().message;
    const Core::u32 identity = source->catalogIdentity();
    const LocalizedTextId id = source->findTextId(KeyLives);
    ASSERT_TRUE(id.hasValue());

    LocalizationCatalog moved = std::move(*source);
    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(moved.catalogIdentity(), identity);
    EXPECT_EQ(moved.locale().text(), "en-US");
    EXPECT_EQ(moved.entryCount(), 8U);
    EXPECT_EQ(moved.revision(), 1U);
    // The id outlives the move untouched, which is the point of the indirection.
    EXPECT_EQ(moved.resolveOr(id, "<none>"), "Lives");

    // The moved-from catalog drops its identity, so it can neither mint nor honour ids.
    EXPECT_FALSE(static_cast<bool>(*source));
    EXPECT_EQ(source->catalogIdentity(), 0U);
    EXPECT_FALSE(source->locale().hasValue());
    EXPECT_FALSE(source->findTextId(KeyLives).hasValue());
    EXPECT_FALSE(source->contains(id));
    const Core::Result<std::string_view> fromMovedFrom = source->resolve(id);
    ASSERT_FALSE(fromMovedFrom.has_value());
    EXPECT_EQ(fromMovedFrom.error().code, LocalizationErrorCode::InvalidTextId);
}

// ---------------------------------------------------------------------------
// Locale tags.
// ---------------------------------------------------------------------------

TEST(LocaleTagTests, RoundTripsAcceptedTagsByteExactly)
{
    for (const std::string_view text : {std::string_view("en"),
                                        std::string_view("en-US"),
                                        std::string_view("zh-Hans-CN"),
                                        std::string_view("de-DE-1996"),
                                        std::string_view("sr-Latn-RS"),
                                        std::string_view("x"),
                                        std::string_view("123")}) {
        EXPECT_TRUE(LocaleTag::isValidText(text)) << text;
        const LocaleTag tag = LocaleTag::parse(text);
        EXPECT_TRUE(tag.hasValue()) << text;
        EXPECT_TRUE(static_cast<bool>(tag)) << text;
        EXPECT_EQ(tag.text(), text);
        EXPECT_EQ(LocaleTag::parse(tag.text()), tag) << text;
    }

    // Casing is the cooker's job, so these stay two distinct tags rather than folding.
    EXPECT_NE(LocaleTag::parse("en-US"), LocaleTag::parse("en-us"));
    EXPECT_EQ(LocaleTag::parse("en-US"), LocaleTag::parse("en-US"));
    EXPECT_LT(LocaleTag::parse("en"), LocaleTag::parse("fr"));

    // Longest accepted tag is exactly MaximumLocaleTagLength bytes.
    const std::string longest(LocalizedTextContract::MaximumLocaleTagLength, 'a');
    EXPECT_TRUE(LocaleTag::isValidText(longest));
    const LocaleTag longestTag = LocaleTag::parse(longest);
    ASSERT_TRUE(longestTag.hasValue());
    EXPECT_EQ(longestTag.text(), longest);
    EXPECT_EQ(longestTag.text().size(), LocalizedTextContract::MaximumLocaleTagLength);
}

TEST(LocaleTagTests, RejectsMalformedTags)
{
    const std::string tooLong(LocalizedTextContract::MaximumLocaleTagLength + 1U, 'a');
    const std::string_view embeddedNul("en\0US", 5);

    const std::string_view invalid[] = {
        "",                 // empty
        "-en",              // leading separator
        "en-",              // trailing separator
        "en--US",           // doubled separator
        "-",                // separator only
        "--",               //
        "en_US",            // underscore is not the accepted separator
        "en US",            // space
        "en.US",            // dot
        "zh-\xE6\xBC\xA2",  // non-ASCII bytes
        "\xC3\xA9n",        // non-ASCII lead byte
        tooLong,            // over-long
        embeddedNul,        // NUL is neither alphanumeric nor a separator
    };

    for (const std::string_view text : invalid) {
        EXPECT_FALSE(LocaleTag::isValidText(text)) << '"' << text << '"';
        const LocaleTag tag = LocaleTag::parse(text);
        EXPECT_FALSE(tag.hasValue()) << '"' << text << '"';
        EXPECT_FALSE(static_cast<bool>(tag)) << '"' << text << '"';
        // A rejected tag parses to the default, which is what Create() then refuses.
        EXPECT_EQ(tag, LocaleTag{}) << '"' << text << '"';
        EXPECT_TRUE(tag.text().empty()) << '"' << text << '"';
    }

    EXPECT_FALSE(LocaleTag{}.hasValue());
}

TEST(LocalizationCatalogTests, CreateRejectsAnInvalidLocaleTag)
{
    std::pmr::unsynchronized_pool_resource memory;

    // Default-constructed tag: the shape a rejected parse() produces.
    auto missing = LocalizationCatalog::Create(
        LocalizationTableDesc{
            .entries = BaseEntries, .text = textSpan(TextBlob), .locale = LocaleTag{}},
        LocalizationCatalogConfig{}, memory);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, LocalizationErrorCode::InvalidLocaleTag);

    for (const std::string_view text : {std::string_view(""),
                                        std::string_view("-en"),
                                        std::string_view("en-"),
                                        std::string_view("en--US")}) {
        auto rejected = LocalizationCatalog::Create(baseDesc(text),
                                                    LocalizationCatalogConfig{}, memory);
        ASSERT_FALSE(rejected.has_value()) << text;
        EXPECT_EQ(rejected.error().code, LocalizationErrorCode::InvalidLocaleTag) << text;
    }

    // A valid tag is carried through unchanged.
    auto accepted = makeBase(memory, "zh-Hans-CN");
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    EXPECT_EQ(accepted->locale(), LocaleTag::parse("zh-Hans-CN"));
    EXPECT_EQ(accepted->locale().text(), "zh-Hans-CN");
}

// ---------------------------------------------------------------------------
// Storage ownership.
// ---------------------------------------------------------------------------

TEST(LocalizationCatalogTests, CatalogOwnsIngestedEntriesAndText)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::array<char, 5> mutableText{'L', 'i', 'v', 'e', 's'};
    std::array<LocalizationEntryDesc, 1> mutableEntries{{
        {.keyHash = localizationKeyHash(KeyLives), .textOffset = 0, .textLength = 5},
    }};

    auto catalog = LocalizationCatalog::Create(
        LocalizationTableDesc{.entries = mutableEntries,
                              .text = mutableText,
                              .locale = LocaleTag::parse("en")},
        LocalizationCatalogConfig{}, memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    // Scribble over the caller's storage. A catalog that aliased it would now be wrong.
    mutableText = {'X', 'X', 'X', 'X', 'X'};
    mutableEntries[0].keyHash = 0;
    mutableEntries[0].textLength = 1;

    const LocalizedTextId id = catalog->findTextId(KeyLives);
    ASSERT_TRUE(id.hasValue());
    EXPECT_EQ(catalog->resolveOr(id, "<none>"), "Lives");
    EXPECT_EQ(catalog->keyHashOf(id).value_or(0U), Core::stringKeyHash(KeyLives));
}

TEST(LocalizationEntryDescTests, ComparesMemberwise)
{
    constexpr LocalizationEntryDesc left{.keyHash = 7, .textOffset = 1, .textLength = 2};
    constexpr LocalizationEntryDesc same{.keyHash = 7, .textOffset = 1, .textLength = 2};
    constexpr LocalizationEntryDesc greater{.keyHash = 8, .textOffset = 0, .textLength = 0};
    static_assert(left == same);
    static_assert(left < greater);
    EXPECT_EQ(left, same);
    EXPECT_LT(left, greater);
}

} // namespace
} // namespace Tina::Localization
