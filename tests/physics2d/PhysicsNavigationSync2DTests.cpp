#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/PhysicsNavigationSync2D.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <utility>
#include <vector>

namespace Tina::Asset {

struct PhysicsNavigationSync2DTestAccess final {
    [[nodiscard]] static Navigation2D::NavigationBlockerId publishedBlocker(
        const PhysicsNavigationSync2D& sync,
        Physics2D::PhysicsBodyId body) noexcept
    {
        return sync.m_records[sync.findRecord(body)].blocker;
    }
};

namespace {

[[nodiscard]] Physics2D::PhysicsWorld2DConfig worldConfig(
    Core::usize bodyCapacity = 8) noexcept
{
    Physics2D::PhysicsWorld2DConfig config;
    config.bodyCapacity = bodyCapacity;
    config.shapeCapacity = bodyCapacity;
    config.jointCapacity = 2;
    config.contactBeginCapacity = 4;
    config.contactEndCapacity = 4;
    config.contactHitCapacity = 2;
    config.commandCapacity = 8;
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    config.solverSubStepCount = 1;
    return config;
}

[[nodiscard]] Core::Result<Navigation2D::NavigationGrid2D> makeGrid(
    Core::usize blockerCapacity = 8,
    Core::u32 width = 8,
    Core::u32 height = 8,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    std::vector<Core::u8> flags(static_cast<std::size_t>(width) * height, 0U);
    std::vector<Core::u8> costs(flags.size(), 1U);
    auto data = Navigation2D::NavigationGrid2DData::Create(
        Navigation2D::NavigationGrid2DDataDesc{
            .widthCells = width,
            .heightCells = height,
            .originXMeters = 0.0F,
            .originYMeters = 0.0F,
            .cellSizeMeters = 1.0F,
            .cellFlags = flags,
            .traversalCosts = costs},
        resource);
    if (!data) {
        return Core::failure(std::move(data.error()));
    }
    return Navigation2D::NavigationGrid2D::Create(
        std::move(*data),
        Navigation2D::NavigationGrid2DConfig{.dynamicBlockerCapacity = blockerCapacity},
        resource);
}

[[nodiscard]] Core::Result<Physics2D::PhysicsBodyId> makeBody(
    Physics2D::PhysicsWorld2D& world,
    Physics2D::PhysicsVec2 position = {1.5F, 1.5F},
    float angle = 0.0F)
{
    Physics2D::PhysicsBody2DDesc desc;
    desc.type = Physics2D::PhysicsBodyType2D::Kinematic;
    desc.positionMeters = position;
    desc.angleRadians = angle;
    return world.createBody(desc);
}

[[nodiscard]] PhysicsNavigationBody2DDesc boundsFor(
    Physics2D::PhysicsBodyId body,
    Physics2D::PhysicsVec2 lower,
    Physics2D::PhysicsVec2 upper) noexcept
{
    return PhysicsNavigationBody2DDesc{
        .body = body,
        .localBoundsMeters = Physics2D::PhysicsAabb2D{
            .lowerMeters = lower,
            .upperMeters = upper}};
}

class CountingResource final : public std::pmr::memory_resource {
public:
    explicit CountingResource(std::pmr::memory_resource& upstream) noexcept
        : m_upstream(upstream)
    {
    }

    [[nodiscard]] Core::usize allocations() const noexcept { return m_allocations; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++m_allocations;
        return m_upstream.allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        m_upstream.deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::memory_resource& m_upstream;
    Core::usize m_allocations = 0;
};

TEST(PhysicsNavigationSync2DTest, CreateAndRegistrationValidateContracts)
{
    auto invalid = PhysicsNavigationSync2D::Create({.registrationCapacity = 0});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, AssetErrorCode::PhysicsNavigationCapacityExceeded);

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult) << gridResult.error().message;
    auto body = makeBody(*worldResult);
    ASSERT_TRUE(body) << body.error().message;
    auto sync = PhysicsNavigationSync2D::Create({.registrationCapacity = 1});
    ASSERT_TRUE(sync) << sync.error().message;

    EXPECT_FALSE(sync->registerBody(*worldResult, boundsFor(
        *body, {1.0F, 1.0F}, {0.0F, 0.0F})));
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    EXPECT_FALSE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    EXPECT_EQ(sync->stats().registeredBodyCount, 1U);
}

TEST(PhysicsNavigationSync2DTest, InitialAddNoOpAndTransformUpdate)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult) << gridResult.error().message;
    auto body = makeBody(*worldResult, {1.5F, 1.5F});
    ASSERT_TRUE(body) << body.error().message;
    auto sync = PhysicsNavigationSync2D::Create({.registrationCapacity = 2});
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));

    auto first = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(first) << first.error().message;
    EXPECT_EQ(first->lastAddedBlockerCount, 1U);
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 1U);
    const Core::u64 firstRevision = gridResult->revision();

    auto second = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(second) << second.error().message;
    EXPECT_EQ(second->lastUnchangedBlockerCount, 1U);
    EXPECT_EQ(gridResult->revision(), firstRevision);

    ASSERT_TRUE(worldResult->enqueueSetTransform(*body, {3.5F, 1.5F}, 0.0F));
    ASSERT_TRUE(worldResult->step());
    auto moved = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(moved) << moved.error().message;
    EXPECT_EQ(moved->lastUpdatedBlockerCount, 1U);
    EXPECT_EQ(gridResult->dynamicBlockerCountAt({3, 1}), 1U);
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 1U);
}

TEST(PhysicsNavigationSync2DTest, RotationUsesConservativeWorldAabb)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult);
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult);
    auto body = makeBody(*worldResult, {3.5F, 3.5F}, 0.78539816339F);
    ASSERT_TRUE(body);
    auto sync = PhysicsNavigationSync2D::Create();
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-1.0F, -0.25F}, {1.0F, 0.25F})));
    auto result = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(result) << result.error().message;
    // A 2 x 0.5 rectangle rotated 45 degrees spans more than two cells.
    EXPECT_GE(gridResult->dynamicBlockerCountAt({2, 2}), 1U);
    EXPECT_GE(gridResult->dynamicBlockerCountAt({4, 4}), 1U);
}

TEST(PhysicsNavigationSync2DTest, DisabledAndOutsideBodiesTemporarilyUnpublish)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult);
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult);
    auto body = makeBody(*worldResult, {1.5F, 1.5F});
    ASSERT_TRUE(body);
    auto sync = PhysicsNavigationSync2D::Create();
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->synchronize(*worldResult, *gridResult));
    ASSERT_TRUE(worldResult->enqueueSetEnabled(*body, false));
    ASSERT_TRUE(worldResult->step());
    auto disabled = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(disabled);
    EXPECT_EQ(disabled->lastRemovedBlockerCount, 1U);
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 0U);

    ASSERT_TRUE(worldResult->enqueueSetEnabled(*body, true));
    ASSERT_TRUE(worldResult->enqueueSetTransform(*body, {-4.0F, -4.0F}, 0.0F));
    ASSERT_TRUE(worldResult->step());
    auto outside = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(outside);
    EXPECT_EQ(outside->lastOutsideGridCount, 1U);
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 0U);

    ASSERT_TRUE(worldResult->enqueueSetTransform(*body, {1.5F, 1.5F}, 0.0F));
    ASSERT_TRUE(worldResult->step());
    auto reentered = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(reentered);
    EXPECT_EQ(reentered->lastAddedBlockerCount, 1U);
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 1U);
}

TEST(PhysicsNavigationSync2DTest, DestroyedBodiesAreRetiredAndRegistrationRemoved)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult);
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult);
    auto body = makeBody(*worldResult);
    ASSERT_TRUE(body);
    auto sync = PhysicsNavigationSync2D::Create();
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->synchronize(*worldResult, *gridResult));
    ASSERT_TRUE(worldResult->destroyBody(*body));
    auto retired = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(retired);
    EXPECT_EQ(retired->lastRetiredBodyCount, 1U);
    EXPECT_EQ(retired->lastRemovedBlockerCount, 1U);
    EXPECT_FALSE(sync->contains(*body));
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 0U);
}

TEST(PhysicsNavigationSync2DTest, CapacityFailureLeavesPublishedStateUnchanged)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig(4));
    ASSERT_TRUE(worldResult);
    auto gridResult = makeGrid(1);
    ASSERT_TRUE(gridResult);
    auto firstBody = makeBody(*worldResult, {1.5F, 1.5F});
    auto secondBody = makeBody(*worldResult, {3.5F, 1.5F});
    ASSERT_TRUE(firstBody);
    ASSERT_TRUE(secondBody);
    auto sync = PhysicsNavigationSync2D::Create({.registrationCapacity = 2});
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *firstBody, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *secondBody, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    auto failed = sync->synchronize(*worldResult, *gridResult);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, AssetErrorCode::PhysicsNavigationCapacityExceeded);
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 0U);
    EXPECT_EQ(sync->stats().synchronizeCount, 0U);
}

TEST(PhysicsNavigationSync2DTest, WrongOwnerFailsClosed)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult);
    auto otherWorldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(otherWorldResult);
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult);
    auto otherGridResult = makeGrid();
    ASSERT_TRUE(otherGridResult);
    auto body = makeBody(*worldResult);
    ASSERT_TRUE(body);
    auto sync = PhysicsNavigationSync2D::Create();
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->synchronize(*worldResult, *gridResult));
    EXPECT_FALSE(sync->synchronize(*otherWorldResult, *gridResult));
    EXPECT_FALSE(sync->synchronize(*worldResult, *otherGridResult));

}

TEST(PhysicsNavigationSync2DTest, ExternalBlockerMutationFailsClosed)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult);
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult);
    auto body = makeBody(*worldResult);
    ASSERT_TRUE(body);
    auto sync = PhysicsNavigationSync2D::Create();
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->synchronize(*worldResult, *gridResult));

    const Navigation2D::NavigationBlockerId blocker =
        PhysicsNavigationSync2DTestAccess::publishedBlocker(*sync, *body);
    ASSERT_TRUE(gridResult->updateBlocker(
        blocker, {.x = 5, .y = 5, .width = 1, .height = 1}));

    auto rejected = sync->synchronize(*worldResult, *gridResult);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, AssetErrorCode::PhysicsNavigationContractMismatch);
    EXPECT_EQ(sync->stats().synchronizeCount, 1U);
    EXPECT_EQ(gridResult->blockerRect(blocker),
              (Navigation2D::NavigationCellRect2D{.x = 5, .y = 5, .width = 1, .height = 1}));

    ASSERT_TRUE(gridResult->updateBlocker(
        blocker, {.x = 1, .y = 1, .width = 1, .height = 1}));
    ASSERT_TRUE(sync->shutdown(*gridResult));
}

TEST(PhysicsNavigationSync2DTest, ShutdownIsIdempotentAndSteadyStateDoesNotAllocate)
{
    std::pmr::unsynchronized_pool_resource upstream;
    CountingResource counting(upstream);
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig(), counting);
    ASSERT_TRUE(worldResult);
    auto gridResult = makeGrid(4, 8, 8, counting);
    ASSERT_TRUE(gridResult);
    auto body = makeBody(*worldResult);
    ASSERT_TRUE(body);
    auto sync = PhysicsNavigationSync2D::Create({
        .registrationCapacity = 2,
        .memoryResource = &counting});
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *body, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->synchronize(*worldResult, *gridResult));
    const Core::usize allocationsAfterFirstSync = counting.allocations();
    ASSERT_TRUE(sync->synchronize(*worldResult, *gridResult));
    EXPECT_EQ(counting.allocations(), allocationsAfterFirstSync);
    ASSERT_TRUE(sync->shutdown(*gridResult));
    ASSERT_TRUE(sync->shutdown(*gridResult));
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 0U);
}

} // namespace
} // namespace Tina::Asset
