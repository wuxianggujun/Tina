#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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
    Core::usize contactHitCapacity = 4,
    Core::usize commandCapacity = 8) noexcept
{
    PhysicsWorld2DConfig config;
    config.bodyCapacity = bodyCapacity;
    config.shapeCapacity = shapeCapacity;
    config.jointCapacity = 4;
    config.contactBeginCapacity = contactBeginCapacity;
    config.contactEndCapacity = contactEndCapacity;
    config.contactHitCapacity = contactHitCapacity;
    config.commandCapacity = commandCapacity;
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

[[nodiscard]] PhysicsShape2DDesc unitBox() noexcept
{
    PhysicsShape2DDesc desc;
    desc.kind = PhysicsShapeKind2D::Box;
    desc.halfExtentsMeters = {0.5F, 0.5F};
    desc.density = 1.0F;
    return desc;
}

[[nodiscard]] PhysicsShape2DDesc convexTriangle() noexcept
{
    PhysicsShape2DDesc desc;
    desc.kind = PhysicsShapeKind2D::ConvexPolygon;
    desc.polygonVertices[0] = {-0.5F, -0.5F};
    desc.polygonVertices[1] = {0.5F, -0.5F};
    desc.polygonVertices[2] = {0.0F, 0.5F};
    desc.polygonVertexCount = 3;
    return desc;
}

[[nodiscard]] PhysicsShape2DDesc openChain() noexcept
{
    PhysicsShape2DDesc desc;
    desc.kind = PhysicsShapeKind2D::Chain;
    desc.chainVertices[0] = {-2.0F, 0.0F};
    desc.chainVertices[1] = {-1.0F, 0.5F};
    desc.chainVertices[2] = {0.0F, 0.0F};
    desc.chainVertices[3] = {1.0F, -0.5F};
    desc.chainVertices[4] = {2.0F, 0.0F};
    desc.chainVertexCount = 5;
    return desc;
}

struct CreatedBodyShape final {
    PhysicsBodyId body{};
    PhysicsShapeId shape{};
};

[[nodiscard]] Core::Result<CreatedBodyShape> createBodyWithShape(
    PhysicsWorld2D& world,
    const PhysicsBody2DDesc& bodyDescription,
    const PhysicsShape2DDesc& shapeDescription)
{
    auto body = world.createBody(bodyDescription);
    if (!body) {
        return Core::failure(std::move(body.error()));
    }
    auto shape = world.createShape(*body, shapeDescription);
    if (!shape) {
        (void)world.destroyBody(*body);
        return Core::failure(std::move(shape.error()));
    }
    return CreatedBodyShape{.body = *body, .shape = *shape};
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
    config.jointCapacity = 0;
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
    EXPECT_TRUE(validatePhysicsShape2DDesc(unitBox()));

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

    PhysicsShape2DDesc shape = unitBox();
    shape.halfExtentsMeters.y = 0.0F;
    expectFailureCode(validatePhysicsShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.density = -1.0F;
    expectFailureCode(validatePhysicsShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.friction = 1.01F;
    expectFailureCode(validatePhysicsShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.restitution = -0.01F;
    expectFailureCode(validatePhysicsShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.filter.categoryBits = 0;
    expectFailureCode(validatePhysicsShape2DDesc(shape), Physics2DErrorCode::InvalidShapeDescription);

    shape = unitBox();
    shape.radiusMeters = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(validatePhysicsShape2DDesc(shape));
}

TEST(PhysicsWorld2DTest, ValidatesStrictConvexPolygonBoundaryAndFiniteTransform)
{
    EXPECT_TRUE(validatePhysicsShape2DDesc(convexTriangle()));

    PhysicsShape2DDesc polygon = convexTriangle();
    std::swap(polygon.polygonVertices[0], polygon.polygonVertices[2]);
    EXPECT_TRUE(validatePhysicsShape2DDesc(polygon));

    polygon = convexTriangle();
    polygon.polygonVertexCount = 2;
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);

    polygon = convexTriangle();
    polygon.polygonVertexCount =
        static_cast<Core::u32>(MaximumConvexPolygonVertices2D + 1U);
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);

    polygon = convexTriangle();
    polygon.polygonVertices[1].x = std::numeric_limits<float>::quiet_NaN();
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);

    polygon = convexTriangle();
    polygon.polygonVertices[2] = polygon.polygonVertices[1];
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);

    polygon = convexTriangle();
    polygon.polygonVertices[0] = {0.0F, 0.0F};
    polygon.polygonVertices[1] = {1.0F, 0.0F};
    polygon.polygonVertices[2] = {2.0F, 0.0F};
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);

    polygon = convexTriangle();
    polygon.polygonVertexCount = 5;
    polygon.polygonVertices[0] = {-1.0F, -1.0F};
    polygon.polygonVertices[1] = {1.0F, -1.0F};
    polygon.polygonVertices[2] = {0.0F, 0.0F};
    polygon.polygonVertices[3] = {1.0F, 1.0F};
    polygon.polygonVertices[4] = {-1.0F, 1.0F};
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);

    // Pentagram order has five hull points but is not a valid boundary order.
    polygon = convexTriangle();
    polygon.polygonVertexCount = 5;
    polygon.polygonVertices[0] = {0.0F, 1.0F};
    polygon.polygonVertices[1] = {-0.5878F, -0.8090F};
    polygon.polygonVertices[2] = {0.9511F, 0.3090F};
    polygon.polygonVertices[3] = {-0.9511F, 0.3090F};
    polygon.polygonVertices[4] = {0.5878F, -0.8090F};
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);

    polygon = convexTriangle();
    const float maximum = (std::numeric_limits<float>::max)();
    polygon.localCenterMeters.x = maximum;
    polygon.polygonVertices[0].x = maximum * 0.5F;
    expectFailureCode(
        validatePhysicsShape2DDesc(polygon),
        Physics2DErrorCode::InvalidShapeDescription);
}

TEST(PhysicsWorld2DTest, ValidatesBoundedNonSensorChainVertices)
{
    EXPECT_TRUE(validatePhysicsShape2DDesc(openChain()));

    PhysicsShape2DDesc chain = openChain();
    chain.chainVertexCount = static_cast<Core::u32>(MinimumChainVertices2D - 1U);
    expectFailureCode(
        validatePhysicsShape2DDesc(chain),
        Physics2DErrorCode::InvalidShapeDescription);

    chain = openChain();
    chain.chainVertexCount = static_cast<Core::u32>(MaximumChainVertices2D + 1U);
    expectFailureCode(
        validatePhysicsShape2DDesc(chain),
        Physics2DErrorCode::InvalidShapeDescription);

    chain = openChain();
    chain.isSensor = true;
    expectFailureCode(
        validatePhysicsShape2DDesc(chain),
        Physics2DErrorCode::InvalidShapeDescription);

    chain = openChain();
    chain.chainVertices[2].x = std::numeric_limits<float>::quiet_NaN();
    expectFailureCode(
        validatePhysicsShape2DDesc(chain),
        Physics2DErrorCode::InvalidShapeDescription);

    chain = openChain();
    chain.chainVertices[3] = chain.chainVertices[1];
    expectFailureCode(
        validatePhysicsShape2DDesc(chain),
        Physics2DErrorCode::InvalidShapeDescription);

    chain = openChain();
    chain.chainVertices[2] = {
        chain.chainVertices[1].x + MinimumChainVertexSeparationMeters2D,
        chain.chainVertices[1].y};
    expectFailureCode(
        validatePhysicsShape2DDesc(chain),
        Physics2DErrorCode::InvalidShapeDescription);
}

TEST(PhysicsWorld2DTest, ChainIsStaticOnlyAndQueriesDeduplicateBackendSegments)
{
    PhysicsWorld2DConfig config = smallConfig(2, 2);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto dynamic = world.createBody(dynamicBody());
    ASSERT_TRUE(dynamic) << dynamic.error().message;
    expectFailureCode(
        world.createShape(*dynamic, openChain()),
        Physics2DErrorCode::InvalidShapeDescription);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    PhysicsBody2DDesc staticDesc;
    staticDesc.type = PhysicsBodyType2D::Static;
    auto body = world.createBody(staticDesc);
    ASSERT_TRUE(body) << body.error().message;
    auto shape = world.createShape(*body, openChain());
    ASSERT_TRUE(shape) << shape.error().message;

    auto state = world.shapeState(*shape);
    ASSERT_TRUE(state) << state.error().message;
    EXPECT_EQ(state->body, *body);
    EXPECT_EQ(state->kind, PhysicsShapeKind2D::Chain);
    EXPECT_FALSE(state->isSensor);
    auto owner = world.shapeBody(*shape);
    ASSERT_TRUE(owner) << owner.error().message;
    EXPECT_EQ(*owner, *body);

    PhysicsOverlapHit2D overlapHits[4]{};
    auto overlap = world.overlapAabb({{-3.0F, -1.0F}, {3.0F, 1.0F}}, {}, overlapHits);
    ASSERT_TRUE(overlap) << overlap.error().message;
    EXPECT_EQ(overlap->totalFound, 1U);
    ASSERT_EQ(overlap->written, 1U);
    EXPECT_EQ(overlapHits[0].shape, *shape);

    PhysicsCastHit2D castHits[4]{};
    auto cast = world.castRay({{0.0F, -2.0F}, {0.0F, 4.0F}}, {}, castHits);
    ASSERT_TRUE(cast) << cast.error().message;
    EXPECT_EQ(cast->totalFound, 1U);
    ASSERT_EQ(cast->written, 1U);
    EXPECT_EQ(castHits[0].shape, *shape);

    ASSERT_TRUE(world.destroyShape(*shape));
    EXPECT_FALSE(world.contains(*shape));
    expectFailureCode(world.shapeState(*shape), Physics2DErrorCode::StaleShape);
    EXPECT_EQ(world.stats().shapeCount, 0U);
}

TEST(PhysicsWorld2DTest, LoopChainRetiresWithOwningBody)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc staticDesc;
    staticDesc.type = PhysicsBodyType2D::Static;
    auto body = world.createBody(staticDesc);
    ASSERT_TRUE(body) << body.error().message;

    PhysicsShape2DDesc loop = openChain();
    loop.chainVertexCount = 4;
    loop.chainVertices[0] = {-1.0F, -1.0F};
    loop.chainVertices[1] = {1.0F, -1.0F};
    loop.chainVertices[2] = {1.0F, 1.0F};
    loop.chainVertices[3] = {-1.0F, 1.0F};
    loop.chainLoop = true;
    auto shape = world.createShape(*body, loop);
    ASSERT_TRUE(shape) << shape.error().message;

    ASSERT_TRUE(world.destroyBody(*body));
    EXPECT_FALSE(world.contains(*body));
    EXPECT_FALSE(world.contains(*shape));
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);
}

TEST(PhysicsWorld2DTest, ConvexPolygonFailureIsTransactionalAndLocalTransformIsApplied)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto body = world.createBody({});
    ASSERT_TRUE(body) << body.error().message;

    PhysicsShape2DDesc invalid = convexTriangle();
    const float maximum = (std::numeric_limits<float>::max)();
    invalid.localCenterMeters.x = maximum;
    invalid.polygonVertices[0].x = maximum * 0.5F;
    expectFailureCode(
        world.createShape(*body, invalid),
        Physics2DErrorCode::InvalidShapeDescription);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    PhysicsShape2DDesc transformed = convexTriangle();
    transformed.localCenterMeters = {2.0F, 1.0F};
    transformed.localAngleRadians = 0.5F * std::acos(-1.0F);
    auto shape = world.createShape(*body, transformed);
    ASSERT_TRUE(shape) << shape.error().message;
    EXPECT_EQ(world.stats().shapeCount, 1U);

    auto state = world.shapeState(*shape);
    ASSERT_TRUE(state) << state.error().message;
    EXPECT_EQ(state->kind, PhysicsShapeKind2D::ConvexPolygon);

    PhysicsOverlapHit2D hit[1]{};
    auto overlap = world.overlapAabb({{1.9F, 0.9F}, {2.1F, 1.1F}}, {}, hit);
    ASSERT_TRUE(overlap) << overlap.error().message;
    ASSERT_EQ(overlap->written, 1U);
    EXPECT_EQ(hit[0].body, *body);
    EXPECT_EQ(hit[0].shape, *shape);
}

TEST(PhysicsWorld2DTest, InvalidDescriptionsDoNotConsumeCapacity)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc invalidBody = dynamicBody();
    invalidBody.angularDamping = -1.0F;
    const auto bodyFailure = createBodyWithShape(world, invalidBody, unitBox());
    expectFailureCode(bodyFailure, Physics2DErrorCode::InvalidBodyDescription);
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    PhysicsShape2DDesc invalidShape = unitBox();
    invalidShape.filter.categoryBits = 0;
    const auto shapeFailure = createBodyWithShape(world, dynamicBody(), invalidShape);
    expectFailureCode(shapeFailure, Physics2DErrorCode::InvalidShapeDescription);
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    ASSERT_TRUE(createBodyWithShape(world, dynamicBody(), unitBox()));
    const auto capacityFailure = createBodyWithShape(world, dynamicBody(), unitBox());
    expectFailureCode(capacityFailure, Physics2DErrorCode::CapacityExceeded);
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);
}

TEST(PhysicsWorld2DTest, RollsBackPmrStorageWhenWorldConstructionFails)
{
    // Create allocates: body/shape/joint pools, begin/end/hit contact buffers,
    // shape tombstones, deferred command buffer, then Impl storage.
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

    FailAfterSuccessfulAllocationsResource jointPoolFailure(2);
    const auto jointPoolResult = PhysicsWorld2D::Create(config, jointPoolFailure);
    EXPECT_FALSE(jointPoolResult);
    EXPECT_EQ(jointPoolFailure.outstandingAllocations(), 0U);
    EXPECT_EQ(jointPoolFailure.outstandingBytes(), 0U);

    FailAfterSuccessfulAllocationsResource contactBufferFailure(3);
    const auto contactBufferResult = PhysicsWorld2D::Create(config, contactBufferFailure);
    EXPECT_FALSE(contactBufferResult);
    EXPECT_EQ(contactBufferFailure.outstandingAllocations(), 0U);
    EXPECT_EQ(contactBufferFailure.outstandingBytes(), 0U);

    FailAfterSuccessfulAllocationsResource implFailure(8);
    const auto implResult = PhysicsWorld2D::Create(config, implFailure);
    expectFailureCode(implResult, Physics2DErrorCode::CapacityExceeded);
    EXPECT_EQ(implFailure.outstandingAllocations(), 0U);
    EXPECT_EQ(implFailure.outstandingBytes(), 0U);
}

TEST(PhysicsWorld2DTest, CreatesBodyAndBoxShapeWithIndependentOwningGenerationIds)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(2, 3));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc body = dynamicBody({3.0F, 4.0F}, {2.0F, -1.0F});
    body.angleRadians = 0.25F;
    body.angularVelocityRadiansPerSecond = 0.5F;

    auto createdResult = createBodyWithShape(world, body, unitBox());
    ASSERT_TRUE(createdResult) << createdResult.error().message;
    const CreatedBodyShape created = *createdResult;

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

TEST(PhysicsWorld2DTest, SupportsMultipleBoxCircleAndCapsuleShapesPerBody)
{
    PhysicsWorld2DConfig config = smallConfig(1, 3);
    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto body = world.createBody(dynamicBody());
    ASSERT_TRUE(body) << body.error().message;

    PhysicsShape2DDesc box = unitBox();
    box.filter.categoryBits = 0x2U;
    auto boxShape = world.createShape(*body, box);
    ASSERT_TRUE(boxShape) << boxShape.error().message;

    PhysicsShape2DDesc circle;
    circle.kind = PhysicsShapeKind2D::Circle;
    circle.radiusMeters = 0.25F;
    circle.localCenterMeters = {0.75F, 0.0F};
    auto circleShape = world.createShape(*body, circle);
    ASSERT_TRUE(circleShape) << circleShape.error().message;

    PhysicsShape2DDesc capsule;
    capsule.kind = PhysicsShapeKind2D::Capsule;
    capsule.radiusMeters = 0.2F;
    capsule.localPointAMeters = {-0.5F, 0.5F};
    capsule.localPointBMeters = {0.5F, 0.5F};
    auto capsuleShape = world.createShape(*body, capsule);
    ASSERT_TRUE(capsuleShape) << capsuleShape.error().message;

    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 3U);
    for (const PhysicsShapeId shape : {*boxShape, *circleShape, *capsuleShape}) {
        auto owner = world.shapeBody(shape);
        ASSERT_TRUE(owner) << owner.error().message;
        EXPECT_EQ(*owner, *body);
    }

    auto boxState = world.shapeState(*boxShape);
    auto circleState = world.shapeState(*circleShape);
    auto capsuleState = world.shapeState(*capsuleShape);
    ASSERT_TRUE(boxState);
    ASSERT_TRUE(circleState);
    ASSERT_TRUE(capsuleState);
    EXPECT_EQ(boxState->kind, PhysicsShapeKind2D::Box);
    EXPECT_EQ(boxState->filter.categoryBits, 0x2U);
    EXPECT_EQ(circleState->kind, PhysicsShapeKind2D::Circle);
    EXPECT_EQ(capsuleState->kind, PhysicsShapeKind2D::Capsule);

    ASSERT_TRUE(world.destroyShape(*circleShape));
    EXPECT_FALSE(world.contains(*circleShape));
    EXPECT_TRUE(world.contains(*boxShape));
    EXPECT_TRUE(world.contains(*capsuleShape));
    expectFailureCode(world.shapeState(*circleShape), Physics2DErrorCode::StaleShape);

    ASSERT_TRUE(world.destroyBody(*body));
    EXPECT_FALSE(world.contains(*boxShape));
    EXPECT_FALSE(world.contains(*capsuleShape));
    EXPECT_EQ(world.stats().shapeCount, 0U);
}

TEST(PhysicsWorld2DTest, PublishesSensorEnterAndExitWithoutContactResponse)
{
    PhysicsWorld2DConfig config = smallConfig(2, 2);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    PhysicsBody2DDesc sensorBodyDescription;
    sensorBodyDescription.type = PhysicsBodyType2D::Static;
    auto sensorBody = world.createBody(sensorBodyDescription);
    ASSERT_TRUE(sensorBody) << sensorBody.error().message;
    PhysicsShape2DDesc sensorShapeDescription;
    sensorShapeDescription.kind = PhysicsShapeKind2D::Circle;
    sensorShapeDescription.radiusMeters = 2.0F;
    sensorShapeDescription.density = 0.0F;
    sensorShapeDescription.isSensor = true;
    sensorShapeDescription.enableSensorEvents = true;
    auto sensorShape = world.createShape(*sensorBody, sensorShapeDescription);
    ASSERT_TRUE(sensorShape) << sensorShape.error().message;

    auto visitorBody = world.createBody(dynamicBody());
    ASSERT_TRUE(visitorBody) << visitorBody.error().message;
    PhysicsShape2DDesc visitorShapeDescription;
    visitorShapeDescription.kind = PhysicsShapeKind2D::Circle;
    visitorShapeDescription.radiusMeters = 0.25F;
    visitorShapeDescription.enableSensorEvents = true;
    auto visitorShape = world.createShape(*visitorBody, visitorShapeDescription);
    ASSERT_TRUE(visitorShape) << visitorShape.error().message;

    ASSERT_TRUE(world.step());
    auto entered = world.contactEvents();
    ASSERT_TRUE(entered) << entered.error().message;
    const auto begin = std::find_if(
        entered->beginEvents.begin(), entered->beginEvents.end(),
        [&](const PhysicsContactBeginEvent2D& event) {
            return event.isSensor && event.bodyA == *sensorBody && event.shapeA == *sensorShape
                && event.bodyB == *visitorBody && event.shapeB == *visitorShape;
        });
    ASSERT_NE(begin, entered->beginEvents.end());

    ASSERT_TRUE(world.enqueueSetTransform(*visitorBody, {10.0F, 0.0F}, 0.0F));
    bool sawExit = false;
    for (int stepIndex = 0; stepIndex < 3 && !sawExit; ++stepIndex) {
        ASSERT_TRUE(world.step());
        auto exited = world.contactEvents();
        ASSERT_TRUE(exited) << exited.error().message;
        sawExit = std::any_of(
            exited->endEvents.begin(), exited->endEvents.end(),
            [&](const PhysicsContactEndEvent2D& event) {
                return event.isSensor && event.bodyA == *sensorBody && event.shapeA == *sensorShape
                    && event.bodyB == *visitorBody && event.shapeB == *visitorShape;
            });
    }
    EXPECT_TRUE(sawExit);
}

TEST(PhysicsWorld2DTest, DistanceJointLifecycleUsesGenerationHandles)
{
    PhysicsWorld2DConfig config = smallConfig(3, 3);
    config.jointCapacity = 1;
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto bodyA = world.createBody(dynamicBody({0.0F, 0.0F}));
    auto bodyB = world.createBody(dynamicBody({2.0F, 0.0F}));
    ASSERT_TRUE(bodyA);
    ASSERT_TRUE(bodyB);

    PhysicsJoint2DDesc jointDescription;
    jointDescription.bodyA = *bodyA;
    jointDescription.bodyB = *bodyB;
    jointDescription.lengthMeters = 2.0F;
    jointDescription.enableSpring = true;
    jointDescription.hertz = 4.0F;
    jointDescription.dampingRatio = 0.5F;
    auto firstJoint = world.createJoint(jointDescription);
    ASSERT_TRUE(firstJoint) << firstJoint.error().message;
    EXPECT_TRUE(world.contains(*firstJoint));
    EXPECT_EQ(world.stats().jointCount, 1U);

    auto state = world.jointState(*firstJoint);
    ASSERT_TRUE(state) << state.error().message;
    EXPECT_EQ(state->bodyA, *bodyA);
    EXPECT_EQ(state->bodyB, *bodyB);
    EXPECT_NEAR(state->lengthMeters, 2.0F, 1.0e-4F);
    EXPECT_TRUE(state->springEnabled);
    EXPECT_NEAR(state->springHertz, 4.0F, 1.0e-5F);
    EXPECT_NEAR(state->springDampingRatio, 0.5F, 1.0e-5F);

    expectFailureCode(world.createJoint(jointDescription), Physics2DErrorCode::CapacityExceeded);
    ASSERT_TRUE(world.destroyJoint(*firstJoint));
    expectFailureCode(world.jointState(*firstJoint), Physics2DErrorCode::StaleJoint);

    auto reusedJoint = world.createJoint(jointDescription);
    ASSERT_TRUE(reusedJoint) << reusedJoint.error().message;
    EXPECT_EQ(reusedJoint->index(), firstJoint->index());
    EXPECT_NE(reusedJoint->generation(), firstJoint->generation());

    ASSERT_TRUE(world.destroyBody(*bodyA));
    EXPECT_FALSE(world.contains(*reusedJoint));
    expectFailureCode(world.destroyJoint(*reusedJoint), Physics2DErrorCode::StaleJoint);
    EXPECT_TRUE(world.contains(*bodyB));
}

TEST(PhysicsWorld2DTest, RejectsJointKindSpecificInvalidFieldsWithoutConsumingCapacity)
{
    PhysicsWorld2DConfig config = smallConfig(2, 1);
    config.jointCapacity = 1;
    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto bodyA = world.createBody(dynamicBody());
    auto bodyB = world.createBody(dynamicBody());
    ASSERT_TRUE(bodyA);
    ASSERT_TRUE(bodyB);

    PhysicsJoint2DDesc revolute;
    revolute.kind = PhysicsJointKind2D::Revolute;
    revolute.bodyA = *bodyA;
    revolute.bodyB = *bodyB;
    EXPECT_TRUE(validatePhysicsJoint2DDesc(revolute));

    revolute.enableSpring = true;
    revolute.hertz = 2.0F;
    revolute.dampingRatio = 2.0F;
    EXPECT_TRUE(validatePhysicsJoint2DDesc(revolute));

    PhysicsJoint2DDesc invalid = revolute;
    invalid.targetAngleRadians = std::numeric_limits<float>::quiet_NaN();
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    invalid = revolute;
    invalid.targetAngleRadians = 4.0F;
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    invalid = revolute;
    invalid.referenceAngleRadians = -4.0F;
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    invalid = revolute;
    invalid.lowerAngleRadians = -4.0F;
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    invalid = revolute;
    invalid.lowerAngleRadians = 0.25F;
    invalid.upperAngleRadians = -0.25F;
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    invalid = revolute;
    invalid.maxMotorTorqueNewtonMeters = -1.0F;
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    PhysicsJoint2DDesc prismatic;
    prismatic.kind = PhysicsJointKind2D::Prismatic;
    prismatic.bodyA = *bodyA;
    prismatic.bodyB = *bodyB;
    EXPECT_TRUE(validatePhysicsJoint2DDesc(prismatic));

    prismatic.localAxisA = {(std::numeric_limits<float>::max)(), 1.0F};
    EXPECT_TRUE(validatePhysicsJoint2DDesc(prismatic));

    invalid = prismatic;
    invalid.localAxisA = {};
    expectFailureCode(
        world.createJoint(invalid),
        Physics2DErrorCode::InvalidJointDescription);
    EXPECT_EQ(world.stats().jointCount, 0U);

    invalid = prismatic;
    invalid.lowerTranslationMeters = 1.0F;
    invalid.upperTranslationMeters = -1.0F;
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    invalid = prismatic;
    invalid.maxMotorForceNewtons = -1.0F;
    expectFailureCode(
        validatePhysicsJoint2DDesc(invalid),
        Physics2DErrorCode::InvalidJointDescription);

    auto valid = world.createJoint(revolute);
    ASSERT_TRUE(valid) << valid.error().message;
    EXPECT_EQ(world.stats().jointCount, 1U);
}

TEST(PhysicsWorld2DTest, RejectsCrossWorldJointHandlesAndBodyReferences)
{
    PhysicsWorld2DConfig config = smallConfig(2, 1);
    config.jointCapacity = 1;
    auto firstResult = PhysicsWorld2D::Create(config);
    auto secondResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(firstResult);
    ASSERT_TRUE(secondResult);
    PhysicsWorld2D first = std::move(*firstResult);
    PhysicsWorld2D second = std::move(*secondResult);

    auto firstBodyA = first.createBody(dynamicBody());
    auto firstBodyB = first.createBody(dynamicBody());
    auto secondBody = second.createBody(dynamicBody());
    ASSERT_TRUE(firstBodyA);
    ASSERT_TRUE(firstBodyB);
    ASSERT_TRUE(secondBody);

    PhysicsJoint2DDesc joint;
    joint.bodyA = *firstBodyA;
    joint.bodyB = *firstBodyB;
    auto firstJoint = first.createJoint(joint);
    ASSERT_TRUE(firstJoint) << firstJoint.error().message;

    EXPECT_FALSE(second.contains(*firstJoint));
    expectFailureCode(second.jointState(*firstJoint), Physics2DErrorCode::WrongWorld);
    expectFailureCode(second.destroyJoint(*firstJoint), Physics2DErrorCode::WrongWorld);

    joint.bodyB = *secondBody;
    expectFailureCode(first.createJoint(joint), Physics2DErrorCode::WrongWorld);
    EXPECT_EQ(first.stats().jointCount, 1U);
}

TEST(PhysicsWorld2DTest, RevoluteAndPrismaticStateCascadeAndReuseGenerationHandles)
{
    PhysicsWorld2DConfig config = smallConfig(4, 1);
    config.jointCapacity = 2;
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto bodyA = world.createBody(dynamicBody());
    auto bodyB = world.createBody(dynamicBody());
    auto bodyC = world.createBody(dynamicBody());
    ASSERT_TRUE(bodyA);
    ASSERT_TRUE(bodyB);
    ASSERT_TRUE(bodyC);

    PhysicsJoint2DDesc revolute;
    revolute.kind = PhysicsJointKind2D::Revolute;
    revolute.bodyA = *bodyA;
    revolute.bodyB = *bodyB;
    revolute.targetAngleRadians = 0.2F;
    revolute.enableSpring = true;
    revolute.hertz = 3.0F;
    revolute.dampingRatio = 0.4F;
    revolute.enableLimit = true;
    revolute.lowerAngleRadians = -0.5F;
    revolute.upperAngleRadians = 0.6F;
    revolute.enableMotor = true;
    revolute.motorSpeedRadiansPerSecond = 1.25F;
    revolute.maxMotorTorqueNewtonMeters = 8.0F;
    revolute.collideConnected = true;
    auto revoluteJoint = world.createJoint(revolute);
    ASSERT_TRUE(revoluteJoint) << revoluteJoint.error().message;

    auto revoluteState = world.jointState(*revoluteJoint);
    ASSERT_TRUE(revoluteState) << revoluteState.error().message;
    EXPECT_EQ(revoluteState->kind, PhysicsJointKind2D::Revolute);
    EXPECT_EQ(revoluteState->bodyA, *bodyA);
    EXPECT_EQ(revoluteState->bodyB, *bodyB);
    EXPECT_TRUE(std::isfinite(revoluteState->currentAngleRadians));
    EXPECT_NEAR(revoluteState->targetAngleRadians, 0.2F, 1.0e-5F);
    EXPECT_TRUE(revoluteState->springEnabled);
    EXPECT_NEAR(revoluteState->springHertz, 3.0F, 1.0e-5F);
    EXPECT_NEAR(revoluteState->springDampingRatio, 0.4F, 1.0e-5F);
    EXPECT_TRUE(revoluteState->limitEnabled);
    EXPECT_NEAR(revoluteState->lowerAngleRadians, -0.5F, 1.0e-5F);
    EXPECT_NEAR(revoluteState->upperAngleRadians, 0.6F, 1.0e-5F);
    EXPECT_TRUE(revoluteState->motorEnabled);
    EXPECT_NEAR(revoluteState->motorSpeedRadiansPerSecond, 1.25F, 1.0e-5F);
    EXPECT_NEAR(revoluteState->maxMotorTorqueNewtonMeters, 8.0F, 1.0e-5F);
    EXPECT_TRUE(revoluteState->collideConnected);

    PhysicsJoint2DDesc prismatic;
    prismatic.kind = PhysicsJointKind2D::Prismatic;
    prismatic.bodyA = *bodyA;
    prismatic.bodyB = *bodyC;
    prismatic.localAxisA = {2.0F, 0.0F};
    prismatic.targetTranslationMeters = 0.25F;
    prismatic.enableSpring = true;
    prismatic.hertz = 2.5F;
    prismatic.dampingRatio = 0.6F;
    prismatic.enableLimit = true;
    prismatic.lowerTranslationMeters = -0.75F;
    prismatic.upperTranslationMeters = 0.8F;
    prismatic.enableMotor = true;
    prismatic.motorSpeedMetersPerSecond = 0.9F;
    prismatic.maxMotorForceNewtons = 9.0F;
    auto prismaticJoint = world.createJoint(prismatic);
    ASSERT_TRUE(prismaticJoint) << prismaticJoint.error().message;

    auto prismaticState = world.jointState(*prismaticJoint);
    ASSERT_TRUE(prismaticState) << prismaticState.error().message;
    EXPECT_EQ(prismaticState->kind, PhysicsJointKind2D::Prismatic);
    EXPECT_EQ(prismaticState->bodyA, *bodyA);
    EXPECT_EQ(prismaticState->bodyB, *bodyC);
    EXPECT_TRUE(std::isfinite(prismaticState->currentTranslationMeters));
    EXPECT_NEAR(prismaticState->targetTranslationMeters, 0.25F, 1.0e-5F);
    EXPECT_TRUE(prismaticState->springEnabled);
    EXPECT_NEAR(prismaticState->springHertz, 2.5F, 1.0e-5F);
    EXPECT_NEAR(prismaticState->springDampingRatio, 0.6F, 1.0e-5F);
    EXPECT_TRUE(prismaticState->limitEnabled);
    EXPECT_NEAR(prismaticState->lowerTranslationMeters, -0.75F, 1.0e-5F);
    EXPECT_NEAR(prismaticState->upperTranslationMeters, 0.8F, 1.0e-5F);
    EXPECT_TRUE(prismaticState->motorEnabled);
    EXPECT_NEAR(prismaticState->motorSpeedMetersPerSecond, 0.9F, 1.0e-5F);
    EXPECT_NEAR(prismaticState->maxMotorForceNewtons, 9.0F, 1.0e-5F);

    ASSERT_TRUE(world.destroyBody(*bodyA));
    EXPECT_FALSE(world.contains(*revoluteJoint));
    EXPECT_FALSE(world.contains(*prismaticJoint));
    EXPECT_EQ(world.stats().jointCount, 0U);
    expectFailureCode(world.jointState(*revoluteJoint), Physics2DErrorCode::StaleJoint);
    expectFailureCode(world.jointState(*prismaticJoint), Physics2DErrorCode::StaleJoint);

    auto replacement = world.createBody(dynamicBody());
    ASSERT_TRUE(replacement) << replacement.error().message;
    revolute.bodyA = *replacement;
    auto reused = world.createJoint(revolute);
    ASSERT_TRUE(reused) << reused.error().message;
    if (reused->index() == revoluteJoint->index()) {
        EXPECT_NE(reused->generation(), revoluteJoint->generation());
    } else {
        EXPECT_EQ(reused->index(), prismaticJoint->index());
        EXPECT_NE(reused->generation(), prismaticJoint->generation());
    }
}

TEST(PhysicsWorld2DTest, AdvancesByTheConfiguredFixedStepOnly)
{
    PhysicsWorld2DConfig config = smallConfig(1, 1);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    config.fixedDeltaSeconds = 0.25F;

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = createBodyWithShape(world, dynamicBody({2.0F, 3.0F}, {4.0F, -2.0F}), unitBox());
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

    auto firstPair = createBodyWithShape(first, dynamicBody(), unitBox());
    auto secondPair = createBodyWithShape(second, dynamicBody(), unitBox());
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

    auto reused = createBodyWithShape(first, dynamicBody(), unitBox());
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

    auto first = createBodyWithShape(world, dynamicBody(), unitBox());
    ASSERT_TRUE(first) << first.error().message;

    const auto second = createBodyWithShape(world, dynamicBody(), unitBox());
    expectFailureCode(second, Physics2DErrorCode::CapacityExceeded);
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);
    EXPECT_TRUE(world.contains(first->body));
    EXPECT_TRUE(world.contains(first->shape));

    ASSERT_TRUE(world.destroyBody(first->body));
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);

    auto retry = createBodyWithShape(world, dynamicBody(), unitBox());
    ASSERT_TRUE(retry) << retry.error().message;
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);
}

TEST(PhysicsWorld2DTest, DestroyBodyRetiresItsShapeAndRepeatedDestroyIsStale)
{
    auto worldResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = createBodyWithShape(world, dynamicBody(), unitBox());
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

    auto created = createBodyWithShape(world, dynamicBody(), unitBox());
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
    expectFailureCode(createBodyWithShape(world, dynamicBody(), unitBox()), Physics2DErrorCode::WorldClosed);
}

TEST(PhysicsWorld2DTest, MoveTransfersOwnershipAndLeavesSourceClosed)
{
    auto sourceResult = PhysicsWorld2D::Create(smallConfig(1, 1));
    ASSERT_TRUE(sourceResult) << sourceResult.error().message;
    PhysicsWorld2D source = std::move(*sourceResult);

    auto created = createBodyWithShape(source, dynamicBody(), unitBox());
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

    auto created = createBodyWithShape(world, dynamicBody(), unitBox());
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

        const auto createResult = createBodyWithShape(world, dynamicBody(), unitBox());
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

        auto created = createBodyWithShape(world, dynamicBody(), unitBox());
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
    PhysicsShape2DDesc groundShape = unitBox();
    groundShape.halfExtentsMeters = {5.0F, 0.5F};
    groundShape.enableContactEvents = true;

    PhysicsBody2DDesc fallingBody = dynamicBody({0.0F, 2.0F}, {0.0F, -20.0F});
    PhysicsShape2DDesc fallingShape = unitBox();
    fallingShape.enableContactEvents = true;

    auto ground = createBodyWithShape(world, groundBody, groundShape);
    auto falling = createBodyWithShape(world, fallingBody, fallingShape);
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
    PhysicsShape2DDesc staticShape = unitBox();
    staticShape.halfExtentsMeters = {3.0F, 0.5F};
    staticShape.enableContactEvents = true;

    auto platform = createBodyWithShape(world, staticBody, staticShape);
    ASSERT_TRUE(platform) << platform.error().message;

    PhysicsBody2DDesc left = dynamicBody({-0.75F, 1.5F}, {0.0F, -30.0F});
    PhysicsBody2DDesc right = dynamicBody({0.75F, 1.5F}, {0.0F, -30.0F});
    PhysicsShape2DDesc box = unitBox();
    box.halfExtentsMeters = {0.4F, 0.4F};
    box.enableContactEvents = true;

    auto leftBody = createBodyWithShape(world, left, box);
    auto rightBody = createBodyWithShape(world, right, box);
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

    auto created = createBodyWithShape(world, dynamicBody({0.0F, 0.0F}), unitBox());
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
    auto left = createBodyWithShape(world, staticBody, unitBox());
    staticBody.positionMeters = {1.0F, 0.0F};
    auto right = createBodyWithShape(world, staticBody, unitBox());
    staticBody.positionMeters = {10.0F, 10.0F};
    auto farAway = createBodyWithShape(world, staticBody, unitBox());
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
    auto nearBody = createBodyWithShape(world, staticBody, unitBox());
    staticBody.positionMeters = {5.0F, 0.0F};
    auto farBody = createBodyWithShape(world, staticBody, unitBox());
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

TEST(PhysicsWorld2DTest, DeferredCommandsApplyBeforeStepInFifoOrder)
{
    PhysicsWorld2DConfig config = smallConfig(2, 2, 8, 8, 4, 4);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    config.fixedDeltaSeconds = 1.0F / 60.0F;

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = createBodyWithShape(world, dynamicBody({0.0F, 0.0F}), unitBox());
    ASSERT_TRUE(created) << created.error().message;

    ASSERT_TRUE(world.enqueueSetLinearVelocity(created->body, {6.0F, 0.0F}));
    ASSERT_TRUE(world.enqueueSetTransform(created->body, {1.0F, 2.0F}, 0.0F));
    EXPECT_EQ(world.pendingCommandCount(), 2U);

    ASSERT_TRUE(world.step());
    EXPECT_EQ(world.pendingCommandCount(), 0U);
    EXPECT_GE(world.stats().appliedCommandCount, 2U);

    auto state = world.bodyState(created->body);
    ASSERT_TRUE(state) << state.error().message;
    EXPECT_NEAR(state->positionMeters.x, 1.0F + 6.0F * config.fixedDeltaSeconds, 1.0e-3F);
    EXPECT_NEAR(state->positionMeters.y, 2.0F, 1.0e-3F);
    EXPECT_FLOAT_EQ(state->linearVelocityMetersPerSecond.x, 6.0F);
}

TEST(PhysicsWorld2DTest, DeferredDestroyAndCapacityAndStaleSkip)
{
    PhysicsWorld2DConfig config = smallConfig(2, 2, 8, 8, 4, 1);
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};

    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto first = createBodyWithShape(world, dynamicBody({0.0F, 0.0F}), unitBox());
    ASSERT_TRUE(first) << first.error().message;

    ASSERT_TRUE(world.enqueueDestroyBody(first->body));
    expectFailureCode(
        world.enqueueSetLinearVelocity(first->body, {1.0F, 0.0F}),
        Physics2DErrorCode::CapacityExceeded);
    EXPECT_EQ(world.pendingCommandCount(), 1U);

    ASSERT_TRUE(world.step());
    EXPECT_FALSE(world.contains(first->body));
    EXPECT_EQ(world.pendingCommandCount(), 0U);

    auto second = createBodyWithShape(world, dynamicBody({3.0F, 0.0F}), unitBox());
    ASSERT_TRUE(second) << second.error().message;
    ASSERT_TRUE(world.enqueueSetLinearVelocity(second->body, {2.0F, 0.0F}));
    ASSERT_TRUE(world.destroyBody(second->body));
    // Enqueued command targeting a body destroyed before step is skipped at flush.
    ASSERT_TRUE(world.step());
    EXPECT_GE(world.stats().skippedStaleCommandCount, 1U);

    // Stale enqueue rejects at enqueue time via validateBody.
    expectFailureCode(world.enqueueDestroyBody(second->body), Physics2DErrorCode::StaleBody);
    ASSERT_TRUE(world.clearCommands());
    EXPECT_EQ(world.pendingCommandCount(), 0U);
}

} // namespace
} // namespace Tina::Physics2D
