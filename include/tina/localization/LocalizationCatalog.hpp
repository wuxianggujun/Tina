#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/localization/LocalizedText.hpp>

#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Localization {

namespace LocalizationCatalogContract {

// A slot index must fit in LocalizedTextId's u32 with InvalidSlot still reserved.
inline constexpr Core::u32 MaximumEntryCount = 1U << 22U;
inline constexpr Core::usize MaximumTextBytes = Core::usize{64} * 1024U * 1024U;
// Absent-translation encoding: a valid key whose text this locale does not carry. Chosen over
// length 0 because an empty translation is a legitimate authored value, and over a separate flag
// byte because the cooked entry is fixed-width.
inline constexpr Core::u32 AbsentTextOffset = (0xFFFFFFFFU);

} // namespace LocalizationCatalogContract

// One cooked table entry. Plain POD so this module compiles against Core alone: the Asset-side
// bridge that adapts AssetFormat::LocalizationTablePayloadView into a span of these is deliberately
// not here, and depends downward onto this module the way the navigation bridge does.
struct LocalizationEntryDesc final {
    Core::u64 keyHash = 0;
    // Byte offset into the text blob, or LocalizationCatalogContract::AbsentTextOffset.
    Core::u32 textOffset = 0;
    Core::u32 textLength = 0;

    auto operator<=>(const LocalizationEntryDesc&) const = default;
};

struct LocalizationTableDesc final {
    // Strictly ascending by keyHash. Binary search depends on it, so it is validated, not assumed.
    std::span<const LocalizationEntryDesc> entries{};
    // Text blob the entries slice. UTF-8, not NUL-terminated.
    std::span<const char> text{};
    LocaleTag locale{};
};

struct LocalizationCatalogConfig final {
    // Fixed capacities. Storage is reserved once at Create() and never grows: an ingested table
    // that does not fit is LocalizationErrorCode::CapacityExceeded, never a bigger allocation.
    Core::u32 entryCapacity = 1024;
    Core::usize textByteCapacity = Core::usize{256} * 1024U;
};

// Immutable localized string table for one locale, chosen once at load time.
//
// Owner-thread construction, then read-only: resolve() is const and allocation-free, so any number
// of threads may resolve concurrently. A future in-place re-point for a runtime language switch is
// the one operation that would need exclusive access, and it needs no signature change to add.
class LocalizationCatalog final {
public:
    [[nodiscard]] static Core::Result<LocalizationCatalog> Create(
        const LocalizationTableDesc& desc,
        LocalizationCatalogConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~LocalizationCatalog() noexcept = default;

    LocalizationCatalog(const LocalizationCatalog&) = delete;
    LocalizationCatalog& operator=(const LocalizationCatalog&) = delete;
    LocalizationCatalog(LocalizationCatalog&& other) noexcept;
    // Deleted for the same reason as Navigation2D's: a move-assigned-over catalog would keep its
    // old identity alive in ids already handed out while holding another table's slots.
    LocalizationCatalog& operator=(LocalizationCatalog&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] LocaleTag locale() const noexcept { return m_locale; }
    [[nodiscard]] Core::u32 catalogIdentity() const noexcept { return m_identity; }
    [[nodiscard]] Core::u32 entryCount() const noexcept { return static_cast<Core::u32>(m_entries.size()); }
    [[nodiscard]] Core::u32 entryCapacity() const noexcept { return m_entryCapacity; }
    [[nodiscard]] Core::usize textByteCount() const noexcept { return m_text.size(); }
    [[nodiscard]] Core::usize textByteCapacity() const noexcept { return m_textByteCapacity; }
    // Bumped whenever slot->text is re-pointed. Always 1 for a startup-only catalog; a runtime
    // switch would advance it so a caller caching resolved views can tell they went stale.
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }

    // Cold path: interning a key. Returns a default LocalizedTextId when the key is absent from
    // the table -- an explicit invalid handle, never an empty string that looks like a hit.
    [[nodiscard]] LocalizedTextId findTextId(std::string_view key) const noexcept;
    // Same, for a caller that already holds the cooked hash and must not depend on
    // localizationKeyHash().
    [[nodiscard]] LocalizedTextId findTextIdByKeyHash(Core::u64 keyHash) const noexcept;

    // Hot path: O(1), no allocation, no hashing, no search. The slot in the id indexes storage
    // directly; only the catalog identity and bounds are checked.
    //
    // Fails with MissingText for a valid id whose locale carries no translation, and with
    // InvalidTextId for a default id or one minted by another catalog. The returned view is owned
    // by this catalog and stays valid for as long as it lives and its revision() is unchanged.
    [[nodiscard]] Core::Result<std::string_view> resolve(LocalizedTextId id) const noexcept;

    // Convenience for call sites that have a fallback of their own. Returns the fallback for every
    // case resolve() fails, so it cannot distinguish an unknown key from an absent translation.
    [[nodiscard]] std::string_view resolveOr(LocalizedTextId id, std::string_view fallback) const noexcept;

    [[nodiscard]] bool contains(LocalizedTextId id) const noexcept;
    // Key hash behind a slot. Stable across a future re-point, which is what lets a switch rebuild
    // the text without re-interning anything.
    [[nodiscard]] Core::Result<Core::u64> keyHashOf(LocalizedTextId id) const noexcept;

private:
    struct Slot final {
        Core::u64 keyHash = 0;
        Core::u32 textOffset = 0;
        Core::u32 textLength = 0;
        bool present = false;
    };

    LocalizationCatalog(Core::u32 identity,
                        LocaleTag locale,
                        std::pmr::vector<Slot> entries,
                        std::pmr::vector<char> text,
                        Core::u32 entryCapacity,
                        Core::usize textByteCapacity) noexcept;

    [[nodiscard]] const Slot* slotFor(LocalizedTextId id) const noexcept;

    Core::u32 m_identity = 0;
    LocaleTag m_locale{};
    // Ascending by keyHash, so findTextIdByKeyHash() is a binary search.
    std::pmr::vector<Slot> m_entries;
    std::pmr::vector<char> m_text;
    Core::u32 m_entryCapacity = 0;
    Core::usize m_textByteCapacity = 0;
    Core::u64 m_revision = 1;
};

} // namespace Tina::Localization
