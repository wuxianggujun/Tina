#include "BgfxResourceSlotGeneration.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace Tina::Render::Bgfx {
namespace {

TEST(BgfxResourceSlotGenerationTest, TextureSlotPermanentlyRetiresAtGenerationLimit)
{
    constexpr u32 MaximumGeneration = (std::numeric_limits<u32>::max)();
    auto identity =
        BgfxTextureResourceSlotGeneration::fromGenerationForTesting(MaximumGeneration);

    ASSERT_TRUE(identity.canReuse(false, false));
    identity.advanceAfterRelease();

    EXPECT_EQ(identity.value(), MaximumGeneration);
    EXPECT_TRUE(identity.permanentlyRetired());
    EXPECT_FALSE(identity.canReuse(false, false));
    EXPECT_FALSE(identity.canReuse(false, true));
}

TEST(BgfxResourceSlotGenerationTest, MeshSlotPermanentlyRetiresAtGenerationLimit)
{
    constexpr u32 MaximumGeneration = (std::numeric_limits<u32>::max)();
    auto identity =
        BgfxMeshResourceSlotGeneration::fromGenerationForTesting(MaximumGeneration);

    ASSERT_TRUE(identity.canReuse(false, false));
    identity.advanceAfterRelease();

    EXPECT_EQ(identity.value(), MaximumGeneration);
    EXPECT_TRUE(identity.permanentlyRetired());
    EXPECT_FALSE(identity.canReuse(false, false));
    EXPECT_FALSE(identity.canReuse(false, true));
}

TEST(BgfxResourceSlotGenerationTest, NonWrappingReleaseAdvancesAndRemainsReusable)
{
    constexpr u32 PenultimateGeneration = (std::numeric_limits<u32>::max)() - 1U;
    auto textureIdentity =
        BgfxTextureResourceSlotGeneration::fromGenerationForTesting(PenultimateGeneration);
    auto meshIdentity =
        BgfxMeshResourceSlotGeneration::fromGenerationForTesting(PenultimateGeneration);

    textureIdentity.advanceAfterRelease();
    meshIdentity.advanceAfterRelease();

    EXPECT_EQ(textureIdentity.value(), PenultimateGeneration + 1U);
    EXPECT_EQ(meshIdentity.value(), PenultimateGeneration + 1U);
    EXPECT_FALSE(textureIdentity.permanentlyRetired());
    EXPECT_FALSE(meshIdentity.permanentlyRetired());
    EXPECT_TRUE(textureIdentity.canReuse(false, false));
    EXPECT_TRUE(meshIdentity.canReuse(false, false));
}

} // namespace
} // namespace Tina::Render::Bgfx
