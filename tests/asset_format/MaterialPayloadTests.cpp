#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

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
    EXPECT_EQ(MaterialWire::SchemaVersion, 2U);
    EXPECT_EQ(MaterialWire::HeaderBytes, 40U);

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
    EXPECT_EQ(view->alphaMode, MaterialAlphaMode::Opaque);
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
    };
    auto cooked = writeCookedMaterialAsset(materialId, desc);
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << (asset ? "" : asset.error().message);
    EXPECT_EQ(asset->header().dependencyCount, 3U);
    auto dep0 = asset->dependency(0);
    auto dep1 = asset->dependency(1);
    auto dep2 = asset->dependency(2);
    ASSERT_TRUE(dep0.has_value());
    ASSERT_TRUE(dep1.has_value());
    ASSERT_TRUE(dep2.has_value());
    EXPECT_EQ(dep0->assetId, baseColorId);
    EXPECT_EQ(dep1->assetId, mrId);
    EXPECT_EQ(dep2->assetId, normalId);
    EXPECT_EQ(dep0->expectedKind, AssetKind::Texture2D);

    auto view = parseMaterialPayload(asset->payload());
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_TRUE(view->hasBaseColorTexture);
    EXPECT_TRUE(view->hasMetallicRoughnessTexture);
    EXPECT_TRUE(view->hasNormalTexture);
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

TEST(MaterialPayloadTests, RejectsOneTextureSharedByMultipleRolesInV2)
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

    // v1 24-byte payloads must not parse as v2.
    std::vector<std::byte> v1(24U, std::byte{0});
    v1[0] = std::byte{1}; // schemaVersion=1 little-endian
    EXPECT_FALSE(parseMaterialPayload(v1).has_value());
}

TEST(MaterialPayloadTests, RejectsNonZeroReservedTail)
{
    auto ok = writeMaterialPayloadBytes(MaterialPayloadDesc{});
    ASSERT_TRUE(ok.has_value());
    (*ok)[32] = std::byte{1};
    EXPECT_FALSE(parseMaterialPayload(*ok).has_value());
}

} // namespace
} // namespace Tina::AssetFormat
