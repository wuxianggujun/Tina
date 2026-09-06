#pragma once

#include <tina/asset_format/LocalizationTablePayload.hpp>
#include <tina/localization/LocalizationCatalog.hpp>

#include <memory_resource>
#include <span>

namespace Tina::Asset {

// Bridge from a cooked LocalizationTable payload to a runtime LocalizationCatalog.
//
// It lives here, on the Asset side, and depends downward onto Tina::Localization the same way
// TileMapNavigation2D depends onto Tina::Navigation2D. Tina::Localization links Core only, so it
// cannot see AssetFormat; putting the adapter there would invert the dependency, and duplicating
// either side's key hash would make every runtime lookup miss silently (both already share
// Core::stringKeyHash for exactly that reason).

struct LocalizationCatalogLoadConfig final {
    // Fixed capacities handed to LocalizationCatalog::Create. Zero means "size to this table
    // exactly", which is the right default for a startup-only load: a caller who intends to
    // re-point the catalog at a larger locale later states the headroom explicitly instead of
    // discovering the limit on a language switch.
    Core::u32 entryCapacity = 0;
    Core::usize textByteCapacity = 0;
};

// Builds a catalog from an already-parsed payload view.
//
// The view borrows the cooked bytes; the returned catalog copies what it needs, so the payload may
// be released afterwards.
//
// Absent-translation entries cannot arrive this way. Localization's AbsentTextOffset sentinel
// (0xFFFFFFFF) is only reachable through the hand-built LocalizationTableDesc API, because the wire
// validator rejects any textOffset past textBytes -- so no cooked payload can express "key exists,
// this locale has no translation". A locale that means to blank a string authors an empty value,
// which resolves successfully as an empty string rather than failing with MissingText.
[[nodiscard]] Core::Result<Localization::LocalizationCatalog>
loadLocalizationCatalogFromPayloadView(
    const AssetFormat::LocalizationTablePayloadView& view,
    LocalizationCatalogLoadConfig config = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

// Parses cooked payload bytes and builds the catalog in one step. Payload defects surface as
// AssetFormat errors, table defects as Localization errors, so a caller can tell a corrupt file
// from a well-formed table it refused to accept.
[[nodiscard]] Core::Result<Localization::LocalizationCatalog>
loadLocalizationCatalogFromPayload(
    std::span<const std::byte> payload,
    LocalizationCatalogLoadConfig config = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

} // namespace Tina::Asset
