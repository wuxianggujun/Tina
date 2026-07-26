#include <tina/render/RenderErrors.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/Trail2D.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory_resource>
#include <new>
#include <numbers>

namespace Tina::Scene {
namespace {

class CountingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCalls() const noexcept { return m_allocationCalls; }
    [[nodiscard]] usize deallocationCalls() const noexcept { return m_deallocationCalls; }
    [[nodiscard]] usize currentBytes() const noexcept { return m_currentBytes; }
    void rejectAllocationAtLeast(usize bytes) noexcept
    {
        m_rejectedAllocationMinimumBytes = bytes;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (bytes >= m_rejectedAllocationMinimumBytes) {
            throw std::bad_alloc{};
        }
        void* memory = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCalls;
        m_currentBytes += bytes;
        return memory;
    }

    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
        ++m_deallocationCalls;
        m_currentBytes -= bytes;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCalls = 0;
    usize m_deallocationCalls = 0;
    usize m_currentBytes = 0;
    usize m_rejectedAllocationMinimumBytes = (std::numeric_limits<usize>::max)();
};

[[nodiscard]] Trail2DConfig trailConfig(usize capacity = 4)
{
    return Trail2DConfig{
        .segmentCapacity = capacity,
        .segmentLifetime = Core::Duration{2.0},
        .startWidthMeters = 2.0F,
        .endWidthMeters = 0.5F,
        .spriteKey = 7,
        .stableEntityKeyBase = 100,
        .uvRect = {.u0 = 0.1F, .v0 = 0.2F, .u1 = 0.9F, .v1 = 0.8F},
        .color = {.red = 10, .green = 20, .blue = 30, .alpha = 200},
        .sortingLayer = 3,
        .orderInLayer = 4,
    };
}

[[nodiscard]] Render::RenderCamera2DInput camera(float centerX = 0.0F, float worldSize = 100.0F)
{
    return Render::RenderCamera2DInput{
        .stableCameraKey = 1,
        .centerX = centerX,
        .centerY = 0.0F,
        .worldWidth = worldSize,
        .worldHeight = worldSize,
        .actualPixelsPerMeter = 10.0F,
    };
}

void expectCreateFailure(Trail2DConfig config, Core::ErrorCode expected)
{
    auto trail = Trail2D::Create(config);
    ASSERT_FALSE(trail.has_value());
    EXPECT_EQ(trail.error().code, expected);
}

TEST(Trail2DTest, CreateRejectsInvalidConfiguration)
{
    auto config = trailConfig();
    config.segmentCapacity = 0;
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);

    config = trailConfig();
    config.segmentLifetime = Core::Duration{0.0};
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);
    config.segmentLifetime = Core::Duration{-1.0};
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);
    config.segmentLifetime = Core::Duration{std::numeric_limits<double>::infinity()};
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);

    config = trailConfig();
    config.startWidthMeters = 0.0F;
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);
    config = trailConfig();
    config.endWidthMeters = -1.0F;
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);
    config = trailConfig();
    config.startWidthMeters = std::numeric_limits<float>::quiet_NaN();
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);

    config = trailConfig();
    config.spriteKey = 0;
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);
    config = trailConfig();
    config.stableEntityKeyBase = 0;
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);
    config = trailConfig();
    config.uvRect.u1 = config.uvRect.u0;
    expectCreateFailure(config, SceneErrorCode::InvalidComponent);
}

TEST(Trail2DTest, CreateMapsPmrAllocationFailureToCapacityExceeded)
{
    CountingMemoryResource memory;
    constexpr usize capacity = 4;
    memory.rejectAllocationAtLeast(sizeof(Trail2DSegment) * capacity);
    auto allocationFailure = Trail2D::Create(trailConfig(capacity), memory);
    ASSERT_FALSE(allocationFailure.has_value());
    EXPECT_EQ(allocationFailure.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(memory.allocationCalls(), memory.deallocationCalls());
}

TEST(Trail2DTest, CreateRejectsUnrepresentableSegmentCapacity)
{
    expectCreateFailure(
        trailConfig((std::numeric_limits<usize>::max)()),
        SceneErrorCode::CapacityExceeded);
}

TEST(Trail2DTest, RejectsInvalidGeometryAndExtractsRotatedSprite)
{
    auto trail = Trail2D::Create(trailConfig());
    ASSERT_TRUE(trail.has_value());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto invalidFirst = trail->appendPoint({nan, 0.0F});
    ASSERT_FALSE(invalidFirst.has_value());
    EXPECT_EQ(invalidFirst.error().code, SceneErrorCode::InvalidComponent);
    EXPECT_FALSE(trail->hasAnchor());

    ASSERT_TRUE(trail->appendPoint({0.0F, 0.0F}).has_value());
    const auto degenerate = trail->appendPoint({0.0F, 0.0F});
    ASSERT_FALSE(degenerate.has_value());
    EXPECT_EQ(degenerate.error().code, SceneErrorCode::InvalidComponent);
    const auto nonFinite = trail->appendPoint({std::numeric_limits<float>::infinity(), 1.0F});
    ASSERT_FALSE(nonFinite.has_value());
    EXPECT_EQ(nonFinite.error().code, SceneErrorCode::InvalidComponent);

    ASSERT_TRUE(trail->appendPoint({3.0F, 4.0F}).has_value());
    ASSERT_EQ(trail->segmentCount(), 1U);
    EXPECT_EQ(trail->segments()[0].stableEntityKey, 100U);

    auto builder = Render::RenderSceneBuilder::Create(
        Render::RenderSceneCapacity{.spriteCapacity = 4});
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame().has_value());
    auto writer = builder->writer();
    ASSERT_TRUE(writer.setCamera2D(camera()).has_value());
    ASSERT_TRUE(trail->extract(writer).has_value());
    auto scene = builder->commit();
    ASSERT_TRUE(scene.has_value());
    ASSERT_EQ(scene->sprites2D().size(), 1U);
    const auto& sprite = scene->sprites2D()[0];
    EXPECT_EQ(sprite.spriteKey, 7U);
    EXPECT_EQ(sprite.stableEntityKey, 100U);
    EXPECT_FLOAT_EQ(sprite.centerX, 1.5F);
    EXPECT_FLOAT_EQ(sprite.centerY, 2.0F);
    EXPECT_FLOAT_EQ(sprite.widthMeters, 5.0F);
    EXPECT_FLOAT_EQ(sprite.heightMeters, 2.0F);
    EXPECT_NEAR(sprite.rotationRadians, std::atan2(4.0F, 3.0F), 1.0e-6F);
    EXPECT_FLOAT_EQ(sprite.u0, 0.1F);
    EXPECT_FLOAT_EQ(sprite.v1, 0.8F);
    EXPECT_EQ(sprite.red, 10U);
    EXPECT_EQ(sprite.alpha, 200U);
    EXPECT_EQ(sprite.sortingLayer, 3);
    EXPECT_EQ(sprite.orderInLayer, 4);
}

TEST(Trail2DTest, BreakAndIndependentLifetimeDriveLinearWidths)
{
    auto trail = Trail2D::Create(trailConfig());
    ASSERT_TRUE(trail.has_value());
    ASSERT_TRUE(trail->appendPoint({0.0F, 0.0F}).has_value());
    ASSERT_TRUE(trail->appendPoint({2.0F, 0.0F}).has_value());
    ASSERT_TRUE(trail->update(Core::Duration{0.5}).has_value());

    trail->breakTrail();
    ASSERT_TRUE(trail->appendPoint({10.0F, 0.0F}).has_value());
    EXPECT_EQ(trail->segmentCount(), 1U);
    ASSERT_TRUE(trail->appendPoint({10.0F, 2.0F}).has_value());
    ASSERT_TRUE(trail->update(Core::Duration{0.5}).has_value());
    ASSERT_EQ(trail->segmentCount(), 2U);
    EXPECT_DOUBLE_EQ(trail->segments()[0].age.count(), 1.0);
    EXPECT_DOUBLE_EQ(trail->segments()[1].age.count(), 0.5);

    auto builder = Render::RenderSceneBuilder::Create(
        Render::RenderSceneCapacity{.spriteCapacity = 4});
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame().has_value());
    {
        auto writer = builder->writer();
        ASSERT_TRUE(writer.setCamera2D(camera()).has_value());
        ASSERT_TRUE(trail->extract(writer).has_value());
        auto scene = builder->commit();
        ASSERT_TRUE(scene.has_value());
        ASSERT_EQ(scene->sprites2D().size(), 2U);
        EXPECT_FLOAT_EQ(scene->sprites2D()[0].heightMeters, 1.25F);
        EXPECT_FLOAT_EQ(scene->sprites2D()[1].heightMeters, 1.625F);
        EXPECT_FLOAT_EQ(scene->sprites2D()[0].rotationRadians, 0.0F);
        EXPECT_NEAR(
            scene->sprites2D()[1].rotationRadians,
            std::numbers::pi_v<float> * 0.5F,
            1.0e-6F);
    }

    ASSERT_TRUE(trail->update(Core::Duration{1.0}).has_value());
    ASSERT_EQ(trail->segmentCount(), 1U);
    EXPECT_EQ(trail->segments()[0].stableEntityKey, 101U);

    ASSERT_TRUE(builder->beginFrame().has_value());
    {
        auto writer = builder->writer();
        ASSERT_TRUE(writer.setCamera2D(camera()).has_value());
        ASSERT_TRUE(trail->extract(writer).has_value());
        auto scene = builder->commit();
        ASSERT_TRUE(scene.has_value());
        ASSERT_EQ(scene->sprites2D().size(), 1U);
        EXPECT_FLOAT_EQ(scene->sprites2D()[0].heightMeters, 0.875F);
    }

    ASSERT_TRUE(trail->update(Core::Duration{0.5}).has_value());
    EXPECT_EQ(trail->segmentCount(), 0U);
}

TEST(Trail2DTest, CapacityFailurePreservesAnchorSegmentsAndStableKeySequence)
{
    auto trail = Trail2D::Create(trailConfig(1));
    ASSERT_TRUE(trail.has_value());
    ASSERT_TRUE(trail->appendPoint({0.0F, 0.0F}).has_value());
    ASSERT_TRUE(trail->appendPoint({1.0F, 0.0F}).has_value());
    const auto full = trail->appendPoint({2.0F, 0.0F});
    ASSERT_FALSE(full.has_value());
    EXPECT_EQ(full.error().code, SceneErrorCode::CapacityExceeded);
    ASSERT_EQ(trail->segmentCount(), 1U);
    EXPECT_EQ(trail->segments()[0].stableEntityKey, 100U);

    ASSERT_TRUE(trail->update(Core::Duration{2.0}).has_value());
    EXPECT_EQ(trail->segmentCount(), 0U);
    ASSERT_TRUE(trail->appendPoint({2.0F, 0.0F}).has_value());
    ASSERT_EQ(trail->segmentCount(), 1U);
    EXPECT_EQ(trail->segments()[0].start, (Vec2{1.0F, 0.0F}));
    EXPECT_EQ(trail->segments()[0].end, (Vec2{2.0F, 0.0F}));
    EXPECT_EQ(trail->segments()[0].stableEntityKey, 101U);
}

TEST(Trail2DTest, StableKeyOverflowFailurePreservesAnchorAndSegments)
{
    auto config = trailConfig(2);
    config.stableEntityKeyBase = (std::numeric_limits<u64>::max)();
    auto trail = Trail2D::Create(config);
    ASSERT_TRUE(trail.has_value());
    ASSERT_TRUE(trail->appendPoint({0.0F, 0.0F}).has_value());
    ASSERT_TRUE(trail->appendPoint({1.0F, 0.0F}).has_value());
    ASSERT_EQ(trail->segmentCount(), 1U);
    EXPECT_EQ(trail->segments()[0].stableEntityKey, (std::numeric_limits<u64>::max)());

    const auto exhausted = trail->appendPoint({2.0F, 0.0F});
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, SceneErrorCode::CapacityExceeded);
    ASSERT_EQ(trail->segmentCount(), 1U);

    ASSERT_TRUE(trail->update(Core::Duration{2.0}).has_value());
    EXPECT_EQ(trail->segmentCount(), 0U);
    const auto retry = trail->appendPoint({2.0F, 0.0F});
    ASSERT_FALSE(retry.has_value());
    EXPECT_EQ(retry.error().code, SceneErrorCode::CapacityExceeded);
}

TEST(Trail2DTest, UpdateOverflowFailurePreservesAllSegmentState)
{
    auto config = trailConfig(2);
    config.segmentLifetime = Core::Duration{(std::numeric_limits<double>::max)()};
    auto trail = Trail2D::Create(config);
    ASSERT_TRUE(trail.has_value());
    ASSERT_TRUE(trail->appendPoint({0.0F, 0.0F}).has_value());
    ASSERT_TRUE(trail->appendPoint({1.0F, 0.0F}).has_value());
    ASSERT_TRUE(
        trail->update(Core::Duration{(std::numeric_limits<double>::max)() * 0.5}).has_value());
    ASSERT_TRUE(trail->appendPoint({2.0F, 0.0F}).has_value());
    ASSERT_EQ(trail->segmentCount(), 2U);

    const double firstAge = trail->segments()[0].age.count();
    const double secondAge = trail->segments()[1].age.count();
    const u64 firstKey = trail->segments()[0].stableEntityKey;
    const u64 secondKey = trail->segments()[1].stableEntityKey;
    const auto overflow = trail->update(
        Core::Duration{(std::numeric_limits<double>::max)()});

    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, SceneErrorCode::InvalidComponent);
    ASSERT_EQ(trail->segmentCount(), 2U);
    EXPECT_DOUBLE_EQ(trail->segments()[0].age.count(), firstAge);
    EXPECT_DOUBLE_EQ(trail->segments()[1].age.count(), secondAge);
    EXPECT_EQ(trail->segments()[0].stableEntityKey, firstKey);
    EXPECT_EQ(trail->segments()[1].stableEntityKey, secondKey);
}

TEST(Trail2DTest, UpdateAndExtractRemainAllocationFreeForThreeHundredFrames)
{
    CountingMemoryResource memory;
    {
        auto config = trailConfig(8);
        config.segmentLifetime = Core::Duration{0.1};
        auto trail = Trail2D::Create(config, memory);
        ASSERT_TRUE(trail.has_value());
        ASSERT_TRUE(trail->appendPoint({0.0F, 0.0F}).has_value());

        auto builder = Render::RenderSceneBuilder::Create(
            Render::RenderSceneCapacity{.spriteCapacity = 8});
        ASSERT_TRUE(builder.has_value());
        const usize allocationCalls = memory.allocationCalls();
        const usize currentBytes = memory.currentBytes();
        ASSERT_GT(allocationCalls, 0U);
        ASSERT_GT(currentBytes, 0U);

        for (u32 frame = 0; frame < 300U; ++frame) {
            ASSERT_TRUE(trail->update(Core::Duration{1.0 / 60.0}).has_value());
            ASSERT_TRUE(trail->appendPoint({static_cast<float>(frame + 1U), 0.0F}).has_value());
            ASSERT_LE(trail->segmentCount(), 8U);

            ASSERT_TRUE(builder->beginFrame().has_value());
            auto writer = builder->writer();
            ASSERT_TRUE(writer.setCamera2D(camera(150.0F, 400.0F)).has_value());
            ASSERT_TRUE(trail->extract(writer).has_value());
            ASSERT_TRUE(builder->commit().has_value());

            EXPECT_EQ(memory.allocationCalls(), allocationCalls);
            EXPECT_EQ(memory.currentBytes(), currentBytes);
        }
    }
    EXPECT_EQ(memory.currentBytes(), 0U);
    EXPECT_EQ(memory.allocationCalls(), memory.deallocationCalls());
}

TEST(Trail2DTest, ExtractPropagatesRenderWriterCapacityFailure)
{
    auto trail = Trail2D::Create(trailConfig(2));
    ASSERT_TRUE(trail.has_value());
    ASSERT_TRUE(trail->appendPoint({0.0F, 0.0F}).has_value());
    ASSERT_TRUE(trail->appendPoint({1.0F, 0.0F}).has_value());
    ASSERT_TRUE(trail->appendPoint({2.0F, 0.0F}).has_value());

    auto builder = Render::RenderSceneBuilder::Create(
        Render::RenderSceneCapacity{.spriteCapacity = 1});
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame().has_value());
    auto writer = builder->writer();
    ASSERT_TRUE(writer.setCamera2D(camera()).has_value());
    const auto extracted = trail->extract(writer);
    ASSERT_FALSE(extracted.has_value());
    EXPECT_EQ(extracted.error().code, Render::RenderErrorCode::RenderSceneCapacityExceeded);
    ASSERT_FALSE(builder->commit().has_value());
}

} // namespace
} // namespace Tina::Scene
