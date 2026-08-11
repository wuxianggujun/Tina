#include <tina/physics2d/PhysicsErrors.hpp>
#include <tina/physics2d/PhysicsGridBodies.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <utility>

namespace Tina::Physics2D {
namespace {

[[nodiscard]] PhysicsWorld2DConfig smallWorld(
    Core::usize bodyCapacity = 16,
    Core::usize shapeCapacity = 16) noexcept
{
    PhysicsWorld2DConfig config;
    config.bodyCapacity = bodyCapacity;
    config.shapeCapacity = shapeCapacity;
    config.contactBeginCapacity = 8;
    config.contactEndCapacity = 8;
    config.contactHitCapacity = 4;
    config.commandCapacity = 8;
    config.solverSubStepCount = 1;
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    return config;
}

[[nodiscard]] PhysicsGridColliderMaterial2D contactMaterial() noexcept
{
    PhysicsGridColliderMaterial2D material;
    material.enableContactEvents = true;
    return material;
}

// One merged rectangle becomes one box shape centered on the covered cell span.
TEST(PhysicsGridBodiesTest, CreatesOneBodyWithOneShapePerSolidRectangle)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(8, 8));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    // Mimics a merged TileMap floor run (1..3, y=0) plus a separate single cell.
    const std::array<PhysicsGridSolidRect2D, 2> rectangles{{
        {.cellX = 1, .cellY = 0, .widthCells = 3, .heightCells = 1},
        {.cellX = 6, .cellY = 2, .widthCells = 1, .heightCells = 2},
    }};

    auto created =
        createStaticBodyForSolidRectangles(world, rectangles, 1.0F, contactMaterial());
    ASSERT_TRUE(created) << created.error().message;
    EXPECT_TRUE(created->body.hasValue());
    EXPECT_EQ(created->shapeCount, 2U);
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 2U);

    // The body itself stays at the origin; each rectangle center is baked into
    // the shape's local center, so one body can cover a whole chunk.
    auto state = world.bodyState(created->body);
    ASSERT_TRUE(state) << state.error().message;
    EXPECT_NEAR(state->positionMeters.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(state->positionMeters.y, 0.0F, 1.0e-5F);

    auto destroyed = destroyBodies(world, std::array{created->body});
    ASSERT_TRUE(destroyed) << destroyed.error().message;
    EXPECT_EQ(destroyed->written, 1U);
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);
}

// A merged rectangle must collide over its full span, not just the origin cell.
TEST(PhysicsGridBodiesTest, MergedRectangleCollidesAcrossItsFullSpan)
{
    PhysicsWorld2DConfig config = smallWorld(8, 8);
    config.gravityMetersPerSecondSquared = {0.0F, -20.0F};
    auto worldResult = PhysicsWorld2D::Create(config);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    const std::array<PhysicsGridSolidRect2D, 1> floor{{
        {.cellX = 0, .cellY = 0, .widthCells = 8, .heightCells = 1},
    }};
    auto created = createStaticBodyForSolidRectangles(world, floor, 1.0F, contactMaterial());
    ASSERT_TRUE(created) << created.error().message;
    ASSERT_EQ(created->shapeCount, 1U);

    // Drop far from the rectangle origin: only a correctly spanned box catches it.
    PhysicsBody2DDesc dynamicBody;
    dynamicBody.type = PhysicsBodyType2D::Dynamic;
    dynamicBody.positionMeters = {6.5F, 4.0F};
    auto dynamic = world.createBody(dynamicBody);
    ASSERT_TRUE(dynamic) << dynamic.error().message;

    PhysicsShape2DDesc box;
    box.kind = PhysicsShapeKind2D::Box;
    box.halfExtentsMeters = {0.4F, 0.4F};
    box.density = 1.0F;
    box.enableContactEvents = true;
    auto dynamicShape = world.createShape(*dynamic, box);
    ASSERT_TRUE(dynamicShape) << dynamicShape.error().message;

    bool sawBegin = false;
    for (int step = 0; step < 240; ++step) {
        ASSERT_TRUE(world.step());
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts);
        if (!contacts->beginEvents.empty()) {
            sawBegin = true;
            break;
        }
    }
    EXPECT_TRUE(sawBegin);

    auto rested = world.bodyState(*dynamic);
    ASSERT_TRUE(rested) << rested.error().message;
    // Came to rest on top of the 1 m floor rather than falling through it.
    EXPECT_GT(rested->positionMeters.y, 1.0F);
    EXPECT_LT(rested->positionMeters.y, 4.0F);
}

// An empty rectangle span is a successful no-op with an empty body id.
TEST(PhysicsGridBodiesTest, EmptyRectangleSpanCreatesNothing)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(4, 4));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    auto created = createStaticBodyForSolidRectangles(
        world,
        std::span<const PhysicsGridSolidRect2D>{},
        1.0F,
        contactMaterial());
    ASSERT_TRUE(created) << created.error().message;
    EXPECT_FALSE(created->body.hasValue());
    EXPECT_EQ(created->shapeCount, 0U);
    EXPECT_EQ(world.stats().bodyCount, 0U);
}

// Degenerate or overflowing rectangles are rejected before any body exists.
TEST(PhysicsGridBodiesTest, RejectsInvalidRectanglesWithoutCreatingBodies)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(8, 8));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    constexpr Core::u32 MaxU32 = (std::numeric_limits<Core::u32>::max)();
    const std::array<PhysicsGridSolidRect2D, 4> invalid{{
        {.cellX = 0, .cellY = 0, .widthCells = 0, .heightCells = 1},
        {.cellX = 0, .cellY = 0, .widthCells = 1, .heightCells = 0},
        {.cellX = MaxU32, .cellY = 0, .widthCells = 2, .heightCells = 1},
        {.cellX = 0, .cellY = MaxU32, .widthCells = 1, .heightCells = 2},
    }};

    for (const PhysicsGridSolidRect2D& rectangle : invalid) {
        auto created = createStaticBodyForSolidRectangles(
            world,
            std::span{&rectangle, 1},
            1.0F,
            contactMaterial());
        ASSERT_FALSE(created);
        EXPECT_EQ(created.error().code, Physics2DErrorCode::InvalidShapeDescription);
        EXPECT_EQ(world.stats().bodyCount, 0U);
    }

    // A single invalid entry rejects the whole batch, including valid neighbours.
    const std::array<PhysicsGridSolidRect2D, 2> mixed{{
        {.cellX = 0, .cellY = 0, .widthCells = 2, .heightCells = 2},
        {.cellX = 4, .cellY = 0, .widthCells = 0, .heightCells = 1},
    }};
    auto batch = createStaticBodyForSolidRectangles(world, mixed, 1.0F, contactMaterial());
    ASSERT_FALSE(batch);
    EXPECT_EQ(batch.error().code, Physics2DErrorCode::InvalidShapeDescription);
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);
}

// Cell size and material are validated before the batch is attempted.
TEST(PhysicsGridBodiesTest, RejectsInvalidCellSizeAndMaterial)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(4, 4));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    const std::array<PhysicsGridSolidRect2D, 1> rectangles{{
        {.cellX = 0, .cellY = 0, .widthCells = 1, .heightCells = 1},
    }};

    for (const float cellSize : {0.0F, -1.0F, std::numeric_limits<float>::infinity()}) {
        auto created = createStaticBodyForSolidRectangles(
            world, rectangles, cellSize, contactMaterial());
        ASSERT_FALSE(created);
        EXPECT_EQ(created.error().code, Physics2DErrorCode::InvalidConfiguration);
    }

    // categoryBits=0 would make the collider collide with nothing at all.
    PhysicsGridColliderMaterial2D unfiltered = contactMaterial();
    unfiltered.filter.categoryBits = 0;
    auto filtered =
        createStaticBodyForSolidRectangles(world, rectangles, 1.0F, unfiltered);
    ASSERT_FALSE(filtered);
    EXPECT_EQ(filtered.error().code, Physics2DErrorCode::InvalidConfiguration);

    PhysicsGridColliderMaterial2D loudFriction = contactMaterial();
    loudFriction.friction = 2.0F;
    auto rejected =
        createStaticBodyForSolidRectangles(world, rectangles, 1.0F, loudFriction);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, Physics2DErrorCode::InvalidConfiguration);

    EXPECT_EQ(world.stats().bodyCount, 0U);
}

// A mid-batch shape failure destroys the body and every shape this call created.
TEST(PhysicsGridBodiesTest, RollsBackEntireBodyWhenShapeCapacityIsExhausted)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(4, 2));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    const std::array<PhysicsGridSolidRect2D, 3> rectangles{{
        {.cellX = 0, .cellY = 0, .widthCells = 1, .heightCells = 1},
        {.cellX = 2, .cellY = 0, .widthCells = 1, .heightCells = 1},
        {.cellX = 4, .cellY = 0, .widthCells = 1, .heightCells = 1},
    }};

    auto created =
        createStaticBodyForSolidRectangles(world, rectangles, 1.0F, contactMaterial());
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, Physics2DErrorCode::CapacityExceeded);
    // No half-published collider: the partially built body is gone too.
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);
}

TEST(PhysicsGridBodiesTest, DestroyBodiesSkipsStaleAndEmpty)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(4, 4));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    const std::array<PhysicsGridSolidRect2D, 1> rectangles{{
        {.cellX = 0, .cellY = 0, .widthCells = 1, .heightCells = 1},
    }};
    auto created =
        createStaticBodyForSolidRectangles(world, rectangles, 1.0F, contactMaterial());
    ASSERT_TRUE(created) << created.error().message;
    ASSERT_TRUE(world.destroyBody(created->body));

    const std::array<PhysicsBodyId, 2> staleBatch{created->body, PhysicsBodyId{}};
    auto result = destroyBodies(world, staleBatch);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->totalFound, 2U);
    EXPECT_EQ(result->written, 0U);
}

} // namespace
} // namespace Tina::Physics2D
