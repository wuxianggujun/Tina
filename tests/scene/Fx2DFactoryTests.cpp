#include <tina/asset/AssetStore.hpp>
#include <tina/scene/Fx2DFactory.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <gtest/gtest.h>

#include <memory_resource>

namespace Tina::Scene {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x7EU);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] AssetFormat::Fx2DPayloadDesc fxDesc()
{
    AssetFormat::Fx2DPayloadDesc desc{};
    desc.spriteAssetId = assetId(1U);
    desc.particle.capacity = 6;
    desc.particle.count = 4;
    desc.particle.randomSeed = 99;
    desc.particle.firstStableParticleKey = 100;
    desc.particle.originX = 2.0F;
    desc.particle.originY = 3.0F;
    desc.particle.lifetimeMinSeconds = 2.0F;
    desc.particle.lifetimeMaxSeconds = 2.0F;
    desc.particle.startWidthMeters = 0.5F;
    desc.particle.startHeightMeters = 0.25F;
    desc.particle.endWidthMeters = 0.1F;
    desc.particle.endHeightMeters = 0.2F;
    desc.trail.segmentCapacity = 5;
    desc.trail.segmentLifetimeSeconds = 3.0F;
    desc.trail.startWidthMeters = 0.4F;
    desc.trail.endWidthMeters = 0.1F;
    desc.trail.stableEntityKeyBase = 200;
    return desc;
}

TEST(Fx2DFactoryTests, CreatesConfiguredParticleBurstAndTrail)
{
    std::pmr::unsynchronized_pool_resource assetMemory;
    auto store = Asset::AssetStore::Create({.capacity = 1, .memoryResource = &assetMemory});
    ASSERT_TRUE(store) << store.error().message;
    auto sprite = store->beginQueued(assetId(1U), AssetFormat::AssetKind::Sprite);
    ASSERT_TRUE(sprite) << sprite.error().message;

    std::pmr::unsynchronized_pool_resource sceneMemory;
    auto instance = createFx2DFromAsset(fxDesc(), *sprite, sceneMemory);
    ASSERT_TRUE(instance) << instance.error().message;
    EXPECT_EQ(instance->particles.capacity(), 6U);
    EXPECT_EQ(instance->particles.randomSeed(), 99U);
    EXPECT_EQ(instance->initialBurst.count, 4U);
    EXPECT_EQ(instance->initialBurst.sprite, *sprite);
    EXPECT_FLOAT_EQ(instance->initialBurst.origin.x, 2.0F);
    EXPECT_EQ(instance->trail.segmentCapacity(), 5U);
    EXPECT_EQ(instance->trail.config().sprite, *sprite);
    EXPECT_EQ(instance->trail.config().stableEntityKeyBase, 200U);
    ASSERT_TRUE(instance->particles.emitBurst(instance->initialBurst));
    EXPECT_EQ(instance->particles.liveCount(), 4U);
}

TEST(Fx2DFactoryTests, RejectsUnresolvedSpriteBeforeAllocatingOwners)
{
    auto result = createFx2DFromAsset(fxDesc(), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, SceneErrorCode::UnresolvedSprite);
}

} // namespace
} // namespace Tina::Scene
