#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/StringKeyHash.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::AssetFormat {

// LocalizationTable cooked payload schema v1 (little-endian, after the CookedAsset header).
// One cooked asset carries exactly one locale, so switching language at runtime is an asset
// swap rather than a table rewrite.
//
// Layout:
//   u16  schemaVersion     (=1)
//   u16  localeTagLength   (1..MaximumLocaleTagBytes)
//   u32  entryCount        (1..MaximumEntryCount)
//   u32  textBytes         (0..MaximumTextBytes)
//   u32  reserved0         (=0)
//   char localeTag[LocaleTagBytes]  // NUL-padded; every byte at or past localeTagLength is zero
//   u32  reserved1         (=0)
//   LocalizationEntryWire[entryCount] (EntryBytes each):
//     u64 keyHash
//     u32 textOffset       // byte offset into the text blob
//     u32 textLength       // 0..MaximumValueBytes
//   char textBlob[textBytes]
//
// Entries are strictly increasing by keyHash, enforced by both the writer and the parser, so the
// runtime binary-searches the table instead of comparing strings. This mirrors how the cooked
// manifest enforces strictly ascending AssetId ordering.
//
// The key *text* is never stored: only its 64-bit hash is. That keeps authoring identifiers out of
// shipped payloads and makes lookup a pure integer compare. The cost is that a hash collision
// between two distinct authored keys is unrepresentable; because entries must strictly increase,
// such a collision surfaces as a duplicate-hash rejection at cook time rather than silently
// shadowing one of the two strings at runtime.
//
// Text values live in one concatenated UTF-8 blob referenced by {textOffset, textLength}. The
// writer packs values in entry order, but the parser only requires that each range lie inside the
// blob -- so a producer may point several entries at the same bytes to deduplicate repeated
// strings. An empty value is legal: a locale may deliberately blank a string.
namespace LocalizationTableWire {

inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 56;
inline constexpr Core::u32 EntryBytes = 16;

// Width of the inline locale tag field, including the NUL pad that keeps the field printable when
// read as a C string. MaximumLocaleTagBytes is therefore one less, matching the
// PrefabWire::NameBytes / MaximumNameBytes idiom.
inline constexpr Core::u32 LocaleTagBytes = 36;

// 35 is the longest well-formed BCP-47 (RFC 5646) tag this format needs to carry, derived rather
// than guessed: an 8-byte primary language subtag, a 4-byte script, a 3-byte region and two 8-byte
// variants, joined by four '-' separators -- 8+1+4+1+3+1+8+1+8. Extension ('-a-...') and
// private-use ('-x-...') subtags are deliberately out of range: they do not participate in locale
// matching, so a cooked string table has no use for them.
inline constexpr Core::u32 MaximumLocaleTagBytes = LocaleTagBytes - 1U;

// A 65535-entry cap bounds the entry block at 1 MiB and keeps every index representable in 16 bits
// for callers that want a compact side table. entryCount itself stays u32 so the field does not
// need to be repurposed if the cap is ever raised.
inline constexpr Core::u32 MaximumEntryCount = 65'535;

// Aggregate blob cap. A malformed header must never be able to ask for an unbounded allocation, and
// 8 MiB of UTF-8 is far more localized text than one locale of one game carries.
inline constexpr Core::u32 MaximumTextBytes = 8U * 1024U * 1024U;

// Per-value cap. Without it a single absurd string could consume the whole aggregate budget. 64 KiB
// is beyond any UI string, dialogue line or credits block.
inline constexpr Core::u32 MaximumValueBytes = 64U * 1024U;

// The key text is not a wire field, so this bounds the *cook input* rather than the payload. It
// lives beside the hash because both halves of the key contract must be identical in every
// producer for the hashes to agree.
inline constexpr Core::u32 MaximumKeyBytes = 256;

// FNV-1a 64-bit parameters. Chosen over the engine's ContentHash because ContentHash is a 128-bit
// runtime digest reached through Core::digestContentHash, and neither it nor the vendored xxHash is
// usable in a constant expression. FNV-1a is short enough to state exactly here, which is what
// makes the cooker and the runtime derive identical hashes.
inline constexpr Core::u64 KeyHashOffsetBasis = 14'695'981'039'346'656'037ULL;
inline constexpr Core::u64 KeyHashPrime = 1'099'511'628'211ULL;

// Both blocks start on an 8-byte boundary relative to the payload, so keyHash stays naturally
// aligned even though this schema decodes it byte-wise.
static_assert(HeaderBytes % 8U == 0U);
static_assert(EntryBytes % 8U == 0U);
// Header field offsets: the locale tag occupies [16, 16 + LocaleTagBytes) and reserved1 follows it.
static_assert(HeaderBytes == 16U + LocaleTagBytes + 4U);
static_assert(MaximumLocaleTagBytes == 35U);
// The advertised per-value cap has to be reachable within the aggregate cap.
static_assert(MaximumValueBytes <= MaximumTextBytes);
// A payload at every limit at once still fits the cooked payload budget.
static_assert(static_cast<Core::u64>(HeaderBytes) +
                  static_cast<Core::u64>(MaximumEntryCount) * EntryBytes +
                  static_cast<Core::u64>(MaximumTextBytes) <=
              Wire::MaxPayloadBytes);

} // namespace LocalizationTableWire

// The key->hash mapping is `Core::stringKeyHash`, not a function defined here. It has to be one
// definition shared with the runtime resolver, and `Tina::Localization` links Core only -- it cannot
// include this header. Two copies of the constants would each pass their own tests while making
// every runtime lookup miss silently.
[[nodiscard]] constexpr Core::u64 localizationKeyHash(std::string_view key) noexcept
{
    return Core::stringKeyHash(key);
}

struct LocalizationTableEntryDesc final {
    // Non-empty, at most MaximumKeyBytes, strict UTF-8 without NUL. Only its hash is written.
    std::string_view key{};
    // At most MaximumValueBytes, strict UTF-8 without NUL. May be empty.
    std::string_view text{};
};

struct LocalizationTablePayloadDesc final {
    std::string_view localeTag{};
    // Must already be sorted strictly ascending by localizationKeyHash(key).
    std::span<const LocalizationTableEntryDesc> entries{};
};

struct LocalizationTableEntryView final {
    Core::u64 keyHash = 0;
    Core::u32 textOffset = 0;
    Core::u32 textLength = 0;
};

struct LocalizationTablePayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u32 entryCount = 0;
    Core::u32 textBytes = 0;
    std::string_view localeTag{};
    std::span<const std::byte> entriesBytes{};
    std::string_view textBlob{};

    // The wire entry record is not layout-compatible with the decoded view, so entries are decoded
    // on demand rather than exposed as a zero-copy span.
    [[nodiscard]] std::optional<LocalizationTableEntryView> entry(Core::u32 index) const noexcept;

    // Value of one entry, or empty when index is out of range. A parsed view guarantees every range
    // is in bounds, so an empty result from a valid index is a genuinely empty value.
    [[nodiscard]] std::string_view text(Core::u32 index) const noexcept;

    // Binary search over the ascending keyHash order the schema guarantees. Returns nullopt when no
    // entry carries the hash, which is how an absent key is distinguished from an empty value.
    [[nodiscard]] std::optional<std::string_view> find(Core::u64 keyHash) const noexcept;
};

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeLocalizationTablePayloadBytes(const LocalizationTablePayloadDesc& desc);

// Borrows payload bytes. Every span and string_view on the returned view aliases into `payload`,
// which must outlive the view unchanged.
[[nodiscard]] Core::Result<LocalizationTablePayloadView>
parseLocalizationTablePayload(std::span<const std::byte> payload);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedLocalizationTableAsset(Core::AssetId assetId, const LocalizationTablePayloadDesc& desc,
                                  TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
