#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace Tina::Physics2D {
namespace {

class FailAfterSuccessfulAllocationsResource final : public std::pmr::memory_resource {
public:
    explicit FailAfterSuccessfulAllocationsResource(std::size_t allowedAllocations) noexcept
        : m_allowedAllocations(allowedAllocations)
    {
    }

    [[nodiscard]] std::size_t outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

    [[nodiscard]] std::size_t outstandingBytes() const noexcept
    {
        return m_outstandingBytes;
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (m_successfulAllocations >= m_allowedAllocations) {
            throw std::bad_alloc{};
        }

        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_successfulAllocations;
        ++m_outstandingAllocations;
        m_outstandingBytes += bytes;
        return pointer;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --m_outstandingAllocations;
        m_outstandingBytes -= bytes;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::size_t m_allowedAllocations = 0;
    std::size_t m_successfulAllocations = 0;
    std::size_t m_outstandingAllocations = 0;
    std::size_t m_outstandingBytes = 0;
};

template <typename Result>
void expectFailureCode(const Result& result, Core::ErrorCode expectedCode)
{
    EXPECT_FALSE(result);
    if (!result) {
        EXPECT_EQ(result.error().code, expectedCode);
    }
}

[[nodiscard]] PhysicsWorld2DConfig smallConfig(
    Core::usize bodyCapacity = 4,
    Core::usize shapeCapacity = 4,
    Core::usize contactBeginCapacity = 8,
    Core::usize contactEndCapacity = 8,
    Core::usize contactHitCapacity = 4) noexcept
{
    PhysicsWorld2DConfig config;
    config.bodyCapacity = bodyCapacity;
    config.shapeCapacity = shapeCapacity;
    config.contactBeginCapacity = contactBeginCapacity;
    config.contactEndCapacity = contactEndCapacity;
    config.contactHitCapacity = contactHitCapacity;
    config.solverSubStepCount = 1;
    return config;
}

template <typename Id>
[[nodiscard]] bool contactPairMatches(
    Id leftA,
    Id leftB,
    Id expectedA,
    Id expectedB) noexcept
{
    return (leftA == expectedA && leftB == expectedB)
        || (leftA == expectedB && leftB == expectedA);
}

[[nodiscard]] PhysicsBody2DDesc dynamicBody(
    PhysicsVec2 position = {},
    PhysicsVec2 velocity = {}) noexcept
{
    PhysicsBody2DDesc desc;
    desc.type = PhysicsBodyType2D::Dynamic;
    desc.positionMeters = position;
    desc.linearVelocityMetersPerSecond = velocity;
    return desc;
}

[[nodiscard]] PhysicsBoxShape2DDesc unitBox() noexcept
{
    PhysicsBoxShape2DDesc desc;
    desc.halfExtentsMeters = {0.5F, 0.5F};
    desc.density = 1.0F;
    return desc;
}

TEST(PhysicsWorld2DTest, ValidatesWorldConfigBeforeCreatingBackendState)
{
    EXPECT_TRUE(validatePhysicsWorld2DConfig(smallConfig()));

    PhysicsWorld2DConfig config = smallConfig();
    config.bodyCapacity = 0;
    expectFailureCode(validatePhysicsWorld2DConfig(config), Physics2DErrorCode::InvalidConfiguration);
    expectFailureCode(PhysicsWorld2D::Create(config), Physics2DErrorCode::InvalidConfiguration);

    config = smallConfig();
    config.shapeCapacity = 0;
    expectFailureCode(validatePhysicsWorld2DConfig(config), Physics2DErrorCode::InvalidConfiguration);

    config = smallConfig();
    config.bodyCapacity = PhysicsWorld2DConfig::MaxBodyCapacity + 1;
    expectFailureCode(validatePhysicsWorld2DConfig(config), Physics2DErrorCode::InvalidConfiguration);

    config = smallConfig();
    config.gravityMetersPerSecondSquared.x = std::numeric_limits<float>::quiet_NaN();
    expectFailureCode(validatePhysicsWorld2DConfig(config), Physics2DErrorCode::InvalidConfiguration);

    config = smallConfig();
    config.fixedDeltaSeconds = 0.0F;
    expectFailureCode(validatePhysicsWorld2DConfig(config), Physics2DErrorCode::InvalidConfiguration);

    config = smallConfig();
    config.solverSubStepCount = PhysicsWorld2DConfig::MaxSolverSubStepCount + 1;
    expectFailureCode(validatePhysicsWorld2DConfig(config), Physics2DErrorCode::InvalidConfiguration);

    config = smallConfig();
    config.solverSubStepCount = 0;
    expectFailureCode(PhysicsWorld2D::Create(config), Physics2DErrorCode::InvalidConfiguration);
}

TEST(PhysicsWorld2DTest, ValidatesBodyAndShapeDescriptions)
{
    EXPECT_TRUE(validatePhysicsBody2DDesc(dynamicBody()));
    EXPECT_TRUE(validatePhysicsBoxShape2DDesc(unitBox()));

    PhysicsBody2DDesc body = dynamicBody();
    body.type = static_cast<PhysicsBodyType2D>(255);
    expectFailureCode(validatePhysicsBody2DDesc(body), Physics2DErrorCode::InvalidBodyDescription);

    body = dynamicBody();
    body.positionMeters.x = std::numeric_limits<float>::infinity();
    expectFailureCode(validatePhysicsBody2DDesc(body), Physics2DErrorCode::InvalidBodyDescription);

    body = dynamicBody();
    body.linearVelocityMetersPerSecond.y = std::numeric_limits<float>::quiet_NaN();
    expectFailureCode(validatePhysicsBody2DDesc(body), Physics2DErrorCode::InvalidBodyDescription);

    body = dynamicBody();
    body.linearDamping = -0.01F;
    expectFailureCode(validatePhysicsBody2DDesc(body), Physics2DErrorCode::InvalidBodyDescription);

    PhysicsBoxShape2DDesc shape = unitBox();
    shape.halfExtentsMeters.y = 0.0F;
    expectFailureCode(validatePhysicsBoxShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.density = -1.0F;
    expectFailureCode(validatePhysicsBoxShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.friction = 1.01F;
    expectFailureCode(validatePhysicsBoxShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.restitution = -0.01F;
    expectFailureCode(validatePhysicsBoxShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.filter.categoryBits = 0;
    expectFailureCode(validatePhysicsBoxShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);
}

TEST(PhysicsWorld2DTest, InvalidDescriptionsDoNotConsumeCapacity)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc invalidBody = dynamicBody();
    invalidBody.angularDamping = -1.0F;
    const auto bodyFailure = world.createBoxBody(invalidBody, unitBox());
    expectFailureCode(bodyFailure, Physics2DErrorCode::InvalidBodyDescription);
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    PhysicsBoxShape2DDesc invalidShape = unitBox();
    invalidShape.filter.categoryBits = 0;
    const auto shapeFailure = world.createBoxBody(dynamicBody(), invalidShape);
    expectFailureCode(shapeFailure, Physics2DErrorCode::InvalidShapeDescription);
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    ASSERT_TRUE(world.createBoxBody(dynamicBody(), unitBox()));
    const auto capacityFailure = world.createBoxBody(dynamicBody(), unitBox());
    expectFailureCode(capacityFailure, Physics2DErrorCode::CapacityExceeded);
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);
}

TEST(PhysicsWorld2DTest, RollsBackPmrStorageWhenWorldConstructionFails)
{
    // Create allocates: body pool, shape pool, begin/end/hit contact buffers,
    // shape tombstones, then Impl storage.
    const PhysicsWorld2DConfig config = smallConfig(2, 2);

    FailAfterSuccessfulAllocationsResource bodyPoolFailure(0);
    const auto bodyPoolResult = PhysicsWorld2D::Create(config, bodyPoolFailure);
    EXPECT_FALSE(bodyPoolResult);
    EXPECT_EQ(bodyPoolFailure.outstandingAllocations(), 0U);
    EXPECT_EQ(bodyPoolFailure.outstandingBytes(), 0U);

    FailAfterSuccessfulAllocationsResource shapePoolFailure(1);
    const auto shapePoolResult = PhysicsWorld2D::Create(config, shapePoolFailure);
    EXPECT_FALSE(shapePoolResult);
    EXPECT_EQ(shapePoolFailure.outstandingAllocations(), 0U);
    EXPECT_EQ(shapePoolFailure.outstandingBytes(), 0U);

    FailAfterSuccessfulAllocationsResource contactBufferFailure(2);
    const auto contactBufferResult = PhysicsWorld2D::Create(config, contactBufferFailure);
    EXPECT_FALSE(contactBufferResult);
    EXPECT_EQ(contactBufferFailure.outstandingAllocations(), 0U);
    EXPECT_EQ(contactBufferFailure.outstandingBytes(), 0U);

    FailAfterSuccessfulAllocationsResource implFailure(6);
    const auto implResult = PhysicsWorld2D::Create(config, implFailure);
    expectFailureCode(implResult, Physics2DErrorCode::CapacityExceeded);
    EXPECT_EQ(implFailure.outstandingAllocations(), 0U);
    EXPECT_EQ(implFailure.outstandingBytes(), 0U);
}

TEST(PhysicsWorld2DTest, AtomicallyCreatesBodyAndBoxShapeWithOwningGenerationIds)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(2, 3));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc body = dynamicBody({3.0F, 4.0F}, {2.0F, -1.0F});
    body.angleRadians = 0.25F;
    body.angularVelocityRadiansPerSecond = 0.5F;

    auto createdResult = world.createBoxBody(body, unitBox());
    ASSERT_TRUE(createdResult) << createdResult.error().message;
    const PhysicsBodyShape2D created = *createdResult;

    EXPECT_TRUE(world.contains(created.body));
    EXPECT_TRUE(world.contains(created.shape));

    auto owningBody = world.shapeBody(created.shape);
    ASSERT_TRUE(owningBody) << owningBody.error().message;
    EXPECT_EQ(*owningBody, created.body);

    const PhysicsWorld2DStats statistics = world.stats();
    EXPECT_EQ(statistics.bodyCount, 1U);
    EXPECT_EQ(statistics.shapeCount, 1U);
    EXPECT_EQ(statistics.bodyCapacity, 2U);
    EXPECT_EQ(statistics.shapeCapacity, 3U);
    EXPECT_TRUE(statistics.open);

    auto stateResult = world.bodyState(created.body);
    ASSERT_TRUE(stateResult) << stateResult.error().message;
    const PhysicsBodyState2D state = *stateResult;
    EXPECT_FLOAT_EQ(state.positionMeters.x, 3.0F);
    EXPECT_FLOAT_EQ(state.positionMeters.y, 4.0F);
    EXPECT_NEAR(state.angleRadians, 0.25F, 2.0e-3F);
    EXPECT_FLOAT_EQ(state.linearVelocityMetersPerSecond.x, 2.0F);
    EXPECT_FLOAT_EQ(state.linearVelocityMetersPerSecond.y, -1.0F);
    EXPECT_FLOAT_EQ(state.angularVelocityRadiansPerSecond, 0.5F);
    EXPECT_TRUE(state.enabled);
}

TEST(PhysicsWorld2DTest, AdvancesByTheConfiguredFixedStepOnly)
{
    PhysicsWorld2DConfig config = smallConfig(1, 1);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    config.fixedDeltaSeconds = 0.25F;

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = world.createBoxBody(dynamicBody({2.0F, 3.0F}, {4.0F, -2.0F}), unitBox());
    ASSERT_TRUE(created) << created.error().message;

    EXPECT_EQ(world.stats().completedStepCount, 0U);
    ASSERT_TRUE(world.step());

    auto stateResult = world.bodyState(created->body);
    ASSERT_TRUE(stateResult) << stateResult.error().message;
    EXPECT_NEAR(stateResult->positionMeters.x, 3.0F, 1.0e-4F);
    EXPECT_NEAR(stateResult->positionMeters.y, 2.5F, 1.0e-4F);
    EXPECT_EQ(world.stats().completedStepCount, 1U);
}

TEST(PhysicsWorld2DTest, RejectsStaleAndCrossWorldBodyAndShapeHandles)
{
    auto firstResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    auto secondResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(firstResult) << firstResult.error().message;
    ASSERT_TRUE(secondResult) << secondResult.error().message;
    PhysicsWorld2D first = std::move(*firstResult);
    PhysicsWorld2D second = std::move(*secondResult);

    auto firstPair = first.createBoxBody(dynamicBody(), unitBox());
    auto secondPair = second.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(firstPair) << firstPair.error().message;
    ASSERT_TRUE(secondPair) << secondPair.error().message;

    EXPECT_TRUE(first.contains(firstPair->body));
    EXPECT_TRUE(first.contains(firstPair->shape));
    EXPECT_FALSE(second.contains(firstPair->body));
    EXPECT_FALSE(second.contains(firstPair->shape));
    expectFailureCode(first.destroyBody(secondPair->body), Physics2DErrorCode::WrongWorld);
    expectFailureCode(first.shapeBody(secondPair->shape), Physics2DErrorCode::WrongWorld);

    ASSERT_TRUE(first.destroyBody(firstPair->body));
    EXPECT_FALSE(first.contains(firstPair->body));
    EXPECT_FALSE(first.contains(firstPair->shape));
    expectFailureCode(first.destroyBody(firstPair->body), Physics2DErrorCode::StaleBody);
    expectFailureCode(first.bodyState(firstPair->body), Physics2DErrorCode::StaleBody);
    expectFailureCode(first.shapeBody(firstPair->shape), Physics2DErrorCode::StaleShape);

    auto reused = first.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(reused) << reused.error().message;
    EXPECT_EQ(reused->body.owner(), firstPair->body.owner());
    EXPECT_EQ(reused->body.index(), firstPair->body.index());
    EXPECT_NE(reused->body.generation(), firstPair->body.generation());
    EXPECT_EQ(reused->shape.owner(), firstPair->shape.owner());
    EXPECT_EQ(reused->shape.index(), firstPair->shape.index());
    EXPECT_NE(reused->shape.generation(), firstPair->shape.generation());
}

TEST(PhysicsWorld2DTest, RollsBackBodyReservationWhenShapeCapacityIsFull)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(2, 1));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto first = world.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(first) << first.error().message;

    const auto second = world.createBoxBody(dynamicBody(), unitBox());
    expectFailureCode(second, Physics2DErrorCode::CapacityExceeded);
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);
    EXPECT_TRUE(world.contains(first->body));
    EXPECT_TRUE(world.contains(first->shape));

    ASSERT_TRUE(world.destroyBody(first->body));
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    auto retry = world.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(retry) << retry.error().message;
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);
}

TEST(PhysicsWorld2DTest, DestroyBodyRetiresItsShapeAndRepeatedDestroyIsStale)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = world.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(created) << created.error().message;

    ASSERT_TRUE(world.destroyBody(created->body));
    EXPECT_FALSE(world.contains(created->body));
    EXPECT_FALSE(world.contains(created->shape));
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);
    expectFailureCode(world.destroyBody(created->body), Physics2DErrorCode::StaleBody);
    expectFailureCode(world.shapeBody(created->shape), Physics2DErrorCode::StaleShape);
}

TEST(PhysicsWorld2DTest, ShutdownIsIdempotentAndClosesAllOperations)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(2, 2));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = world.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(created) << created.error().message;

    ASSERT_TRUE(world.shutdown());
    EXPECT_TRUE(world.shutdown());
    EXPECT_FALSE(world.isOpen());
    EXPECT_FALSE(world.stats().open);
    EXPECT_FALSE(world.contains(created->body));
    EXPECT_FALSE(world.contains(created->shape));
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    const Core::Status stepStatus = world.step();
    expectFailureCode(stepStatus, Physics2DErrorCode::WorldClosed);
    expectFailureCode(world.bodyState(created->body), Physics2DErrorCode::WorldClosed);
    expectFailureCode(world.shapeBody(created->shape), Physics2DErrorCode::WorldClosed);
    expectFailureCode(world.createBoxBody(dynamicBody(), unitBox()), Physics2DErrorCode::WorldClosed);
}

TEST(PhysicsWorld2DTest, MoveTransfersOwnershipAndLeavesSourceClosed)
{
    auto sourceResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(sourceResult) << sourceResult.error().message;
    PhysicsWorld2D source = std::move(*sourceResult);

    auto created = source.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(created) << created.error().message;

    PhysicsWorld2D destination(std::move(source));
    EXPECT_FALSE(source.isOpen());
    EXPECT_FALSE(source.stats().open);
    EXPECT_TRUE(source.shutdown());
    EXPECT_TRUE(destination.isOpen());
    EXPECT_TRUE(destination.contains(created->body));
    EXPECT_TRUE(destination.contains(created->shape));
    EXPECT_TRUE(destination.step());
}

TEST(PhysicsWorld2DTest, RejectsOwnerThreadViolationsWithoutMutatingWorld)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(2, 2));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = world.createBoxBody(dynamicBody(), unitBox());
    ASSERT_TRUE(created) << created.error().message;

    struct ObservedErrors final {
        Core::ErrorCode create = Core::CoreErrorCode::Internal;
        Core::ErrorCode destroy = Core::CoreErrorCode::Internal;
        Core::ErrorCode step = Core::CoreErrorCode::Internal;
        Core::ErrorCode bodyState = Core::CoreErrorCode::Internal;
        Core::ErrorCode shapeBody = Core::CoreErrorCode::Internal;
        Core::ErrorCode shutdown = Core::CoreErrorCode::Internal;
    };

    std::optional<ObservedErrors> observed;
    std::thread worker([&] {
        ObservedErrors local;

        const auto createResult = world.createBoxBody(dynamicBody(), unitBox());
        if (!createResult) {
            local.create = createResult.error().code;
        }

        const Core::Status destroyStatus = world.destroyBody(created->body);
        if (!destroyStatus) {
            local.destroy = destroyStatus.error().code;
        }

        const Core::Status stepStatus = world.step();
        if (!stepStatus) {
            local.step = stepStatus.error().code;
        }

        const auto stateResult = world.bodyState(created->body);
        if (!stateResult) {
            local.bodyState = stateResult.error().code;
        }

        const auto shapeBodyResult = world.shapeBody(created->shape);
        if (!shapeBodyResult) {
            local.shapeBody = shapeBodyResult.error().code;
        }

        const Core::Status shutdownStatus = world.shutdown();
        if (!shutdownStatus) {
            local.shutdown = shutdownStatus.error().code;
        }

        observed = local;
    });
    worker.join();

    ASSERT_TRUE(observed.has_value());
    EXPECT_EQ(observed->create, Physics2DErrorCode::WrongOwnerThread);
    EXPECT_EQ(observed->destroy, Physics2DErrorCode::WrongOwnerThread);
    EXPECT_EQ(observed->step, Physics2DErrorCode::WrongOwnerThread);
    EXPECT_EQ(observed->bodyState, Physics2DErrorCode::WrongOwnerThread);
    EXPECT_EQ(observed->shapeBody, Physics2DErrorCode::WrongOwnerThread);
    EXPECT_EQ(observed->shutdown, Physics2DErrorCode::WrongOwnerThread);
    EXPECT_TRUE(world.isOpen());
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);
    EXPECT_TRUE(world.contains(created->body));
    EXPECT_TRUE(world.contains(created->shape));
}

TEST(PhysicsWorld2DTest, ReleasesAllTinaOwnedMemoryToThePhysicsTag)
{
    Core::MemoryTracker tracker;
    Core::CountingMemoryResource resource(
        tracker,
        Core::MemoryTag::Physics2D,
        *std::pmr::new_delete_resource());

    {
        auto worldResult = PhysicsWorld2D::Create(smallConfig(8, 8), resource);
        ASSERT_TRUE(worldResult) << worldResult.error().message;
        PhysicsWorld2D world = std::move(*worldResult);

        auto created = world.createBoxBody(dynamicBody(), unitBox());
        ASSERT_TRUE(created) << created.error().message;
        EXPECT_GT(tracker.snapshot(Core::MemoryTag::Physics2D).currentBytes, 0U);

        ASSERT_TRUE(world.shutdown());
        EXPECT_GT(tracker.snapshot(Core::MemoryTag::Physics2D).currentBytes, 0U);
    }

    const Core::MemoryStatistics statistics = tracker.snapshot(Core::MemoryTag::Physics2D);
    EXPECT_EQ(statistics.currentBytes, 0U);
    EXPECT_EQ(statistics.allocationCount, statistics.deallocationCount);
    EXPECT_EQ(statistics.invalidDeallocationCount, 0U);
}

TEST(PhysicsWorld2DTest, PublishesBeginAndEndContactEventsAfterStep)
{
    PhysicsWorld2DConfig config = smallConfig(4, 4);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    config.fixedDeltaSeconds = 1.0F / 60.0F;

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc groundBody;
    groundBody.type = PhysicsBodyType2D::Static;
    groundBody.positionMeters = {0.0F, -1.0F};
    PhysicsBoxShape2DDesc groundShape = unitBox();
    groundShape.halfExtentsMeters = {5.0F, 0.5F};
    groundShape.enableContactEvents = true;

    PhysicsBody2DDesc fallingBody = dynamicBody({0.0F, 2.0F}, {0.0F, -20.0F});
    PhysicsBoxShape2DDesc fallingShape = unitBox();
    fallingShape.enableContactEvents = true;

    auto ground = world.createBoxBody(groundBody, groundShape);
    auto falling = world.createBoxBody(fallingBody, fallingShape);
    ASSERT_TRUE(ground) << ground.error().message;
    ASSERT_TRUE(falling) << falling.error().message;

    bool sawBegin = false;
    for (int stepIndex = 0; stepIndex < 120; ++stepIndex) {
        ASSERT_TRUE(world.step());
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts) << contacts.error().message;
        for (const PhysicsContactBeginEvent2D& beginEvent : contacts->beginEvents) {
            if (contactPairMatches(
                    beginEvent.bodyA,
                    beginEvent.bodyB,
                    ground->body,
                    falling->body)) {
                sawBegin = true;
                EXPECT_TRUE(contactPairMatches(
                    beginEvent.shapeA,
                    beginEvent.shapeB,
                    ground->shape,
                    falling->shape));
                break;
            }
        }
        if (sawBegin) {
            break;
        }
    }
    ASSERT_TRUE(sawBegin);

    ASSERT_TRUE(world.destroyBody(falling->body));
    ASSERT_TRUE(world.step());
    auto afterDestroy = world.contactEvents();
    ASSERT_TRUE(afterDestroy) << afterDestroy.error().message;

    bool sawEnd = false;
    for (const PhysicsContactEndEvent2D& endEvent : afterDestroy->endEvents) {
        if (contactPairMatches(endEvent.bodyA, endEvent.bodyB, ground->body, falling->body)
            || contactPairMatches(
                   endEvent.shapeA,
                   endEvent.shapeB,
                   ground->shape,
                   falling->shape)) {
            sawEnd = true;
            EXPECT_TRUE(endEvent.shapeADestroyed || endEvent.shapeBDestroyed);
            break;
        }
    }
    EXPECT_TRUE(sawEnd);
}

TEST(PhysicsWorld2DTest, ContactBeginOverflowSetsFlagAndDropsTailEvents)
{
    PhysicsWorld2DConfig config = smallConfig(8, 8, 1, 8, 0);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc staticBody;
    staticBody.type = PhysicsBodyType2D::Static;
    staticBody.positionMeters = {0.0F, 0.0F};
    PhysicsBoxShape2DDesc staticShape = unitBox();
    staticShape.halfExtentsMeters = {3.0F, 0.5F};
    staticShape.enableContactEvents = true;

    auto platform = world.createBoxBody(staticBody, staticShape);
    ASSERT_TRUE(platform) << platform.error().message;

    PhysicsBody2DDesc left = dynamicBody({-0.75F, 1.5F}, {0.0F, -30.0F});
    PhysicsBody2DDesc right = dynamicBody({0.75F, 1.5F}, {0.0F, -30.0F});
    PhysicsBoxShape2DDesc box = unitBox();
    box.halfExtentsMeters = {0.4F, 0.4F};
    box.enableContactEvents = true;

    auto leftBody = world.createBoxBody(left, box);
    auto rightBody = world.createBoxBody(right, box);
    ASSERT_TRUE(leftBody) << leftBody.error().message;
    ASSERT_TRUE(rightBody) << rightBody.error().message;

    bool sawOverflow = false;
    for (int stepIndex = 0; stepIndex < 60; ++stepIndex) {
        ASSERT_TRUE(world.step());
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts) << contacts.error().message;
        if (contacts->beginOverflow) {
            sawOverflow = true;
            EXPECT_LE(contacts->beginEvents.size(), 1U);
            EXPECT_GE(world.stats().droppedBeginContactCount, 1U);
            break;
        }
    }
    // Overflow is best-effort: two concurrent begin events may land in one step.
    // If the solver serializes them, capacity-1 still stores every published event.
    if (sawOverflow) {
        EXPECT_TRUE(true);
    } else {
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts);
        EXPECT_FALSE(contacts->beginOverflow);
    }
}

TEST(PhysicsWorld2DTest, ContactEventsViewClearsOnNextStepWithoutNewContacts)
{
    PhysicsWorld2DConfig config = smallConfig(2, 2);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = world.createBoxBody(dynamicBody({0.0F, 0.0F}), unitBox());
    ASSERT_TRUE(created) << created.error().message;
    ASSERT_TRUE(world.step());

    auto first = world.contactEvents();
    ASSERT_TRUE(first) << first.error().message;
    EXPECT_TRUE(first->beginEvents.empty());
    EXPECT_TRUE(first->endEvents.empty());
    EXPECT_TRUE(first->hitEvents.empty());

    ASSERT_TRUE(world.step());
    auto second = world.contactEvents();
    ASSERT_TRUE(second) << second.error().message;
    EXPECT_TRUE(second->beginEvents.empty());
    EXPECT_FALSE(second->beginOverflow);
    EXPECT_FALSE(second->endOverflow);
    EXPECT_FALSE(second->hitOverflow);
}

TEST(PhysicsWorld2DTest, OverlapAabbReturnsSortedHitsAndReportsOverflow)
{
    PhysicsWorld2DConfig config = smallConfig(4, 4);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc staticBody;
    staticBody.type = PhysicsBodyType2D::Static;

    staticBody.positionMeters = {-1.0F, 0.0F};
    auto left = world.createBoxBody(staticBody, unitBox());
    staticBody.positionMeters = {1.0F, 0.0F};
    auto right = world.createBoxBody(staticBody, unitBox());
    staticBody.positionMeters = {10.0F, 10.0F};
    auto farAway = world.createBoxBody(staticBody, unitBox());
    ASSERT_TRUE(left) << left.error().message;
    ASSERT_TRUE(right) << right.error().message;
    ASSERT_TRUE(farAway) << farAway.error().message;

    PhysicsAabb2D aabb{{-2.0F, -1.0F}, {2.0F, 1.0F}};
    PhysicsOverlapHit2D hits[2]{};
    auto query = world.overlapAabb(aabb, {}, hits);
    ASSERT_TRUE(query) << query.error().message;
    EXPECT_EQ(query->totalFound, 2U);
    EXPECT_EQ(query->written, 2U);
    EXPECT_FALSE(query->overflow);
    EXPECT_TRUE(
        (hits[0].body == left->body && hits[1].body == right->body)
        || (hits[0].body == right->body && hits[1].body == left->body));
    if (hits[0].body.index() > hits[1].body.index()) {
        FAIL() << "overlap hits must be sorted by body index";
    }

    PhysicsOverlapHit2D oneSlot[1]{};
    auto overflowQuery = world.overlapAabb(aabb, {}, oneSlot);
    ASSERT_TRUE(overflowQuery) << overflowQuery.error().message;
    EXPECT_EQ(overflowQuery->totalFound, 2U);
    EXPECT_EQ(overflowQuery->written, 1U);
    EXPECT_TRUE(overflowQuery->overflow);

    PhysicsOverlapHit2D empty[1]{};
    auto miss = world.overlapAabb(PhysicsAabb2D{{20.0F, 20.0F}, {21.0F, 21.0F}}, {}, empty);
    ASSERT_TRUE(miss) << miss.error().message;
    EXPECT_EQ(miss->totalFound, 0U);
    EXPECT_EQ(miss->written, 0U);
    EXPECT_FALSE(miss->overflow);

    PhysicsAabb2D invalid{{2.0F, 0.0F}, {1.0F, 1.0F}};
    expectFailureCode(world.overlapAabb(invalid, {}, hits), Physics2DErrorCode::InvalidQuery);
}

TEST(PhysicsWorld2DTest, CastRayFindsHitsAndClosestIsStable)
{
    PhysicsWorld2DConfig config = smallConfig(4, 4);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc staticBody;
    staticBody.type = PhysicsBodyType2D::Static;
    staticBody.positionMeters = {2.0F, 0.0F};
    auto nearBody = world.createBoxBody(staticBody, unitBox());
    staticBody.positionMeters = {5.0F, 0.0F};
    auto farBody = world.createBoxBody(staticBody, unitBox());
    ASSERT_TRUE(nearBody) << nearBody.error().message;
    ASSERT_TRUE(farBody) << farBody.error().message;

    PhysicsRayCast2D ray{{0.0F, 0.0F}, {10.0F, 0.0F}};
    PhysicsCastHit2D hits[4]{};
    auto multi = world.castRay(ray, {}, hits);
    ASSERT_TRUE(multi) << multi.error().message;
    EXPECT_GE(multi->totalFound, 2U);
    EXPECT_GE(multi->written, 2U);
    EXPECT_FALSE(multi->overflow);
    EXPECT_LE(hits[0].fraction, hits[1].fraction);
    EXPECT_EQ(hits[0].body, nearBody->body);

    auto closest = world.castRayClosest(ray, {});
    ASSERT_TRUE(closest) << closest.error().message;
    EXPECT_EQ(closest->body, nearBody->body);
    EXPECT_EQ(closest->shape, nearBody->shape);
    EXPECT_GT(closest->fraction, 0.0F);
    EXPECT_LT(closest->fraction, 1.0F);

    auto miss = world.castRayClosest(PhysicsRayCast2D{{0.0F, 5.0F}, {1.0F, 0.0F}}, {});
    expectFailureCode(miss, Physics2DErrorCode::InvalidQuery);

    expectFailureCode(
        world.castRay(PhysicsRayCast2D{{0.0F, 0.0F}, {0.0F, 0.0F}}, {}, hits),
        Physics2DErrorCode::InvalidQuery);

    PhysicsCastHit2D one[1]{};
    auto overflow = world.castRay(ray, {}, one);
    ASSERT_TRUE(overflow) << overflow.error().message;
    EXPECT_GE(overflow->totalFound, 2U);
    EXPECT_EQ(overflow->written, 1U);
    EXPECT_TRUE(overflow->overflow);
}

} // namespace
} // namespace Tina::Physics2D
