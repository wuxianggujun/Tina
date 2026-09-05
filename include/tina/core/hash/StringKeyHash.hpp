#pragma once

#include <tina/core/base/Types.hpp>

#include <string_view>

namespace Tina::Core {

// FNV-1a 64 over raw bytes, for stable identifier keys that are hashed by a cooker and looked up at
// runtime. This lives in Core because it is a *contract* between two layers that must not depend on
// each other: `Tina::AssetFormat` writes the hash into a cooked payload, and modules such as
// `Tina::Localization` (which links Core only, never Asset) resolve keys against it. Two copies of
// these constants would compile and pass their own tests while making every cross-layer lookup miss
// silently, so there is exactly one definition and both sides include it.
//
// Not ContentHash: that is a 128-bit XXH3 digest reached through `Core::digestContentHash`, returns
// a `Result`, and is not usable in a constant expression. This is a plain integer key hash with no
// failure mode, deliberately `constexpr` so a cooker can derive sort order at compile time.
//
// Not for security, not for content addressing, and not stable across a change to these constants:
// changing either value is a cooked-data migration, exactly like bumping a payload SchemaVersion.
namespace StringKeyHashContract {

inline constexpr u64 OffsetBasis = 14'695'981'039'346'656'037ULL;
inline constexpr u64 Prime = 1'099'511'628'211ULL;

} // namespace StringKeyHashContract

[[nodiscard]] constexpr u64 stringKeyHash(std::string_view key) noexcept
{
    u64 hash = StringKeyHashContract::OffsetBasis;
    for (const char character : key)
    {
        hash ^= static_cast<u64>(static_cast<unsigned char>(character));
        hash *= StringKeyHashContract::Prime;
    }
    return hash;
}

} // namespace Tina::Core
