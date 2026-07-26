#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/ParticleSystem2D.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <utility>

namespace Tina::Scene {
namespace {

class TrackingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return m_allocationCount; }
    [[nodiscard]] usize deallocationCount() const noexcept { return m_deallocationCount; }
    void rejectAllocationAtLeast(usize bytes) noexcept { m_rejectedAllocationMinimumBytes = bytes; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (bytes >= m_rejectedAllocationMinimumBytes) {
            throw std::bad_alloc{};
        }
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        return storage;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        ++m_deallocationCount;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    usize m_deallocationCount = 0;
    usize m_rejectedAllocationMinimumBytes = (std::numeric_limits<usize>::max)();
};

[[nodiscard]] ParticleSystem2D makeSystem(
    usize capacity,
    u64 seed = 7,
    u64 firstStableParticleKey = 100,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    auto system = ParticleSystem2D::Create(
        {
            .capacity = capacity,
            .randomSeed = seed,
            .firstStableParticleKey = firstStableParticleKey,
        },
        resource);
    if (!system) {
        throw std::runtime_error(system.error().message);
    }
    return std::move(*system);
}

[[nodiscard]] ParticleBurst2D randomizedBurst(usize count = 1)
{
    return ParticleBurst2D{
        .count = count,
        .spriteKey = 17,
        .origin = {4.0F, -3.0F},
        .positionOffset = {
            .minimum = {-2.0F, -1.0F},
            .maximum = {3.0F, 5.0F},
        },
        .velocity = {
            .minimum = {-8.0F, 2.0F},
            .maximum = {9.0F, 12.0F},
        },
        .lifetime = {
            .minimum = Core::Duration{0.5},
            .maximum = Core::Duration{3.0},
        },
        .startSizeMeters = {0.25F, 0.5F},
        .endSizeMeters = {1.25F, 1.5F},
        .startColor = {10, 20, 30, 40},
        .endColor = {210, 220, 230, 240},
        .rotationRadians = 0.25F,
        .sortingLayer = 2,
        .orderInLayer = 11,
    };
}

void expectSameParticle(const Particle2D& left, const Particle2D& right)
{
    EXPECT_EQ(left.stableParticleKey, right.stableParticleKey);
    EXPECT_EQ(left.spriteKey, right.spriteKey);
    EXPECT_FLOAT_EQ(left.position.x, right.position.x);
    EXPECT_FLOAT_EQ(left.position.y, right.position.y);
    EXPECT_FLOAT_EQ(left.velocity.x, right.velocity.x);
    EXPECT_FLOAT_EQ(left.velocity.y, right.velocity.y);
    EXPECT_DOUBLE_EQ(left.age.count(), right.age.count());
    EXPECT_DOUBLE_EQ(left.lifetime.count(), right.lifetime.count());
    EXPECT_EQ(left.startSizeMeters, right.startSizeMeters);
    EXPECT_EQ(left.endSizeMeters, right.endSizeMeters);
    EXPECT_EQ(left.startColor, right.startColor);
    EXPECT_EQ(left.endColor, right.endColor);
    EXPECT_FLOAT_EQ(left.rotationRadians, right.rotationRadians);
    EXPECT_EQ(left.sortingLayer, right.sortingLayer);
    EXPECT_EQ(left.orderInLayer, right.orderInLayer);
}

[[nodiscard]] Render::RenderSceneBuilder makeRenderBuilder(u32 spriteCapacity)
{
    auto builder = Render::RenderSceneBuilder::Create({
        .spriteCapacity = spriteCapacity,
        .mesh3DItemCapacity = 1,
        .mesh3DBatchCapacity = 1,
    });
    if (!builder) {
        throw std::runtime_error(builder.error().message);
    }
    return std::move(*builder);
}

[[nodiscard]] Core::Status addTestCamera(Render::RenderSceneWriter& writer)
{
    return writer.setCamera2D({
        .stableCameraKey = 1,
        .worldWidth = 100.0F,
        .worldHeight = 100.0F,
        .actualPixelsPerMeter = 1.0F,
    });
}

TEST(ParticleSystem2DTests, CreateRejectsInvalidCapacityAndStableKeyBase)
{
    auto zeroCapacity = ParticleSystem2D::Create({.capacity = 0});
    ASSERT_FALSE(zeroCapacity.has_value());
    EXPECT_EQ(zeroCapacity.error().code, SceneErrorCode::CapacityExceeded);

    auto zeroStableKey = ParticleSystem2D::Create({
        .capacity = 1,
        .firstStableParticleKey = 0,
    });
    ASSERT_FALSE(zeroStableKey.has_value());
    EXPECT_EQ(zeroStableKey.error().code, SceneErrorCode::InvalidComponent);
}

TEST(ParticleSystem2DTests, CreateMapsPmrAllocationFailureWithoutLeakingStorage)
{
    TrackingMemoryResource resource;
    resource.rejectAllocationAtLeast(sizeof(Particle2D) * 8U);
    auto system = ParticleSystem2D::Create({.capacity = 8}, resource);
    ASSERT_FALSE(system.has_value());
    EXPECT_EQ(system.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

TEST(ParticleSystem2DTests, EqualSeedProducesIdenticalParticlesAndDifferentSeedChangesSequence)
{
    auto first = makeSystem(8, 0);
    auto second = makeSystem(8, 0);
    auto different = makeSystem(8, 0x87654321ULL);
    const ParticleBurst2D burst = randomizedBurst(8);

    ASSERT_TRUE(first.emitBurst(burst).has_value());
    ASSERT_TRUE(second.emitBurst(burst).has_value());
    ASSERT_TRUE(different.emitBurst(burst).has_value());
    ASSERT_EQ(first.liveCount(), 8U);
    ASSERT_EQ(second.liveCount(), first.liveCount());

    for (usize index = 0; index < first.liveCount(); ++index) {
        expectSameParticle(first.particles()[index], second.particles()[index]);
    }
    EXPECT_TRUE(
        first.particles()[0].position != different.particles()[0].position ||
        first.particles()[0].velocity != different.particles()[0].velocity ||
        first.particles()[0].lifetime != different.particles()[0].lifetime);
}

TEST(ParticleSystem2DTests, CapacityFailureIsAtomicAndDoesNotAdvanceRandomOrStableKeyState)
{
    auto system = makeSystem(2, 99, 500);
    auto reference = makeSystem(2, 99, 500);

    auto tooLarge = system.emitBurst(randomizedBurst(3));
    ASSERT_FALSE(tooLarge.has_value());
    EXPECT_EQ(tooLarge.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(system.liveCount(), 0U);

    const ParticleBurst2D exact = randomizedBurst(2);
    ASSERT_TRUE(system.emitBurst(exact).has_value());
    ASSERT_TRUE(reference.emitBurst(exact).has_value());
    ASSERT_EQ(system.liveCount(), 2U);
    for (usize index = 0; index < system.liveCount(); ++index) {
        expectSameParticle(system.particles()[index], reference.particles()[index]);
    }

    const auto before = system.particles();
    auto noRoom = system.emitBurst(randomizedBurst(1));
    ASSERT_FALSE(noRoom.has_value());
    EXPECT_EQ(noRoom.error().code, SceneErrorCode::CapacityExceeded);
    ASSERT_EQ(system.liveCount(), 2U);
    expectSameParticle(system.particles()[0], before[0]);
    expectSameParticle(system.particles()[1], before[1]);
}

TEST(ParticleSystem2DTests, StableKeyExhaustionRejectsWholeBurstBeforeRandomStateAdvances)
{
    constexpr u64 MaximumKey = (std::numeric_limits<u64>::max)();
    auto system = makeSystem(2, 123, MaximumKey);
    auto reference = makeSystem(1, 123, MaximumKey);

    auto overflow = system.emitBurst(randomizedBurst(2));
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(system.liveCount(), 0U);

    const ParticleBurst2D one = randomizedBurst(1);
    ASSERT_TRUE(system.emitBurst(one).has_value());
    ASSERT_TRUE(reference.emitBurst(one).has_value());
    expectSameParticle(system.particles().front(), reference.particles().front());

    system.clear();
    auto exhausted = system.emitBurst(one);
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(system.liveCount(), 0U);
}

TEST(ParticleSystem2DTests, RejectsNonFiniteValuesInvalidLifetimeAndUpdateDelta)
{
    auto system = makeSystem(4);
    ParticleBurst2D burst = randomizedBurst();
    burst.lifetime.minimum = Core::Duration::zero();
    auto zeroLifetime = system.emitBurst(burst);
    ASSERT_FALSE(zeroLifetime.has_value());
    EXPECT_EQ(zeroLifetime.error().code, SceneErrorCode::InvalidComponent);

    burst = randomizedBurst();
    burst.velocity.maximum.x = (std::numeric_limits<float>::quiet_NaN)();
    auto nanVelocity = system.emitBurst(burst);
    ASSERT_FALSE(nanVelocity.has_value());
    EXPECT_EQ(nanVelocity.error().code, SceneErrorCode::InvalidComponent);

    burst = randomizedBurst();
    burst.endSizeMeters.y = 0.0F;
    auto zeroSize = system.emitBurst(burst);
    ASSERT_FALSE(zeroSize.has_value());
    EXPECT_EQ(zeroSize.error().code, SceneErrorCode::InvalidComponent);
    EXPECT_EQ(system.liveCount(), 0U);

    auto invalidDelta = system.update(Core::Duration{(std::numeric_limits<double>::infinity)()});
    ASSERT_FALSE(invalidDelta.has_value());
    EXPECT_EQ(invalidDelta.error().code, SceneErrorCode::InvalidComponent);
}

TEST(ParticleSystem2DTests, UpdateExpiresAndReclaimsCapacityWithoutReusingStableKeys)
{
    auto system = makeSystem(2, 4, 700);
    ParticleBurst2D burst = randomizedBurst(2);
    burst.lifetime = {
        .minimum = Core::Duration{0.25},
        .maximum = Core::Duration{0.25},
    };
    ASSERT_TRUE(system.emitBurst(burst).has_value());
    ASSERT_EQ(system.particles()[0].stableParticleKey, 700U);
    ASSERT_EQ(system.particles()[1].stableParticleKey, 701U);

    auto update = system.update(Core::Duration{0.25});
    ASSERT_TRUE(update.has_value()) << (update.has_value() ? "" : update.error().message);
    EXPECT_EQ(update->advanced, 2U);
    EXPECT_EQ(update->expired, 2U);
    EXPECT_EQ(update->alive, 0U);
    EXPECT_EQ(system.availableCapacity(), 2U);

    burst.count = 1;
    ASSERT_TRUE(system.emitBurst(burst).has_value());
    ASSERT_EQ(system.liveCount(), 1U);
    EXPECT_EQ(system.particles().front().stableParticleKey, 702U);
}

TEST(ParticleSystem2DTests, UpdatePositionOverflowLeavesEveryParticleUnchanged)
{
    auto system = makeSystem(2);
    ParticleBurst2D safe = randomizedBurst(1);
    safe.origin = {};
    safe.positionOffset = {};
    safe.velocity = {
        .minimum = {1.0F, 2.0F},
        .maximum = {1.0F, 2.0F},
    };
    safe.lifetime = {
        .minimum = Core::Duration{10.0},
        .maximum = Core::Duration{10.0},
    };
    ASSERT_TRUE(system.emitBurst(safe).has_value());

    ParticleBurst2D overflowing = safe;
    overflowing.origin.x = (std::numeric_limits<float>::max)();
    overflowing.velocity.minimum.x = (std::numeric_limits<float>::max)();
    overflowing.velocity.maximum.x = (std::numeric_limits<float>::max)();
    ASSERT_TRUE(system.emitBurst(overflowing).has_value());
    const Particle2D firstBefore = system.particles()[0];
    const Particle2D secondBefore = system.particles()[1];

    auto update = system.update(Core::Duration{2.0});
    ASSERT_FALSE(update.has_value());
    EXPECT_EQ(update.error().code, SceneErrorCode::InvalidComponent);
    ASSERT_EQ(system.liveCount(), 2U);
    expectSameParticle(system.particles()[0], firstBefore);
    expectSameParticle(system.particles()[1], secondBefore);
}

TEST(ParticleSystem2DTests, ExtractInterpolatesPositionSizeAndColorAtNormalizedLifetime)
{
    auto system = makeSystem(1, 5, 900);
    const ParticleBurst2D burst{
        .count = 1,
        .spriteKey = 44,
        .origin = {1.0F, 2.0F},
        .velocity = {
            .minimum = {2.0F, -1.0F},
            .maximum = {2.0F, -1.0F},
        },
        .lifetime = {
            .minimum = Core::Duration{2.0},
            .maximum = Core::Duration{2.0},
        },
        .startSizeMeters = {2.0F, 4.0F},
        .endSizeMeters = {4.0F, 8.0F},
        .startColor = {10, 20, 30, 40},
        .endColor = {110, 120, 130, 140},
        .rotationRadians = 0.5F,
        .sortingLayer = 3,
        .orderInLayer = 8,
    };
    ASSERT_TRUE(system.emitBurst(burst).has_value());
    ASSERT_TRUE(system.update(Core::Duration{1.0}).has_value());

    auto builder = makeRenderBuilder(1);
    ASSERT_TRUE(builder.beginFrame().has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(addTestCamera(writer).has_value());
    auto extracted = system.extract(writer);
    ASSERT_TRUE(extracted.has_value()) << (extracted.has_value() ? "" : extracted.error().message);
    EXPECT_EQ(extracted->submitted, 1U);
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed.has_value() ? "" : committed.error().message);
    ASSERT_EQ(committed->sprites2D().size(), 1U);

    const Render::RenderSprite2DItem& sprite = committed->sprites2D().front();
    EXPECT_EQ(sprite.spriteKey, 44U);
    EXPECT_EQ(sprite.stableEntityKey, 900U);
    EXPECT_FLOAT_EQ(sprite.centerX, 3.0F);
    EXPECT_FLOAT_EQ(sprite.centerY, 1.0F);
    EXPECT_FLOAT_EQ(sprite.rotationRadians, 0.5F);
    EXPECT_FLOAT_EQ(sprite.widthMeters, 3.0F);
    EXPECT_FLOAT_EQ(sprite.heightMeters, 6.0F);
    EXPECT_EQ(sprite.red, 60U);
    EXPECT_EQ(sprite.green, 70U);
    EXPECT_EQ(sprite.blue, 80U);
    EXPECT_EQ(sprite.alpha, 90U);
    EXPECT_EQ(sprite.sortingLayer, 3);
    EXPECT_EQ(sprite.orderInLayer, 8);
}

TEST(ParticleSystem2DTests, UpdateAndExtractDoNotGrowParticlePmrStorageAcrossThreeHundredFrames)
{
    TrackingMemoryResource particleStorage;
    {
        auto system = makeSystem(16, 77, 1'000, particleStorage);
        const usize allocationCount = particleStorage.allocationCount();
        ASSERT_GT(allocationCount, 0U);
        ParticleBurst2D burst = randomizedBurst(8);
        burst.lifetime = {
            .minimum = Core::Duration{10.0},
            .maximum = Core::Duration{10.0},
        };
        ASSERT_TRUE(system.emitBurst(burst).has_value());
        ASSERT_EQ(particleStorage.allocationCount(), allocationCount);

        auto builder = makeRenderBuilder(16);
        for (usize frame = 0; frame < 300; ++frame) {
            auto updated = system.update(Core::Duration{0.001});
            ASSERT_TRUE(updated.has_value()) << (updated.has_value() ? "" : updated.error().message);
            ASSERT_TRUE(builder.beginFrame().has_value());
            auto writer = builder.writer();
            ASSERT_TRUE(addTestCamera(writer).has_value());
            auto extracted = system.extract(writer);
            ASSERT_TRUE(extracted.has_value()) << (extracted.has_value() ? "" : extracted.error().message);
            ASSERT_EQ(extracted->submitted, 8U);
            ASSERT_TRUE(builder.commit().has_value());
        }
        EXPECT_EQ(particleStorage.allocationCount(), allocationCount);
    }
    EXPECT_EQ(particleStorage.allocationCount(), particleStorage.deallocationCount());
}

TEST(ParticleSystem2DTests, ExtractPropagatesWriterCapacityFailureWithoutMutatingParticles)
{
    auto system = makeSystem(2);
    ASSERT_TRUE(system.emitBurst(randomizedBurst(2)).has_value());
    const Particle2D firstBefore = system.particles()[0];
    const Particle2D secondBefore = system.particles()[1];

    auto builder = makeRenderBuilder(1);
    ASSERT_TRUE(builder.beginFrame().has_value());
    auto writer = builder.writer();
    auto extracted = system.extract(writer);
    ASSERT_FALSE(extracted.has_value());
    EXPECT_EQ(extracted.error().code, Render::RenderErrorCode::RenderSceneCapacityExceeded);
    ASSERT_EQ(system.liveCount(), 2U);
    expectSameParticle(system.particles()[0], firstBefore);
    expectSameParticle(system.particles()[1], secondBefore);
}

} // namespace
} // namespace Tina::Scene
