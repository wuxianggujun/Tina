#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/id/AssetId.hpp>

#include <cstddef>
#include <string_view>

namespace Tina::Asset::Detail {

// Changing this value is an intentional identity migration. Importer contracts
// must bump their version at the same time so old metadata cannot be reused.
inline constexpr Core::u32 DefaultAssetIdDerivationVersion = 2U;

// Role tags are ordered where a cooked dependency list requires it. The hash input
// also contains the semantic AssetKind, ordinal, and channel, so the tag is a stable
// ordering/namespace byte rather than the identity itself.
//
// In practice the tag does most of the separating: outputs of one importer differ
// from each other by tag alone, and outputs of different importers additionally
// differ by locator (the file extension is part of it). Removing AssetKind or channel
// from the hash does not currently make any test fail -- they are defence in depth,
// not the discriminator. Treat that as a reason to keep tags unique, not as licence
// to drop inputs.
//
// Two values below are deliberately reused across importers, which is safe only
// because the remaining inputs differ:
//   0x75  glTF metallic-roughness texture  /  imported texture media
//         Both are AssetKind::Texture2D, so only `channel` (1 vs 0) separates them
//         when the locator matches byte for byte.
//   0x77  glTF animation clip  /  imported audio media
//         Separated by AssetKind (AnimationClip3D vs AudioClip).
// A new producer that reuses a tag *and* matches the other's kind/channel/ordinal
// would collide. Prefer an unused value; the corpus tests in GltfCookTests and
// MediaCookTests pin the two pairs above but cannot anticipate a third.
inline constexpr Core::u8 GltfMeshAssetIdTag = 0x71U;
inline constexpr Core::u8 GltfMaterialAssetIdTag = 0x72U;
inline constexpr Core::u8 GltfPrefabAssetIdTag = 0x73U;
inline constexpr Core::u8 GltfBaseColorTextureAssetIdTag = 0x74U;
inline constexpr Core::u8 GltfMetallicRoughnessTextureAssetIdTag = 0x75U;
inline constexpr Core::u8 GltfNormalTextureAssetIdTag = 0x76U;
inline constexpr Core::u8 GltfAnimationAssetIdTag = 0x77U;
// Shares 0x75 with GltfMetallicRoughnessTextureAssetIdTag; see the note above.
inline constexpr Core::u8 TextureMediaAssetIdTag = 0x75U;
// Shares 0x77 with GltfAnimationAssetIdTag; see the note above.
inline constexpr Core::u8 AudioMediaAssetIdTag = 0x77U;

[[nodiscard]] inline Core::AssetId deriveVersionedAssetId(
    std::string_view canonicalLocator,
    AssetFormat::AssetKind assetKind,
    Core::u8 roleTag,
    Core::u32 outputOrdinal,
    Core::u32 channel = 0U) noexcept
{
    constexpr Core::u64 FnvOffset = 14695981039346656037ULL;
    constexpr Core::u64 FnvPrime = 1099511628211ULL;

    Core::u64 low = FnvOffset ^ 0x54494E4149445632ULL;  // "TINAIDV2"
    Core::u64 high = FnvOffset ^ 0x32564449414E4954ULL; // "2VDINAIT"

    const auto mixByte = [](Core::u64& state, Core::u8 value) noexcept {
        state ^= static_cast<Core::u64>(value);
        state *= FnvPrime;
    };
    const auto mixU16 = [&mixByte](Core::u64& state, Core::u16 value) noexcept {
        mixByte(state, static_cast<Core::u8>(value & 0xFFU));
        mixByte(state, static_cast<Core::u8>((value >> 8U) & 0xFFU));
    };
    const auto mixU32 = [&mixByte](Core::u64& state, Core::u32 value) noexcept {
        for (Core::u32 shift = 0U; shift < 32U; shift += 8U)
        {
            mixByte(state, static_cast<Core::u8>((value >> shift) & 0xFFU));
        }
    };
    const auto mixU64 = [&mixByte](Core::u64& state, Core::u64 value) noexcept {
        for (Core::u32 shift = 0U; shift < 64U; shift += 8U)
        {
            mixByte(state, static_cast<Core::u8>((value >> shift) & 0xFFU));
        }
    };
    const auto mixInput = [&](Core::u64& state) noexcept {
        mixU32(state, DefaultAssetIdDerivationVersion);
        mixU16(state, static_cast<Core::u16>(assetKind));
        mixByte(state, roleTag);
        mixU32(state, outputOrdinal);
        mixU32(state, channel);
        mixU64(state, static_cast<Core::u64>(canonicalLocator.size()));
        for (const char character : canonicalLocator)
        {
            mixByte(state, static_cast<Core::u8>(static_cast<unsigned char>(character)));
        }
    };

    mixInput(low);
    // A distinct second stream prevents the 128-bit result from being two
    // trivially related copies of one accumulator.
    high ^= low;
    mixInput(high);

    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(roleTag);
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        bytes[1U + index] = static_cast<std::byte>(
            static_cast<Core::u8>((low >> (index * 8U)) & 0xFFU));
    }
    for (std::size_t index = 0U; index < 7U; ++index)
    {
        bytes[9U + index] = static_cast<std::byte>(
            static_cast<Core::u8>((high >> (index * 8U)) & 0xFFU));
    }
    return *Core::AssetId::fromBytes(bytes);
}

} // namespace Tina::Asset::Detail

