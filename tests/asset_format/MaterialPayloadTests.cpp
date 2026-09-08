#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <limits>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xE4U);
    return bytes;
}

TEST(MaterialPayloadTests, UnlitBaseColorRoundTrip)
{
    const MaterialPayloadDesc desc{
        .model = MaterialModel::UnlitBaseColor,
        .baseColorR = 0.95F,
        .baseColorG = 0.24F,
        .baseColorB = 0.30F,
        .baseColorA = 1.0F,
        .metallicFactor = 0.25F,
        .roughnessFactor = 0.75F,
        .doubleSided = true,
        .alphaMode = MaterialAlphaMode::Opaque,
    };
    auto written = writeMaterialPayloadBytes(desc);
    ASSERT_TRUE(written.has_value()) << (written ? "" : written.error().message);
    EXPECT_EQ(written->size(), MaterialWire::HeaderBytes);
    EXPECT_EQ(MaterialWire::SchemaVersion, 3U);
    EXPECT_EQ(MaterialWire::HeaderBytes, 48U);

    auto view = parseMaterialPayload(*written);
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->schemaVersion, MaterialWire::SchemaVersion);
    EXPECT_EQ(view->model, MaterialModel::UnlitBaseColor);
    EXPECT_FLOAT_EQ(view->baseColorR, 0.95F);
    EXPECT_FLOAT_EQ(view->baseColorG, 0.24F);
    EXPECT_FLOAT_EQ(view->baseColorB, 0.30F);
    EXPECT_FLOAT_EQ(view->baseColorA, 1.0F);
    EXPECT_FLOAT_EQ(view->metallicFactor, 0.25F);
    EXPECT_FLOAT_EQ(view->roughnessFactor, 0.75F);
    EXPECT_TRUE(view->doubleSided);
    EXPECT_FALSE(view->hasBaseColorTexture);
    EXPECT_FALSE(view->hasMetallicRoughnessTexture);
    EXPECT_FALSE(view->hasNormalTexture);
    EXPECT_FALSE(view->hasEmissiveTexture);
    EXPECT_EQ(view->textureDependencyCount(), 0U);
    EXPECT_EQ(view->alphaMode, MaterialAlphaMode::Opaque);
    EXPECT_FLOAT_EQ(view->alphaCutoff, 0.5F);
    EXPECT_FLOAT_EQ(view->emissiveFactorR, 0.0F);
    EXPECT_FLOAT_EQ(view->emissiveFactorG, 0.0F);
    EXPECT_FLOAT_EQ(view->emissiveFactorB, 0.0F);
}

TEST(MaterialPayloadTests, CookedMaterialRoundTrip)
{
    const auto materialId = *Core::AssetId::fromBytes(idBytes(0x41));
    const MaterialPayloadDesc desc{
        .baseColorR = 0.1F,
        .baseColorG = 0.2F,
        .baseColorB = 0.3F,
        .baseColorA = 0.5F,
        .metallicFactor = 0.0F,
        .roughnessFactor = 1.0F,
    };
    auto cooked = writeCookedMaterialAsset(materialId, desc);
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << (asset ? "" : asset.error().message);
    EXPECT_EQ(asset->header().assetKind, AssetKind::Material);
    EXPECT_EQ(asset->header().assetId, materialId);
    EXPECT_EQ(asset->header().assetTypeVersion, MaterialWire::SchemaVersion);
    EXPECT_EQ(asset->header().dependencyCount, 0U);

    auto view = parseMaterialPayload(asset->payload());
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_FLOAT_EQ(view->baseColorR, 0.1F);
    EXPECT_FLOAT_EQ(view->baseColorA, 0.5F);
    EXPECT_FLOAT_EQ(view->metallicFactor, 0.0F);
    EXPECT_FLOAT_EQ(view->roughnessFactor, 1.0F);
    EXPECT_FALSE(view->hasBaseColorTexture);
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

TEST(MaterialPayloadTests, CookedMaterialWithTextureDependencies)
{
    const auto materialId = *Core::AssetId::fromBytes(idBytes(0x42));
    const auto baseColorId = *Core::AssetId::fromBytes(idBytes(0x43));
    const auto mrId = *Core::AssetId::fromBytes(idBytes(0x44));
    const auto normalId = *Core::AssetId::fromBytes(idBytes(0x45));
    const auto emissiveId = *Core::AssetId::fromBytes(idBytes(0x46));
    const MaterialPayloadDesc desc{
        .baseColorR = 1.0F,
        .baseColorG = 1.0F,
        .baseColorB = 1.0F,
        .baseColorA = 1.0F,
        .metallicFactor = 0.5F,
        .roughnessFactor = 0.4F,
        .baseColorTextureId = baseColorId,
        .metallicRoughnessTextureId = mrId,
        .normalTextureId = normalId,
        .emissiveTextureId = emissiveId,
    };
    auto cooked = writeCookedMaterialAsset(materialId, desc);
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << (asset ? "" : asset.error().message);
    EXPECT_EQ(asset->header().dependencyCount, 4U);
    auto dep0 = asset->dependency(0);
    auto dep1 = asset->dependency(1);
    auto dep2 = asset->dependency(2);
    auto dep3 = asset->dependency(3);
    ASSERT_TRUE(dep0.has_value());
    ASSERT_TRUE(dep1.has_value());
    ASSERT_TRUE(dep2.has_value());
    ASSERT_TRUE(dep3.has_value());
    EXPECT_EQ(dep0->assetId, baseColorId);
    EXPECT_EQ(dep1->assetId, mrId);
    EXPECT_EQ(dep2->assetId, normalId);
    EXPECT_EQ(dep3->assetId, emissiveId);
    EXPECT_EQ(dep3->expectedKind, AssetKind::Texture2D);
    EXPECT_EQ(dep3->flags, DependencyFlags::Required);
    EXPECT_EQ(dep0->expectedKind, AssetKind::Texture2D);

    auto view = parseMaterialPayload(asset->payload());
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_TRUE(view->hasBaseColorTexture);
    EXPECT_TRUE(view->hasMetallicRoughnessTexture);
    EXPECT_TRUE(view->hasNormalTexture);
    EXPECT_TRUE(view->hasEmissiveTexture);
    EXPECT_EQ(view->textureDependencyCount(), 4U);
    EXPECT_FLOAT_EQ(view->metallicFactor, 0.5F);
    EXPECT_FLOAT_EQ(view->roughnessFactor, 0.4F);
}

TEST(MaterialPayloadTests, RejectsTextureRoleAssetIdsThatAreNotStrictlyIncreasing)
{
    const auto materialId = *Core::AssetId::fromBytes(idBytes(0x50));
    const auto baseColorId = *Core::AssetId::fromBytes(idBytes(0x53));
    const auto mrId = *Core::AssetId::fromBytes(idBytes(0x51));
    const auto normalId = *Core::AssetId::fromBytes(idBytes(0x52));
    auto cooked = writeCookedMaterialAsset(materialId, MaterialPayloadDesc{
                                                           .baseColorTextureId = baseColorId,
                                                           .metallicRoughnessTextureId = mrId,
                                                           .normalTextureId = normalId,
                                                       });
    ASSERT_FALSE(cooked.has_value());
    EXPECT_EQ(cooked.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(MaterialPayloadTests, RejectsOneTextureSharedByMultipleRoles)
{
    const auto materialId = *Core::AssetId::fromBytes(idBytes(0x60));
    const auto sharedTextureId = *Core::AssetId::fromBytes(idBytes(0x61));
    auto cooked = writeCookedMaterialAsset(materialId, MaterialPayloadDesc{
                                                           .baseColorTextureId = sharedTextureId,
                                                           .metallicRoughnessTextureId = sharedTextureId,
                                                           .normalTextureId = sharedTextureId,
                                                       });
    ASSERT_FALSE(cooked.has_value());
    EXPECT_EQ(cooked.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(MaterialPayloadTests, DefaultsMetallicRoughnessToOne)
{
    auto written = writeMaterialPayloadBytes(MaterialPayloadDesc{});
    ASSERT_TRUE(written.has_value());
    auto view = parseMaterialPayload(*written);
    ASSERT_TRUE(view.has_value());
    EXPECT_FLOAT_EQ(view->metallicFactor, 1.0F);
    EXPECT_FLOAT_EQ(view->roughnessFactor, 1.0F);
}

TEST(MaterialPayloadTests, RejectsOutOfRangeAndNonFinite)
{
    MaterialPayloadDesc high{.baseColorR = 1.5F};
    EXPECT_FALSE(writeMaterialPayloadBytes(high).has_value());

    MaterialPayloadDesc nanColor{.baseColorG = std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(writeMaterialPayloadBytes(nanColor).has_value());

    MaterialPayloadDesc badMetal{.metallicFactor = 1.5F};
    EXPECT_FALSE(writeMaterialPayloadBytes(badMetal).has_value());

    MaterialPayloadDesc badRough{.roughnessFactor = -0.1F};
    EXPECT_FALSE(writeMaterialPayloadBytes(badRough).has_value());

    auto ok = writeMaterialPayloadBytes(MaterialPayloadDesc{});
    ASSERT_TRUE(ok.has_value());
    auto badSize = *ok;
    badSize.push_back(std::byte{0});
    EXPECT_FALSE(parseMaterialPayload(badSize).has_value());

    // Old layouts are never sniffed or upgraded by the current reader.
    std::vector<std::byte> v1(24U, std::byte{0});
    v1[0] = std::byte{1}; // schemaVersion=1 little-endian
    EXPECT_FALSE(parseMaterialPayload(v1).has_value());
    std::vector<std::byte> v2(40U, std::byte{0});
    v2[0] = std::byte{2};
    EXPECT_FALSE(parseMaterialPayload(v2).has_value());
    auto disguisedV2 = *ok;
    disguisedV2[0] = std::byte{2};
    EXPECT_FALSE(parseMaterialPayload(disguisedV2).has_value());
}

TEST(MaterialPayloadTests, MaskPreservesCutoffAndHdrEmissiveRadiance)
{
    const MaterialPayloadDesc desc{
        .baseColorA = 0.25F,
        .alphaMode = MaterialAlphaMode::Mask,
        .alphaCutoff = 1.25F,
        .emissiveFactorR = 8.0F,
        .emissiveFactorG = 2.5F,
        .emissiveFactorB = 0.0F,
    };
    auto bytes = writeMaterialPayloadBytes(desc);
    ASSERT_TRUE(bytes) << bytes.error().message;
    auto view = parseMaterialPayload(*bytes);
    ASSERT_TRUE(view) << view.error().message;
    EXPECT_EQ(view->alphaMode, MaterialAlphaMode::Mask);
    EXPECT_FLOAT_EQ(view->baseColorA, 0.25F);
    EXPECT_FLOAT_EQ(view->alphaCutoff, 1.25F);
    EXPECT_FLOAT_EQ(view->emissiveFactorR, 8.0F);
    EXPECT_FLOAT_EQ(view->emissiveFactorG, 2.5F);
    EXPECT_FLOAT_EQ(view->emissiveFactorB, 0.0F);
}

TEST(MaterialPayloadTests, EveryTextureRoleSubsetUsesCompactRequiredDependencyOrder)
{
    const auto materialId = *Core::AssetId::fromBytes(idBytes(0x40));
    const std::array roleIds{
        *Core::AssetId::fromBytes(idBytes(0x41)), *Core::AssetId::fromBytes(idBytes(0x42)),
        *Core::AssetId::fromBytes(idBytes(0x43)), *Core::AssetId::fromBytes(idBytes(0x44)),
    };
    for (Core::u32 mask = 0; mask < (1U << MaterialWire::TextureRoleCount); ++mask)
    {
        SCOPED_TRACE(mask);
        MaterialPayloadDesc desc{};
        const std::array fields{&desc.baseColorTextureId, &desc.metallicRoughnessTextureId,
                                 &desc.normalTextureId, &desc.emissiveTextureId};
        std::vector<Core::AssetId> expectedIds;
        for (Core::u32 role = 0; role < fields.size(); ++role)
        {
            if ((mask & (1U << role)) != 0)
            {
                *fields[role] = roleIds[role];
                expectedIds.push_back(roleIds[role]);
            }
        }
        auto bytes = writeCookedMaterialAsset(materialId, desc);
        ASSERT_TRUE(bytes) << bytes.error().message;
        auto asset = parseCookedAssetView(*bytes);
        ASSERT_TRUE(asset) << asset.error().message;
        auto view = parseMaterialPayload(asset->payload());
        ASSERT_TRUE(view) << view.error().message;
        EXPECT_EQ(view->textureDependencyCount(), expectedIds.size());
        EXPECT_EQ(view->hasEmissiveTexture, (mask & MaterialWire::FlagHasEmissiveTexture) != 0);
        ASSERT_EQ(asset->header().dependencyCount, expectedIds.size());
        for (Core::u32 index = 0; index < expectedIds.size(); ++index)
        {
            const auto dependency = asset->dependency(index);
            ASSERT_TRUE(dependency);
            EXPECT_EQ(dependency->assetId, expectedIds[index]);
            EXPECT_EQ(dependency->flags, DependencyFlags::Required);
            EXPECT_EQ(dependency->expectedKind, AssetKind::Texture2D);
        }
        EXPECT_TRUE(verifyCookedAssetContentHash(*asset));
    }
}

TEST(MaterialPayloadTests, ProducerAndParserRejectInvalidCutoffAndEmissiveFactors)
{
    const std::array fields{&MaterialPayloadDesc::alphaCutoff, &MaterialPayloadDesc::emissiveFactorR,
                             &MaterialPayloadDesc::emissiveFactorG, &MaterialPayloadDesc::emissiveFactorB};
    constexpr std::array<Core::usize, 4> offsets{32U, 36U, 40U, 44U};
    auto canonical = writeMaterialPayloadBytes(MaterialPayloadDesc{});
    ASSERT_TRUE(canonical);
    for (Core::usize index = 0; index < fields.size(); ++index)
    {
        SCOPED_TRACE(index);
        for (const float value : {-0.01F, std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN()})
        {
            MaterialPayloadDesc desc{};
            desc.*fields[index] = value;
            EXPECT_FALSE(writeMaterialPayloadBytes(desc));
            auto bytes = *canonical;
            const auto bits = std::bit_cast<Core::u32>(value);
            for (Core::usize byteIndex = 0; byteIndex < sizeof(bits); ++byteIndex)
            {
                bytes[offsets[index] + byteIndex] =
                    static_cast<std::byte>((bits >> (byteIndex * 8U)) & 0xFFU);
            }
            EXPECT_FALSE(parseMaterialPayload(bytes));
        }
    }
}

} // namespace
} // namespace Tina::AssetFormat
