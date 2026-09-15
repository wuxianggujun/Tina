#include <tina/asset/AssetErrors.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/asset/PhysicsNavigationSync2D.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <memory_resource>
#include <new>
#include <utility>
#include <vector>

namespace Tina::Asset {

struct PhysicsNavigationSync2DTestAccess final {
    [[nodiscard]] static Navigation2D::NavigationBlockerId publishedBlocker(
        const PhysicsNavigationSync2D& sync,
        Physics2D::PhysicsBodyId body) noexcept
    {
        return sync.m_storage->records[sync.findRecord(body)].blocker;
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
    std::vector<Core::u8> flags(static_cast<Tina::Core::usize>(width) * height, 0U);
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
        Navigation2D::NavigationGrid2DConfig{.initialBlockerReserve = blockerCapacity},
        resource);
}

[[nodiscard]] Core::Result<Physics2D::PhysicsBodyId> makeBody(
    Physics2D::PhysicsWorld2D& world,
    Math::Vec2 position = {1.5F, 1.5F},
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
    Math::Vec2 lower,
    Math::Vec2 upper) noexcept
{
    return PhysicsNavigationBody2DDesc{
        .body = body,
        .localBoundsMeters = Physics2D::PhysicsAabb2D{
            .lowerMeters = lower,
            .upperMeters = upper}};
}

class CountingResource final : public std::pmr::memory_resource {
public:
    explicit CountingResource(std::pmr::memory_resource& upstream,
                              Core::usize limit = (std::numeric_limits<Core::usize>::max)()) noexcept
        : m_upstream(upstream), m_remaining(limit)
    {
    }

    [[nodiscard]] Core::usize allocations() const noexcept { return m_allocations; }
    [[nodiscard]] Core::usize liveBytes() const noexcept { return m_liveBytes; }
    void seal() noexcept { m_remaining = 0; }
    void unseal() noexcept { m_remaining = (std::numeric_limits<Core::usize>::max)(); }

private:
    void* do_allocate(Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        if (m_remaining == 0) { throw std::bad_alloc{}; }
        void* pointer = m_upstream.allocate(bytes, alignment);
        --m_remaining;
        ++m_allocations;
        m_liveBytes += bytes;
        return pointer;
    }

    void do_deallocate(void* pointer, Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        m_liveBytes -= bytes;
        m_upstream.deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::memory_resource& m_upstream;
    Core::usize m_allocations = 0;
    Core::usize m_remaining = 0;
    Core::usize m_liveBytes = 0;
};

TEST(PhysicsNavigationSync2DTest, CreateAndRegistrationValidateContracts)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    auto gridResult = makeGrid();
    ASSERT_TRUE(gridResult) << gridResult.error().message;
    auto body = makeBody(*worldResult);
    ASSERT_TRUE(body) << body.error().message;
    auto sync = PhysicsNavigationSync2D::Create({.initialRegistrationReserve = 0});
    ASSERT_TRUE(sync) << sync.error().message;
    EXPECT_EQ(sync->reservedRegistrationSlots(), 0U);

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
    auto sync = PhysicsNavigationSync2D::Create({.initialRegistrationReserve = 2});
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

TEST(PhysicsNavigationSync2DTest, RegistrationsAndGridGrowBeyondTheInitialReserves)
{
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig(4));
    ASSERT_TRUE(worldResult);
    auto gridResult = makeGrid(1);
    ASSERT_TRUE(gridResult);
    auto firstBody = makeBody(*worldResult, {1.5F, 1.5F});
    auto secondBody = makeBody(*worldResult, {3.5F, 1.5F});
    ASSERT_TRUE(firstBody);
    ASSERT_TRUE(secondBody);
    auto sync = PhysicsNavigationSync2D::Create({.initialRegistrationReserve = 1});
    ASSERT_TRUE(sync);
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *firstBody, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->registerBody(*worldResult, boundsFor(
        *secondBody, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    auto published = sync->synchronize(*worldResult, *gridResult);
    ASSERT_TRUE(published);
    EXPECT_EQ(gridResult->dynamicBlockerCount(), 2U);
    EXPECT_GE(gridResult->reservedBlockerSlots(), 2U);
    EXPECT_GE(sync->reservedRegistrationSlots(), 2U);
    EXPECT_EQ(sync->stats().synchronizeCount, 1U);
    ASSERT_TRUE(sync->shutdown(*gridResult));
}

TEST(PhysicsNavigationSync2DTest, GridGrowthFailureDoesNotPartiallyUpdatePublishedBlockers)
{
    CountingResource gridMemory(*std::pmr::new_delete_resource());
    auto world = Physics2D::PhysicsWorld2D::Create(worldConfig());
    auto grid = makeGrid(1, 8, 8, gridMemory);
    auto sync = PhysicsNavigationSync2D::Create({.initialRegistrationReserve = 2});
    ASSERT_TRUE(world);
    ASSERT_TRUE(grid);
    ASSERT_TRUE(sync);
    auto first = makeBody(*world, {1.5F, 1.5F});
    auto second = makeBody(*world, {5.5F, 1.5F});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(sync->registerBody(*world, boundsFor(*first, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    ASSERT_TRUE(sync->synchronize(*world, *grid));
    const auto revision = grid->revision();
    ASSERT_TRUE(world->enqueueSetTransform(*first, {3.5F, 1.5F}, 0.0F));
    ASSERT_TRUE(world->step());
    ASSERT_TRUE(sync->registerBody(*world, boundsFor(*second, {-0.5F, -0.5F}, {0.5F, 0.5F})));
    gridMemory.seal();
    auto rejected = sync->synchronize(*world, *grid);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, Core::CoreErrorCode::OutOfMemory);
    EXPECT_EQ(grid->revision(), revision);
    EXPECT_EQ(grid->dynamicBlockerCountAt({1, 1}), 1U);
    EXPECT_FALSE(grid->isBlocked({3, 1}));
    EXPECT_FALSE(grid->isBlocked({5, 1}));
    EXPECT_EQ(sync->stats().synchronizeCount, 1U);
    gridMemory.unseal();
    ASSERT_TRUE(sync->synchronize(*world, *grid));
    EXPECT_EQ(grid->dynamicBlockerCount(), 2U);
    EXPECT_EQ(grid->dynamicBlockerCountAt({3, 1}), 1U);
    ASSERT_TRUE(sync->shutdown(*grid));
}

TEST(PhysicsNavigationSync2DTest, FactoryFailureAndMovePreservePmrOwnership)
{
    bool reachedSuccess = false;
    for (Core::usize limit = 0; limit < 24 && !reachedSuccess; ++limit) {
        CountingResource memory(*std::pmr::new_delete_resource(), limit);
        {
            auto sync = PhysicsNavigationSync2D::Create({.initialRegistrationReserve = 2, .memoryResource = &memory});
            reachedSuccess = sync.has_value();
            if (sync) {
                memory.seal();
                PhysicsNavigationSync2D moved(std::move(*sync));
                EXPECT_FALSE(*sync);
                EXPECT_EQ(moved.reservedRegistrationSlots(), 2U);
            } else {
                EXPECT_EQ(sync.error().code, AssetErrorCode::AllocationFailed);
            }
        }
        EXPECT_EQ(memory.liveBytes(), 0U) << "allocation limit " << limit;
    }
    EXPECT_TRUE(reachedSuccess);
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
        .initialRegistrationReserve = 2,
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
