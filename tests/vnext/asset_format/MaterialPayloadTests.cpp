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
        .doubleSided = true,
        .alphaMode = MaterialAlphaMode::Opaque,
    };
    auto written = writeMaterialPayloadBytes(desc);
    ASSERT_TRUE(written.has_value()) << (written ? "" : written.error().message);
    EXPECT_EQ(written->size(), MaterialWire::HeaderBytes);

    auto view = parseMaterialPayload(*written);
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->schemaVersion, MaterialWire::SchemaVersion);
    EXPECT_EQ(view->model, MaterialModel::UnlitBaseColor);
    EXPECT_FLOAT_EQ(view->baseColorR, 0.95F);
    EXPECT_FLOAT_EQ(view->baseColorG, 0.24F);
    EXPECT_FLOAT_EQ(view->baseColorB, 0.30F);
    EXPECT_FLOAT_EQ(view->baseColorA, 1.0F);
    EXPECT_TRUE(view->doubleSided);
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
    };
    auto cooked = writeCookedMaterialAsset(materialId, desc);
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << (asset ? "" : asset.error().message);
    EXPECT_EQ(asset->header().assetKind, AssetKind::Material);
    EXPECT_EQ(asset->header().assetId, materialId);

    auto view = parseMaterialPayload(asset->payload());
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_FLOAT_EQ(view->baseColorR, 0.1F);
    EXPECT_FLOAT_EQ(view->baseColorA, 0.5F);
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

TEST(MaterialPayloadTests, RejectsOutOfRangeAndNonFinite)
{
    MaterialPayloadDesc high{.baseColorR = 1.5F};
    EXPECT_FALSE(writeMaterialPayloadBytes(high).has_value());

    MaterialPayloadDesc nanColor{.baseColorG = std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(writeMaterialPayloadBytes(nanColor).has_value());

    auto ok = writeMaterialPayloadBytes(MaterialPayloadDesc{});
    ASSERT_TRUE(ok.has_value());
    auto badSize = *ok;
    badSize.push_back(std::byte{0});
    EXPECT_FALSE(parseMaterialPayload(badSize).has_value());
}

} // namespace
} // namespace Tina::AssetFormat
