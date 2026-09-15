#include <tina/asset/AssetStore.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/math/Quaternion.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/ParticleSystem3D.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Tina::Scene {
namespace {

struct TestFrameResourceLifetime final {
    u32 borrows = 0;
};

[[nodiscard]] TestFrameResourceLifetime& testFrameResourceLifetime() noexcept
{
    static TestFrameResourceLifetime lifetime{};
    return lifetime;
}

[[nodiscard]] Render::RenderFramePacket& testFramePacket() noexcept
{
    (void)testFrameResourceLifetime();
    static Render::RenderFramePacket packet{};
    return packet;
}

[[nodiscard]] Render::FrameResourceSink& beginTestFrameResources()
{
    static u64 frameIndex = 0;
    const Core::Status status = testFramePacket().beginFrame(frameIndex++);
    EXPECT_TRUE(status) << (status ? "" : status.error().message);
    return testFramePacket().resourceSink();
}

[[nodiscard]] Core::Result<Render::FrameResourceRef> internTestTexture(
    Render::FrameResourceSink& sink, u32 bindingKey) noexcept
{
    TestFrameResourceLifetime& lifetime = testFrameResourceLifetime();
    ++lifetime.borrows;
    Render::FramePin pin{
        Render::FramePinKind::Custom,
        bindingKey,
        &lifetime,
        [](void* userData) noexcept {
            auto& pinned = *static_cast<TestFrameResourceLifetime*>(userData);
            if (pinned.borrows > 0)
            {
                --pinned.borrows;
            }
        },
    };
    return sink.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Texture2D,
            .deviceBindingKey = bindingKey,
        },
        std::move(pin));
}

[[nodiscard]] u64 textureBindingKey(Render::FrameResourceRef texture) noexcept
{
    const Render::FrameResourceDescriptor* descriptor =
        testFramePacket().resourceTableView().resolve(texture, Render::FrameResourceKind::Texture2D);
    return descriptor == nullptr ? 0 : descriptor->deviceBindingKey;
}

class TrackingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return m_allocationCount; }
    void rejectAllocationAtLeast(usize bytes) noexcept { m_rejectedAllocationMinimumBytes = bytes; }

private:
    void* do_allocate(Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        if (bytes >= m_rejectedAllocationMinimumBytes)
        {
            throw std::bad_alloc{};
        }
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        return storage;
    }

    void do_deallocate(void* pointer, Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    usize m_rejectedAllocationMinimumBytes = (std::numeric_limits<usize>::max)();
};

[[nodiscard]] Core::AssetId fixtureAssetId(u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    return *Core::AssetId::fromBytes(bytes);
}

struct TestSpriteBindings final {
    [[nodiscard]] Asset::AssetFrameResourceResolver resolver() noexcept
    {
        return Asset::AssetFrameResourceResolver{.userData = this, .resolve = &resolve};
    }

    [[nodiscard]] static Core::Result<Render::FrameResourceRef> resolve(
        void* userData, Asset::AssetHandle sprite,
        Render::FrameResourceSink& frameResources) noexcept
    {
        auto& self = *static_cast<TestSpriteBindings*>(userData);
        ++self.resolveCalls;
        self.lastResolved = sprite;
        if (self.store == nullptr ||
            self.store->assetKind(sprite) != AssetFormat::AssetKind::Sprite ||
            self.store->state(sprite) == Asset::AssetLogicalState::Unloaded)
        {
            return Render::FrameResourceRef{};
        }
        if (sprite == self.boundSprite)
        {
            return internTestTexture(frameResources, self.bindingKey);
        }
        if (sprite == self.secondBoundSprite)
        {
            return internTestTexture(frameResources, self.secondBindingKey);
        }
        return Render::FrameResourceRef{};
    }

    Asset::AssetStore* store = nullptr;
    Asset::AssetHandle boundSprite{};
    Asset::AssetHandle secondBoundSprite{};
    u32 bindingKey = 0;
    u32 secondBindingKey = 0;
    usize resolveCalls = 0;
    Asset::AssetHandle lastResolved{};
};

class ParticleSystem3DTests : public testing::Test {
protected:
    void SetUp() override
    {
        auto store = Asset::AssetStore::Create({.initialAssetReserve = 4, .memoryResource = &assetMemory_});
        ASSERT_TRUE(store.has_value()) << (store ? "" : store.error().message);
        assetStore_.emplace(std::move(*store));

        auto firstSprite = assetStore_->beginQueued(fixtureAssetId(1), AssetFormat::AssetKind::Sprite);
        auto secondSprite = assetStore_->beginQueued(fixtureAssetId(2), AssetFormat::AssetKind::Sprite);
        ASSERT_TRUE(firstSprite.has_value());
        ASSERT_TRUE(secondSprite.has_value());
        firstSprite_ = *firstSprite;
        secondSprite_ = *secondSprite;
    }

    [[nodiscard]] TestSpriteBindings bindingsFor(Asset::AssetHandle sprite, u32 bindingKey = 17U) noexcept
    {
        return TestSpriteBindings{
            .store = &*assetStore_,
            .boundSprite = sprite,
            .bindingKey = bindingKey,
        };
    }

    std::pmr::unsynchronized_pool_resource assetMemory_{};
    std::optional<Asset::AssetStore> assetStore_{};
    Asset::AssetHandle firstSprite_{};
    Asset::AssetHandle secondSprite_{};
};

[[nodiscard]] ParticleSystem3D makeSystem(usize capacity, u64 seed = 7,
                                          u64 firstStableParticleKey = 100,
                                          std::pmr::memory_resource& resource =
                                              *std::pmr::get_default_resource())
{
    auto system = ParticleSystem3D::Create(
        {
            .capacity = capacity,
            .randomSeed = seed,
            .firstStableParticleKey = firstStableParticleKey,
        },
        resource);
    if (!system)
    {
        throw std::runtime_error(system.error().message);
    }
    return std::move(*system);
}

[[nodiscard]] ParticleBurst3D randomizedBurst(Asset::AssetHandle sprite, usize count = 1)
{
    return ParticleBurst3D{
        .count = count,
        .sprite = sprite,
        .origin = {4.0F, -3.0F, 2.0F},
        .positionOffset = {.minimum = {-2.0F, -1.0F, -0.5F}, .maximum = {3.0F, 5.0F, 1.5F}},
        .velocity = {.minimum = {-8.0F, 2.0F, -1.0F}, .maximum = {9.0F, 12.0F, 4.0F}},
        .lifetime = {.minimum = Core::Duration{0.5}, .maximum = Core::Duration{3.0}},
        .gravityMetersPerSecondSquared = {0.0F, -9.81F, 0.0F},
        .startSizeMeters = {0.25F, 0.5F},
        .endSizeMeters = {1.25F, 1.5F},
        .startColor = {10, 20, 30, 40},
        .endColor = {210, 220, 230, 240},
        .rotationRadians = 0.25F,
    };
}

[[nodiscard]] Render::RenderSceneBuilder makeRenderBuilder(u32 particleCapacity)
{
    auto builder = Render::RenderSceneBuilder::Create({
        .spriteCapacity = 1,
        .mesh3DItemCapacity = 1,
        .mesh3DBatchCapacity = 1,
        .particle3DItemCapacity = particleCapacity,
    });
    if (!builder)
    {
        throw std::runtime_error(builder.error().message);
    }
    return std::move(*builder);
}

[[nodiscard]] Core::Status addTestCamera(Render::RenderSceneWriter& writer)
{
    return writer.setPerspectiveCamera({
        .stableCameraKey = 1,
        .worldPose = {.positionZ = 40.0F},
        .verticalFovDegrees = 60.0F,
        .nearPlaneMeters = 0.1F,
        .farPlaneMeters = 500.0F,
    });
}

TEST_F(ParticleSystem3DTests, CreateRejectsZeroCapacityAndZeroStableKeyBase)
{
    auto zeroCapacity = ParticleSystem3D::Create({.capacity = 0});
    ASSERT_FALSE(zeroCapacity.has_value());
    EXPECT_EQ(zeroCapacity.error().code, SceneErrorCode::CapacityExceeded);

    auto zeroKey = ParticleSystem3D::Create({.capacity = 4, .firstStableParticleKey = 0});
    ASSERT_FALSE(zeroKey.has_value());
    EXPECT_EQ(zeroKey.error().code, SceneErrorCode::InvalidComponent);
}

TEST_F(ParticleSystem3DTests, CreateAllocatesOnceAndEmitUpdateExtractDoNotGrow)
{
    TrackingMemoryResource memory{};
    auto system = makeSystem(8, 7, 100, memory);
    // Create is the only allocating call. The exact count is a pool-resource
    // implementation detail, so the contract asserted here is that the frame path
    // adds nothing to it.
    const usize afterCreate = memory.allocationCount();
    ASSERT_GT(afterCreate, 0U);

    ASSERT_TRUE(system.emitBurst(randomizedBurst(firstSprite_, 8)).has_value());
    ASSERT_TRUE(system.update(Core::Duration{0.25}).has_value());

    auto builder = makeRenderBuilder(8);
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(addTestCamera(writer).has_value());
    auto bindings = bindingsFor(firstSprite_);
    ASSERT_TRUE(system.extract(writer, beginTestFrameResources(), bindings.resolver()).has_value());

    EXPECT_EQ(memory.allocationCount(), afterCreate);
}

TEST_F(ParticleSystem3DTests, EmitBurstOverCapacityLeavesRandomStateAndKeysUntouched)
{
    auto system = makeSystem(2);
    ASSERT_TRUE(system.emitBurst(randomizedBurst(firstSprite_, 2)).has_value());
    ASSERT_EQ(system.liveCount(), 2U);

    Particle3D before[2]{system.particles()[0], system.particles()[1]};

    auto overflow = system.emitBurst(randomizedBurst(firstSprite_, 1));
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, SceneErrorCode::CapacityExceeded);
    ASSERT_EQ(system.liveCount(), 2U);
    for (usize index = 0; index < 2; ++index)
    {
        EXPECT_EQ(system.particles()[index].stableParticleKey, before[index].stableParticleKey);
        EXPECT_FLOAT_EQ(system.particles()[index].position.x, before[index].position.x);
        EXPECT_FLOAT_EQ(system.particles()[index].velocity.y, before[index].velocity.y);
    }

}

TEST_F(ParticleSystem3DTests, RejectedBurstConsumesNoRandomDrawsAndNoStableKeys)
{
    // Both systems share a seed. The only difference is that `withRejection` also
    // attempts an over-capacity burst in the middle. If that rejected burst leaked
    // even one RNG draw or one stable key, the third particle would diverge.
    auto withRejection = makeSystem(3, 31337, 700);
    auto clean = makeSystem(3, 31337, 700);

    ASSERT_TRUE(withRejection.emitBurst(randomizedBurst(firstSprite_, 2)).has_value());
    ASSERT_TRUE(clean.emitBurst(randomizedBurst(firstSprite_, 2)).has_value());

    // Two requested, one slot free: must be rejected whole, never partially applied.
    auto rejected = withRejection.emitBurst(randomizedBurst(firstSprite_, 2));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(withRejection.liveCount(), 2U);

    ASSERT_TRUE(withRejection.emitBurst(randomizedBurst(firstSprite_, 1)).has_value());
    ASSERT_TRUE(clean.emitBurst(randomizedBurst(firstSprite_, 1)).has_value());

    ASSERT_EQ(withRejection.liveCount(), 3U);
    ASSERT_EQ(clean.liveCount(), 3U);
    const Particle3D& afterRejection = withRejection.particles()[2];
    const Particle3D& never = clean.particles()[2];
    EXPECT_EQ(afterRejection.stableParticleKey, never.stableParticleKey);
    EXPECT_FLOAT_EQ(afterRejection.position.x, never.position.x);
    EXPECT_FLOAT_EQ(afterRejection.position.y, never.position.y);
    EXPECT_FLOAT_EQ(afterRejection.position.z, never.position.z);
    EXPECT_FLOAT_EQ(afterRejection.velocity.z, never.velocity.z);
    EXPECT_DOUBLE_EQ(afterRejection.lifetime.count(), never.lifetime.count());
}

TEST_F(ParticleSystem3DTests, SameSeedReproducesTheSameParticleSequence)
{
    auto left = makeSystem(4, 4242);
    auto right = makeSystem(4, 4242);
    ASSERT_TRUE(left.emitBurst(randomizedBurst(firstSprite_, 4)).has_value());
    ASSERT_TRUE(right.emitBurst(randomizedBurst(firstSprite_, 4)).has_value());
    ASSERT_EQ(left.liveCount(), right.liveCount());
    for (usize index = 0; index < left.liveCount(); ++index)
    {
        EXPECT_FLOAT_EQ(left.particles()[index].position.x, right.particles()[index].position.x);
        EXPECT_FLOAT_EQ(left.particles()[index].position.y, right.particles()[index].position.y);
        EXPECT_FLOAT_EQ(left.particles()[index].position.z, right.particles()[index].position.z);
        EXPECT_DOUBLE_EQ(left.particles()[index].lifetime.count(),
                         right.particles()[index].lifetime.count());
    }

    auto other = makeSystem(4, 4243);
    ASSERT_TRUE(other.emitBurst(randomizedBurst(firstSprite_, 4)).has_value());
    bool anyDifferent = false;
    for (usize index = 0; index < left.liveCount(); ++index)
    {
        anyDifferent = anyDifferent ||
                       left.particles()[index].position.z != other.particles()[index].position.z;
    }
    EXPECT_TRUE(anyDifferent);
}

TEST_F(ParticleSystem3DTests, UpdateIntegratesVelocityBeforePositionUnderGravity)
{
    auto system = makeSystem(1);
    const ParticleBurst3D burst{
        .count = 1,
        .sprite = firstSprite_,
        .origin = {0.0F, 0.0F, 0.0F},
        .velocity = {.minimum = {0.0F, 0.0F, 0.0F}, .maximum = {0.0F, 0.0F, 0.0F}},
        .lifetime = {.minimum = Core::Duration{10.0}, .maximum = Core::Duration{10.0}},
        .gravityMetersPerSecondSquared = {0.0F, -10.0F, 0.0F},
    };
    ASSERT_TRUE(system.emitBurst(burst).has_value());
    ASSERT_TRUE(system.update(Core::Duration{1.0}).has_value());

    // Semi-implicit Euler: v becomes -10 first, then position uses the new v.
    // Explicit Euler would leave y at 0 for this first step, which is the bug
    // this asserts against.
    EXPECT_FLOAT_EQ(system.particles()[0].velocity.y, -10.0F);
    EXPECT_FLOAT_EQ(system.particles()[0].position.y, -10.0F);
}

TEST_F(ParticleSystem3DTests, UpdateExpiresParticlesAndCompactsRetainingStableKeys)
{
    auto system = makeSystem(3, 7, 500);
    const auto shortLived = [&](Asset::AssetHandle sprite, double seconds) {
        return ParticleBurst3D{
            .count = 1,
            .sprite = sprite,
            .lifetime = {.minimum = Core::Duration{seconds}, .maximum = Core::Duration{seconds}},
        };
    };
    ASSERT_TRUE(system.emitBurst(shortLived(firstSprite_, 1.0)).has_value());
    ASSERT_TRUE(system.emitBurst(shortLived(firstSprite_, 5.0)).has_value());
    ASSERT_TRUE(system.emitBurst(shortLived(firstSprite_, 1.0)).has_value());
    ASSERT_EQ(system.liveCount(), 3U);
    const u64 survivorKey = system.particles()[1].stableParticleKey;

    auto stats = system.update(Core::Duration{2.0});
    ASSERT_TRUE(stats.has_value()) << (stats ? "" : stats.error().message);
    EXPECT_EQ(stats->expired, 2U);
    EXPECT_EQ(stats->alive, 1U);
    ASSERT_EQ(system.liveCount(), 1U);
    // Compaction moves the survivor but must not renumber it.
    EXPECT_EQ(system.particles()[0].stableParticleKey, survivorKey);
    EXPECT_EQ(system.availableCapacity(), 2U);
}

// extract() itself accepts a camera-less writer, exactly like addMesh3D does; the
// requirement is enforced once at commit() so a caller may submit particles before
// the camera in the same frame.
TEST_F(ParticleSystem3DTests, CommitRejectsLiveParticlesWithoutPerspectiveCamera)
{
    auto system = makeSystem(1);
    ASSERT_TRUE(system.emitBurst(randomizedBurst(firstSprite_)).has_value());
    auto bindings = bindingsFor(firstSprite_);

    auto builder = makeRenderBuilder(1);
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    {
        auto writer = builder.writer();
        ASSERT_TRUE(
            system.extract(writer, beginTestFrameResources(), bindings.resolver()).has_value());
    }
    auto missingCamera = builder.commit();
    ASSERT_FALSE(missingCamera.has_value());
    EXPECT_EQ(missingCamera.error().code, Render::RenderErrorCode::RenderSceneMissingCamera);

    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    {
        auto writer = builder.writer();
        ASSERT_TRUE(addTestCamera(writer).has_value());
        ASSERT_TRUE(
            system.extract(writer, beginTestFrameResources(), bindings.resolver()).has_value());
    }
    ASSERT_TRUE(builder.commit().has_value());
}

TEST_F(ParticleSystem3DTests, ExtractWithLiveParticleRequiresResolver)
{
    auto system = makeSystem(1);
    ASSERT_TRUE(system.emitBurst(randomizedBurst(firstSprite_)).has_value());

    auto builder = makeRenderBuilder(1);
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(addTestCamera(writer).has_value());
    auto extracted =
        system.extract(writer, beginTestFrameResources(), Asset::AssetFrameResourceResolver{});
    ASSERT_FALSE(extracted.has_value());
    EXPECT_EQ(extracted.error().code, SceneErrorCode::UnresolvedSprite);
}

TEST_F(ParticleSystem3DTests, ExtractInterpolatesPositionSizeAndColorAtNormalizedLifetime)
{
    auto system = makeSystem(1, 5, 900);
    const ParticleBurst3D burst{
        .count = 1,
        .sprite = firstSprite_,
        .origin = {1.0F, 2.0F, -3.0F},
        .velocity = {.minimum = {2.0F, -1.0F, 4.0F}, .maximum = {2.0F, -1.0F, 4.0F}},
        .lifetime = {.minimum = Core::Duration{2.0}, .maximum = Core::Duration{2.0}},
        .startSizeMeters = {2.0F, 4.0F},
        .endSizeMeters = {4.0F, 8.0F},
        .startColor = {10, 20, 30, 40},
        .endColor = {110, 120, 130, 140},
        .rotationRadians = 0.5F,
        .blendMode = Core::BlendMode::Additive,
    };
    ASSERT_TRUE(system.emitBurst(burst).has_value());
    ASSERT_TRUE(system.update(Core::Duration{1.0}).has_value());

    auto builder = makeRenderBuilder(1);
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(addTestCamera(writer).has_value());
    auto bindings = bindingsFor(firstSprite_, 44U);
    auto extracted = system.extract(writer, beginTestFrameResources(), bindings.resolver());
    ASSERT_TRUE(extracted.has_value()) << (extracted ? "" : extracted.error().message);
    EXPECT_EQ(extracted->submitted, 1U);
    EXPECT_EQ(bindings.resolveCalls, 1U);

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);
    ASSERT_EQ(committed->particles3D().size(), 1U);

    const Render::RenderParticle3DItem& item = committed->particles3D().front();
    EXPECT_EQ(textureBindingKey(item.texture), 44U);
    EXPECT_FLOAT_EQ(item.worldX, 3.0F);
    EXPECT_FLOAT_EQ(item.worldY, 1.0F);
    EXPECT_FLOAT_EQ(item.worldZ, 1.0F);
    // Halfway through a 2s lifetime, every ramp sits at its midpoint.
    EXPECT_FLOAT_EQ(item.widthMeters, 3.0F);
    EXPECT_FLOAT_EQ(item.heightMeters, 6.0F);
    EXPECT_EQ(item.red, 60U);
    EXPECT_EQ(item.green, 70U);
    EXPECT_EQ(item.blue, 80U);
    EXPECT_EQ(item.alpha, 90U);
    EXPECT_FLOAT_EQ(item.rotationRadians, 0.5F);
    EXPECT_EQ(item.blendMode, Core::BlendMode::Additive);
}

TEST_F(ParticleSystem3DTests, ExtractResolvesConsecutiveEqualSpritesOnce)
{
    auto system = makeSystem(4);
    ASSERT_TRUE(system.emitBurst(randomizedBurst(firstSprite_, 3)).has_value());

    auto builder = makeRenderBuilder(4);
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(addTestCamera(writer).has_value());
    auto bindings = bindingsFor(firstSprite_, 21U);
    auto extracted = system.extract(writer, beginTestFrameResources(), bindings.resolver());
    ASSERT_TRUE(extracted.has_value()) << (extracted ? "" : extracted.error().message);
    EXPECT_EQ(extracted->submitted, 3U);
    EXPECT_EQ(bindings.resolveCalls, 1U);
}

TEST_F(ParticleSystem3DTests, MoveConstructionTransfersStateAndKeepsDeterminism)
{
    auto system = makeSystem(4, 99, 300);
    ASSERT_TRUE(system.emitBurst(randomizedBurst(firstSprite_, 2)).has_value());
    const u64 firstKey = system.particles()[0].stableParticleKey;

    ParticleSystem3D moved{std::move(system)};
    ASSERT_EQ(moved.liveCount(), 2U);
    EXPECT_EQ(moved.particles()[0].stableParticleKey, firstKey);
    EXPECT_EQ(moved.capacity(), 4U);
    EXPECT_EQ(moved.randomSeed(), 99U);

    // Keys continue from where the moved-from system left them, never reusing one.
    ASSERT_TRUE(moved.emitBurst(randomizedBurst(secondSprite_, 1)).has_value());
    EXPECT_EQ(moved.particles()[2].stableParticleKey, firstKey + 2U);
}

static_assert(!std::is_copy_constructible_v<ParticleSystem3D>);
static_assert(std::is_nothrow_move_constructible_v<ParticleSystem3D>);
static_assert(!std::is_move_assignable_v<ParticleSystem3D>);

} // namespace
} // namespace Tina::Scene
