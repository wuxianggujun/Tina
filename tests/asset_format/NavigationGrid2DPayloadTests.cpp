#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/NavigationGrid2DPayload.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] NavigationGrid2DPayloadDesc validDesc(
    const std::array<Core::u8, 6>& flags,
    const std::array<Core::u8, 6>& costs) noexcept
{
    return {
        .widthCells = 3,
        .heightCells = 2,
        .originXMeters = -2.0F,
        .originYMeters = 4.0F,
        .cellSizeMeters = 0.5F,
        .cellFlags = flags,
        .traversalCosts = costs,
    };
}

TEST(NavigationGrid2DPayloadTests, RoundTripsRowMajorTablesAndCookedIdentity)
{
    const std::array<Core::u8, 6> flags{0, 1, 0, 1, 0, 0};
    const std::array<Core::u8, 6> costs{1, 1, 5, 1, 16, 2};
    auto payload = writeNavigationGrid2DPayloadBytes(validDesc(flags, costs));
    ASSERT_TRUE(payload) << payload.error().message;
    ASSERT_EQ(payload->size(), NavigationGrid2DWire::HeaderBytes + 12U);

    auto view = parseNavigationGrid2DPayload(*payload);
    ASSERT_TRUE(view) << view.error().message;
    EXPECT_EQ(view->widthCells, 3U);
    EXPECT_EQ(view->heightCells, 2U);
    EXPECT_FLOAT_EQ(view->originXMeters, -2.0F);
    EXPECT_FLOAT_EQ(view->originYMeters, 4.0F);
    EXPECT_FLOAT_EQ(view->cellSizeMeters, 0.5F);
    EXPECT_TRUE(std::ranges::equal(view->cellFlags, flags));
    EXPECT_TRUE(std::ranges::equal(view->traversalCosts, costs));

    const Core::AssetId id = assetId(7U);
    auto cooked = writeCookedNavigationGrid2DAsset(id, validDesc(flags, costs));
    ASSERT_TRUE(cooked) << cooked.error().message;
    auto file = parseCookedAssetView(*cooked);
    ASSERT_TRUE(file) << file.error().message;
    EXPECT_EQ(file->header().assetKind, AssetKind::NavigationGrid2D);
    EXPECT_EQ(file->header().assetId, id);
    EXPECT_EQ(file->header().dependencyCount, 0U);
    EXPECT_TRUE(verifyCookedAssetContentHash(*file));
}

TEST(NavigationGrid2DPayloadTests, RejectsInvalidLayoutTablesAndReservedFields)
{
    const std::array<Core::u8, 6> flags{0, 1, 0, 1, 0, 0};
    const std::array<Core::u8, 6> costs{1, 1, 5, 1, 16, 2};
    auto desc = validDesc(flags, costs);

    desc.widthCells = 0;
    EXPECT_FALSE(writeNavigationGrid2DPayloadBytes(desc));
    desc = validDesc(flags, costs);
    desc.cellFlags = std::span<const Core::u8>{flags}.first(5U);
    EXPECT_FALSE(writeNavigationGrid2DPayloadBytes(desc));

    auto invalidFlags = flags;
    invalidFlags[2] = 2U;
    EXPECT_FALSE(writeNavigationGrid2DPayloadBytes(validDesc(invalidFlags, costs)));
    auto invalidCosts = costs;
    invalidCosts[3] = 0U;
    EXPECT_FALSE(writeNavigationGrid2DPayloadBytes(validDesc(flags, invalidCosts)));

    auto payload = writeNavigationGrid2DPayloadBytes(validDesc(flags, costs));
    ASSERT_TRUE(payload);
    (*payload)[2] = std::byte{1};
    auto reserved = parseNavigationGrid2DPayload(*payload);
    ASSERT_FALSE(reserved);
    EXPECT_EQ(reserved.error().code, AssetFormatErrorCode::InvalidLayout);

    payload->pop_back();
    EXPECT_FALSE(parseNavigationGrid2DPayload(*payload));
}

} // namespace
} // namespace Tina::AssetFormat
