#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/hash/StringKeyHash.hpp>

#include <array>
#include <compare>
#include <limits>
#include <string_view>

namespace Tina::Localization {

class LocalizationCatalog;

namespace LocalizedTextContract {

// BCP 47 tags used by shipping games stay far below this; 35 bytes covers
// language-extlang-script-region-variant without allocating.
inline constexpr Core::usize MaximumLocaleTagLength = 35;

} // namespace LocalizedTextContract

// The cooked table stores 64-bit key hashes, not key strings, so the cooker and the runtime must
// agree on one function. That function is `Core::stringKeyHash`, and this is a thin forwarder so
// call sites in this module read in module terms.
//
// It deliberately does NOT live here. This module links Core only and cannot see
// `<tina/asset_format/LocalizationTablePayload.hpp>`, while the cooker cannot see this header
// either -- so a hash defined on either side would be a second copy. Two copies compile, pass their
// own tests, and then make every findTextId() call miss silently: the table stays well-formed,
// resolve() is never reached, and the game renders key names. Core is the one place both can reach.
[[nodiscard]] constexpr Core::u64 localizationKeyHash(std::string_view key) noexcept
{
    return Core::stringKeyHash(key);
}

// Inline, allocation-free locale tag. Byte-exact comparison: canonical casing is the cooker's job,
// so "en-US" and "en-us" are two different tags here rather than being silently folded together.
class LocaleTag final {
public:
    constexpr LocaleTag() noexcept = default;

    // Accepts the ASCII BCP 47 shape only: alphanumeric runs joined by single '-'. Rejects an
    // empty tag, a leading/trailing '-', a doubled '-', anything non-ASCII, and anything longer
    // than MaximumLocaleTagLength.
    [[nodiscard]] static constexpr bool isValidText(std::string_view text) noexcept
    {
        if (text.empty() || text.size() > LocalizedTextContract::MaximumLocaleTagLength) {
            return false;
        }
        bool previousWasSeparator = true;
        for (const char value : text) {
            if (value == '-') {
                if (previousWasSeparator) {
                    return false;
                }
                previousWasSeparator = true;
                continue;
            }
            if (!isAlphanumeric(value)) {
                return false;
            }
            previousWasSeparator = false;
        }
        return !previousWasSeparator;
    }

    [[nodiscard]] static constexpr LocaleTag parse(std::string_view text) noexcept
    {
        if (!isValidText(text)) {
            return LocaleTag{};
        }
        LocaleTag tag;
        for (Core::usize index = 0; index < text.size(); ++index) {
            tag.m_text[index] = text[index];
        }
        tag.m_length = static_cast<Core::u8>(text.size());
        return tag;
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept { return m_length != 0; }
    explicit constexpr operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] constexpr std::string_view text() const noexcept
    {
        return std::string_view(m_text.data(), m_length);
    }

    auto operator<=>(const LocaleTag&) const = default;

private:
    [[nodiscard]] static constexpr bool isAlphanumeric(char value) noexcept
    {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
            || (value >= '0' && value <= '9');
    }

    std::array<char, LocalizedTextContract::MaximumLocaleTagLength> m_text{};
    Core::u8 m_length = 0;
};

// Opaque handle to one localized string, produced by resolving a stable key against a catalog.
//
// Why the indirection instead of returning a string_view straight from a key: the slot number is
// what the id means, and a catalog is free to re-point what a slot's text is without any public
// signature changing. A future runtime language switch therefore keeps every id already handed
// out to gameplay code -- it rebuilds the slot->text mapping and the text blob in place, leaving
// the key hash for each slot alone. A key the new locale drops resolves to
// LocalizationErrorCode::MissingText, which is exactly the fail-closed behaviour a startup-only
// load already produces. Nothing here is switch-specific and no opt-in flag exists; the shape
// simply does not rule the switch out.
//
// The catalog identity is what stops an id from one catalog resolving against another. Identities
// come from a process-wide counter and are never recycled, so an id outliving its catalog stays
// invalid instead of aliasing a later one.
class LocalizedTextId final {
public:
    inline static constexpr Core::u32 InvalidSlot = (std::numeric_limits<Core::u32>::max)();

    constexpr LocalizedTextId() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_catalogIdentity != 0 && m_slot != InvalidSlot;
    }
    explicit constexpr operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] constexpr Core::u32 catalogIdentity() const noexcept { return m_catalogIdentity; }
    [[nodiscard]] constexpr Core::u32 slot() const noexcept { return m_slot; }

    auto operator<=>(const LocalizedTextId&) const = default;

private:
    friend class LocalizationCatalog;

    constexpr LocalizedTextId(Core::u32 catalogIdentity, Core::u32 slot) noexcept
        : m_catalogIdentity(catalogIdentity), m_slot(slot)
    {
    }

    Core::u32 m_catalogIdentity = 0;
    Core::u32 m_slot = InvalidSlot;
};

} // namespace Tina::Localization
