#include <gtest/gtest.h>

#include <tina/render/RenderErrors.hpp>
#include <tina/render/UIDisplayList.hpp>

#include <array>
#include <memory_resource>
#include <new>

namespace Tina::Tests {
namespace {

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    usize allocationCount = 0;
    usize deallocationCount = 0;
    bool rejectAllocations = false;

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (rejectAllocations)
        {
            throw std::bad_alloc{};
        }
        ++allocationCount;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++deallocationCount;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

[[nodiscard]] constexpr Render::UIPremultipliedRgba8 opaque(u8 red, u8 green, u8 blue) noexcept
{
    return {.red = red, .green = green, .blue = blue, .alpha = 255};
}

[[nodiscard]] Render::UIDisplayListBuilder
createBuilder(Render::UIDisplayListCapacity capacity,
              std::pmr::memory_resource& storage = *std::pmr::get_default_resource())
{
    auto result = Render::UIDisplayListBuilder::Create(capacity, storage);
    EXPECT_TRUE(result.has_value());
    return std::move(*result);
}

} // namespace

TEST(UIDisplayListTest, RejectsInvalidCapacityAndFixedStorageAllocationFailure)
{
    auto noCommands = Render::UIDisplayListBuilder::Create({.commandCount = 0, .clipCount = 1, .batchCount = 1});
    ASSERT_FALSE(noCommands.has_value());
    EXPECT_EQ(noCommands.error().code, Render::RenderErrorCode::InvalidDisplayListCapacity);

    auto noBatches = Render::UIDisplayListBuilder::Create({.commandCount = 1, .clipCount = 1, .batchCount = 0});
    ASSERT_FALSE(noBatches.has_value());
    EXPECT_EQ(noBatches.error().code, Render::RenderErrorCode::InvalidDisplayListCapacity);

    TrackingMemoryResource storage;
    storage.rejectAllocations = true;
    auto unavailable =
        Render::UIDisplayListBuilder::Create({.commandCount = 1, .clipCount = 1, .batchCount = 1}, storage);
    ASSERT_FALSE(unavailable.has_value());
    EXPECT_EQ(unavailable.error().code, Render::RenderErrorCode::DisplayListStorageAllocationFailed);
}

TEST(UIDisplayListTest, PrunesEmptyTransparentAndFullyClippedQuads)
{
    auto builder = createBuilder({.commandCount = 4, .clipCount = 2, .batchCount = 4});
    ASSERT_TRUE(builder.beginFrame().has_value());

    EXPECT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 0, 10},
                        .color = opaque(1, 2, 3),
                    })
                    .has_value());
    EXPECT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 1,
                        .bounds = {0, 0, 10, 10},
                        .color = {},
                    })
                    .has_value());
    EXPECT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 2,
                        .bounds = {0, 0, 10, 10},
                        .color = opaque(1, 2, 3),
                        .effectiveClip = Render::UIPixelRect{0, 0, 0, 10},
                    })
                    .has_value());
    EXPECT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 3,
                        .bounds = {0, 0, 10, 10},
                        .color = opaque(1, 2, 3),
                        .effectiveClip = Render::UIPixelRect{20, 20, 5, 5},
                    })
                    .has_value());

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value());
    EXPECT_TRUE(committed->empty());
    EXPECT_TRUE(committed->clips().empty());
    EXPECT_TRUE(committed->batches().empty());
    EXPECT_EQ(committed->statistics().prunedEmptyBoundsCount, 1U);
    EXPECT_EQ(committed->statistics().prunedTransparentCount, 1U);
    EXPECT_EQ(committed->statistics().prunedEmptyClipCount, 1U);
    EXPECT_EQ(committed->statistics().prunedOutsideClipCount, 1U);
}

TEST(UIDisplayListTest, InternsClipsByFirstSeenValueAndBatchesOnlyAdjacentCompatibleCommands)
{
    auto builder = createBuilder({.commandCount = 8, .clipCount = 4, .batchCount = 8});
    ASSERT_TRUE(builder.beginFrame().has_value());

    constexpr Render::UIPixelRect firstClip{0, 0, 100, 100};
    constexpr Render::UIPixelRect secondClip{10, 10, 80, 80};
    const std::array<Render::UISolidQuadInput, 5> inputs{{
        {.paintOrdinal = 2, .bounds = {0, 0, 10, 10}, .color = opaque(10, 20, 30), .effectiveClip = firstClip},
        {.paintOrdinal = 4, .bounds = {10, 0, 10, 10}, .color = opaque(20, 30, 40), .effectiveClip = firstClip},
        {.paintOrdinal = 7, .bounds = {20, 0, 10, 10}, .color = opaque(30, 40, 50)},
        {.paintOrdinal = 8, .bounds = {30, 10, 10, 10}, .color = opaque(40, 50, 60), .effectiveClip = secondClip},
        {.paintOrdinal = 11, .bounds = {40, 0, 10, 10}, .color = opaque(50, 60, 70), .effectiveClip = firstClip},
    }};
    for (const auto& input : inputs)
    {
        ASSERT_TRUE(builder.addSolidQuad(input).has_value());
    }

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value());
    ASSERT_EQ(committed->commands().size(), 5U);
    ASSERT_EQ(committed->clips().size(), 2U);
    EXPECT_EQ(committed->clips()[0], firstClip);
    EXPECT_EQ(committed->clips()[1], secondClip);

    const auto firstClipId = committed->commands()[0].clip;
    const auto secondClipId = committed->commands()[3].clip;
    EXPECT_TRUE(firstClipId.hasClip());
    EXPECT_TRUE(secondClipId.hasClip());
    EXPECT_NE(firstClipId, secondClipId);
    EXPECT_EQ(committed->commands()[1].clip, firstClipId);
    EXPECT_FALSE(committed->commands()[2].clip.hasClip());
    EXPECT_EQ(committed->commands()[4].clip, firstClipId);
    EXPECT_EQ(committed->commands()[0].paintOrdinal, 2U);
    EXPECT_EQ(committed->commands()[1].paintOrdinal, 4U);
    EXPECT_EQ(committed->commands()[2].paintOrdinal, 7U);
    EXPECT_EQ(committed->commands()[3].paintOrdinal, 8U);
    EXPECT_EQ(committed->commands()[4].paintOrdinal, 11U);
    ASSERT_NE(committed->resolveClip(firstClipId), nullptr);
    ASSERT_NE(committed->resolveClip(secondClipId), nullptr);
    EXPECT_EQ(*committed->resolveClip(firstClipId), firstClip);
    EXPECT_EQ(*committed->resolveClip(secondClipId), secondClip);
    EXPECT_EQ(committed->resolveClip({}), nullptr);

    ASSERT_EQ(committed->batches().size(), 4U);
    EXPECT_EQ(committed->batches()[0].firstCommand, 0U);
    EXPECT_EQ(committed->batches()[0].commandCount, 2U);
    EXPECT_EQ(committed->batches()[0].clip, firstClipId);
    EXPECT_EQ(committed->batches()[1].firstCommand, 2U);
    EXPECT_EQ(committed->batches()[1].commandCount, 1U);
    EXPECT_FALSE(committed->batches()[1].clip.hasClip());
    EXPECT_EQ(committed->batches()[2].clip, secondClipId);
    EXPECT_EQ(committed->batches()[3].clip, firstClipId);

    const u64 firstChecksum = committed->paintOrderChecksum();
    EXPECT_NE(firstChecksum, 0U);
    ASSERT_TRUE(builder.beginFrame().has_value());
    for (const auto& input : inputs)
    {
        ASSERT_TRUE(builder.addSolidQuad(input).has_value());
    }
    auto repeated = builder.commit();
    ASSERT_TRUE(repeated.has_value());
    EXPECT_EQ(repeated->paintOrderChecksum(), firstChecksum);

    auto changedClipInputs = inputs;
    constexpr Render::UIPixelRect changedFirstClip{0, 0, 99, 100};
    changedClipInputs[0].effectiveClip = changedFirstClip;
    changedClipInputs[1].effectiveClip = changedFirstClip;
    changedClipInputs[4].effectiveClip = changedFirstClip;
    ASSERT_TRUE(builder.beginFrame().has_value());
    for (const auto& input : changedClipInputs)
    {
        ASSERT_TRUE(builder.addSolidQuad(input).has_value());
    }
    auto changedClip = builder.commit();
    ASSERT_TRUE(changedClip.has_value());
    EXPECT_NE(changedClip->paintOrderChecksum(), firstChecksum);
}

TEST(UIDisplayListTest, CapacityFailureRollsBackTheWholeUnpublishedBuild)
{
    auto builder = createBuilder({.commandCount = 2, .clipCount = 1, .batchCount = 2});
    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 10, 10},
                        .color = opaque(1, 2, 3),
                        .effectiveClip = Render::UIPixelRect{0, 0, 10, 10},
                    })
                    .has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 1,
                        .bounds = {10, 0, 10, 10},
                        .color = opaque(2, 3, 4),
                    })
                    .has_value());

    auto exceeded = builder.addSolidQuad({
        .paintOrdinal = 2,
        .bounds = {20, 0, 10, 10},
        .color = opaque(3, 4, 5),
    });
    ASSERT_FALSE(exceeded.has_value());
    EXPECT_EQ(exceeded.error().code, Render::RenderErrorCode::DisplayListCapacityExceeded);
    EXPECT_TRUE(builder.publishedView().empty());

    auto commit = builder.commit();
    ASSERT_FALSE(commit.has_value());
    EXPECT_EQ(commit.error().code, Render::RenderErrorCode::DisplayListCapacityExceeded);
    EXPECT_TRUE(builder.publishedView().empty());
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 1U);
    EXPECT_EQ(builder.statistics().capacityFailureCount, 1U);

    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 1, 1},
                        .color = opaque(1, 1, 1),
                    })
                    .has_value());
    auto recovered = builder.commit();
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(recovered->commands().size(), 1U);
}

TEST(UIDisplayListTest, BatchAndClipCapacityArePreflightedBeforeMutation)
{
    auto batchLimited = createBuilder({.commandCount = 3, .clipCount = 1, .batchCount = 1});
    ASSERT_TRUE(batchLimited.beginFrame().has_value());
    ASSERT_TRUE(batchLimited
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 10, 10},
                        .color = opaque(1, 1, 1),
                    })
                    .has_value());
    auto batchFailure = batchLimited.addSolidQuad({
        .paintOrdinal = 1,
        .bounds = {10, 0, 10, 10},
        .color = opaque(2, 2, 2),
        .effectiveClip = Render::UIPixelRect{10, 0, 10, 10},
    });
    ASSERT_FALSE(batchFailure.has_value());
    EXPECT_EQ(batchFailure.error().code, Render::RenderErrorCode::DisplayListCapacityExceeded);
    EXPECT_FALSE(batchLimited.commit().has_value());

    auto clipLimited = createBuilder({.commandCount = 3, .clipCount = 1, .batchCount = 3});
    ASSERT_TRUE(clipLimited.beginFrame().has_value());
    ASSERT_TRUE(clipLimited
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 10, 10},
                        .color = opaque(1, 1, 1),
                        .effectiveClip = Render::UIPixelRect{0, 0, 10, 10},
                    })
                    .has_value());
    auto clipFailure = clipLimited.addSolidQuad({
        .paintOrdinal = 1,
        .bounds = {10, 0, 10, 10},
        .color = opaque(2, 2, 2),
        .effectiveClip = Render::UIPixelRect{10, 0, 10, 10},
    });
    ASSERT_FALSE(clipFailure.has_value());
    EXPECT_EQ(clipFailure.error().code, Render::RenderErrorCode::DisplayListCapacityExceeded);
    EXPECT_FALSE(clipLimited.commit().has_value());
}

TEST(UIDisplayListTest, InvalidPremultipliedColorFailsAndRollsBackTheBuild)
{
    auto builder = createBuilder({.commandCount = 2, .clipCount = 1, .batchCount = 2});
    ASSERT_TRUE(builder.beginFrame().has_value());
    auto invalid = builder.addSolidQuad({
        .paintOrdinal = 0,
        .bounds = {0, 0, 10, 10},
        .color = {.red = 2, .green = 0, .blue = 0, .alpha = 1},
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidPremultipliedColor);
    auto commit = builder.commit();
    ASSERT_FALSE(commit.has_value());
    EXPECT_EQ(commit.error().code, Render::RenderErrorCode::InvalidPremultipliedColor);
    EXPECT_EQ(builder.statistics().invalidInputFailureCount, 1U);
}

TEST(UIDisplayListTest, SolidQuadCornerRadiusIsValidatedStoredAndChecksummed)
{
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});
    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 20, 10},
                        .color = opaque(10, 20, 30),
                        .cornerRadius = 4.0F,
                    })
                    .has_value());
    auto rounded = builder.commit();
    ASSERT_TRUE(rounded.has_value());
    ASSERT_EQ(rounded->commands().size(), 1U);
    EXPECT_FLOAT_EQ(rounded->commands().front().cornerRadius, 4.0F);
    const u64 roundedChecksum = rounded->paintOrderChecksum();

    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 20, 10},
                        .color = opaque(10, 20, 30),
                    })
                    .has_value());
    auto square = builder.commit();
    ASSERT_TRUE(square.has_value());
    EXPECT_NE(square->paintOrderChecksum(), roundedChecksum);

    ASSERT_TRUE(builder.beginFrame().has_value());
    auto oversized = builder.addSolidQuad({
        .paintOrdinal = 0,
        .bounds = {0, 0, 20, 10},
        .color = opaque(10, 20, 30),
        .cornerRadius = 5.5F,
    });
    ASSERT_FALSE(oversized.has_value());
    EXPECT_EQ(oversized.error().code, Render::RenderErrorCode::InvalidDrawCommand);
    EXPECT_FALSE(builder.commit().has_value());
}

TEST(UIDisplayListTest, PerformsNoFurtherPmrAllocationsAfterCreate)
{
    TrackingMemoryResource storage;
    {
        auto builder = createBuilder({.commandCount = 8, .clipCount = 4, .batchCount = 8}, storage);
        const usize createAllocationCount = storage.allocationCount;
        EXPECT_EQ(createAllocationCount, 3U);

        for (u32 frame = 0; frame < 300; ++frame)
        {
            ASSERT_TRUE(builder.beginFrame().has_value());
            for (u32 command = 0; command < 8; ++command)
            {
                ASSERT_TRUE(builder
                                .addSolidQuad({
                                    .paintOrdinal = command,
                                    .bounds = {static_cast<i32>(command), 0, 1, 1},
                                    .color = opaque(1, 2, 3),
                                    .effectiveClip = Render::UIPixelRect{0, 0, 8, 1},
                                })
                                .has_value());
            }
            auto committed = builder.commit();
            ASSERT_TRUE(committed.has_value());
            EXPECT_EQ(committed->commands().size(), 8U);
            EXPECT_EQ(committed->batches().size(), 1U);
        }
        EXPECT_EQ(storage.allocationCount, createAllocationCount);
    }
    EXPECT_EQ(storage.allocationCount, storage.deallocationCount);
}

TEST(UIDisplayListTest, EnforcesExplicitBuildTransactionState)
{
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});
    auto withoutBegin = builder.addSolidQuad({.paintOrdinal = 0, .bounds = {0, 0, 1, 1}, .color = opaque(1, 1, 1)});
    ASSERT_FALSE(withoutBegin.has_value());
    EXPECT_EQ(withoutBegin.error().code, Render::RenderErrorCode::DisplayListBuildNotOpen);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame().has_value());
    auto duplicateBegin = builder.beginFrame();
    ASSERT_FALSE(duplicateBegin.has_value());
    EXPECT_EQ(duplicateBegin.error().code, Render::RenderErrorCode::DisplayListBuildAlreadyOpen);
    builder.rollback();
    EXPECT_TRUE(builder.publishedView().empty());
    EXPECT_EQ(builder.statistics().rolledBackBuildCount, 1U);
}

TEST(UIDisplayListTest, MoveTransfersEveryFixedStorageBlockExactlyOnce)
{
    TrackingMemoryResource storage;
    {
        auto result =
            Render::UIDisplayListBuilder::Create({.commandCount = 2, .clipCount = 1, .batchCount = 2}, storage);
        ASSERT_TRUE(result.has_value());
        Render::UIDisplayListBuilder first = std::move(*result);
        Render::UIDisplayListBuilder second = std::move(first);
        ASSERT_TRUE(second.beginFrame().has_value());
        ASSERT_TRUE(second
                        .addSolidQuad({
                            .paintOrdinal = 0,
                            .bounds = {0, 0, 1, 1},
                            .color = opaque(1, 1, 1),
                            .effectiveClip = Render::UIPixelRect{0, 0, 1, 1},
                        })
                        .has_value());
        ASSERT_TRUE(second.commit().has_value());
        EXPECT_EQ(storage.allocationCount, 3U);
        EXPECT_EQ(storage.deallocationCount, 0U);
    }
    EXPECT_EQ(storage.allocationCount, 3U);
    EXPECT_EQ(storage.deallocationCount, 3U);
}

TEST(UIDisplayListTest, RejectsDuplicateOrDecreasingPaintOrdinals)
{
    auto builder = createBuilder({.commandCount = 3, .clipCount = 0, .batchCount = 1});
    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 5,
                        .bounds = {0, 0, 1, 1},
                        .color = opaque(1, 1, 1),
                    })
                    .has_value());
    auto duplicate = builder.addSolidQuad({
        .paintOrdinal = 5,
        .bounds = {1, 0, 1, 1},
        .color = opaque(1, 1, 1),
    });
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, Render::RenderErrorCode::InvalidDrawCommand);
    auto duplicateCommit = builder.commit();
    ASSERT_FALSE(duplicateCommit.has_value());
    EXPECT_EQ(duplicateCommit.error().code, Render::RenderErrorCode::InvalidDrawCommand);

    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 5,
                        .bounds = {0, 0, 1, 1},
                        .color = opaque(1, 1, 1),
                    })
                    .has_value());
    auto decreasing = builder.addSolidQuad({
        .paintOrdinal = 4,
        .bounds = {1, 0, 1, 1},
        .color = opaque(1, 1, 1),
    });
    ASSERT_FALSE(decreasing.has_value());
    EXPECT_EQ(decreasing.error().code, Render::RenderErrorCode::InvalidDrawCommand);
    EXPECT_FALSE(builder.commit().has_value());
}

TEST(UIDisplayListTest, BeginningAReplacementBuildInvalidatesThePreviousSingleBufferView)
{
    auto builder = createBuilder({.commandCount = 1, .clipCount = 0, .batchCount = 1});
    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 0,
                        .bounds = {0, 0, 1, 1},
                        .color = opaque(1, 1, 1),
                    })
                    .has_value());
    auto first = builder.commit();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->commands().size(), 1U);

    ASSERT_TRUE(builder.beginFrame().has_value());
    EXPECT_TRUE(builder.publishedView().empty());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 2,
                        .bounds = {0, 0, 1, 1},
                        .color = opaque(1, 1, 1),
                    })
                    .has_value());
    auto overflow = builder.addSolidQuad({
        .paintOrdinal = 3,
        .bounds = {1, 0, 1, 1},
        .color = opaque(1, 1, 1),
    });
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Render::RenderErrorCode::DisplayListCapacityExceeded);
    EXPECT_TRUE(builder.publishedView().empty());
    EXPECT_FALSE(builder.commit().has_value());
    EXPECT_TRUE(builder.publishedView().empty());
}

TEST(UIDisplayListTest, AcceptsGlyphCommandsAndBatchesByClipAndAtlasPage)
{
    auto builder = createBuilder({.commandCount = 4, .clipCount = 2, .batchCount = 4});
    ASSERT_TRUE(builder.beginFrame().has_value());

    constexpr Render::UIPixelRect clip{0, 0, 100, 100};
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 1,
                        .bounds = {0, 0, 8, 8},
                        .color = opaque(10, 20, 30),
                    })
                    .has_value());
    ASSERT_TRUE(builder
                    .addGlyphQuad({
                        .paintOrdinal = 2,
                        .bounds = {8, 0, 8, 8},
                        .color = opaque(255, 255, 255),
                        .atlasUv = {0, 0, 4, 4},
                        .atlasPage = 0,
                        .effectiveClip = clip,
                    })
                    .has_value());
    ASSERT_TRUE(builder
                    .addGlyphQuad({
                        .paintOrdinal = 3,
                        .bounds = {16, 0, 8, 8},
                        .color = opaque(255, 255, 255),
                        .atlasUv = {4, 0, 4, 4},
                        .atlasPage = 0,
                        .effectiveClip = clip,
                    })
                    .has_value());
    ASSERT_TRUE(builder
                    .addGlyphQuad({
                        .paintOrdinal = 4,
                        .bounds = {24, 0, 8, 8},
                        .color = opaque(255, 255, 255),
                        .atlasUv = {0, 4, 4, 4},
                        .atlasPage = 1,
                        .effectiveClip = clip,
                    })
                    .has_value());

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value());
    ASSERT_EQ(committed->commands().size(), 4U);
    EXPECT_EQ(committed->commands()[0].kind, Render::UIDrawCommandKind::SolidQuad);
    EXPECT_EQ(committed->commands()[1].kind, Render::UIDrawCommandKind::Glyph);
    EXPECT_EQ(committed->commands()[1].atlasUv, (Render::UIPixelRect{0, 0, 4, 4}));
    EXPECT_EQ(committed->commands()[1].atlasPage, 0U);
    EXPECT_EQ(committed->commands()[3].atlasPage, 1U);
    EXPECT_EQ(committed->statistics().solidQuadCommandCount, 1U);
    EXPECT_EQ(committed->statistics().glyphCommandCount, 3U);

    // Solid | Glyph page0 (2 cmds, same clip) | Glyph page1
    ASSERT_EQ(committed->batches().size(), 3U);
    EXPECT_EQ(committed->batches()[0].kind, Render::UIDrawCommandKind::SolidQuad);
    EXPECT_EQ(committed->batches()[0].commandCount, 1U);
    EXPECT_EQ(committed->batches()[1].kind, Render::UIDrawCommandKind::Glyph);
    EXPECT_EQ(committed->batches()[1].commandCount, 2U);
    EXPECT_EQ(committed->batches()[1].atlasPage, 0U);
    EXPECT_EQ(committed->batches()[2].kind, Render::UIDrawCommandKind::Glyph);
    EXPECT_EQ(committed->batches()[2].commandCount, 1U);
    EXPECT_EQ(committed->batches()[2].atlasPage, 1U);
}

TEST(UIDisplayListTest, GlyphRequiresAtlasUvForNonEmptyBoundsAndSharesOrdinalStream)
{
    auto builder = createBuilder({.commandCount = 2, .clipCount = 0, .batchCount = 2});
    ASSERT_TRUE(builder.beginFrame().has_value());
    auto missingUv = builder.addGlyphQuad({
        .paintOrdinal = 1,
        .bounds = {0, 0, 4, 4},
        .color = opaque(1, 1, 1),
        .atlasUv = {},
        .atlasPage = 0,
    });
    ASSERT_FALSE(missingUv.has_value());
    EXPECT_EQ(missingUv.error().code, Render::RenderErrorCode::InvalidDrawCommand);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame().has_value());
    ASSERT_TRUE(builder
                    .addSolidQuad({
                        .paintOrdinal = 5,
                        .bounds = {0, 0, 1, 1},
                        .color = opaque(1, 1, 1),
                    })
                    .has_value());
    auto nonIncreasing = builder.addGlyphQuad({
        .paintOrdinal = 5,
        .bounds = {1, 0, 1, 1},
        .color = opaque(1, 1, 1),
        .atlasUv = {0, 0, 1, 1},
    });
    ASSERT_FALSE(nonIncreasing.has_value());
    EXPECT_EQ(nonIncreasing.error().code, Render::RenderErrorCode::InvalidDrawCommand);
}

} // namespace Tina::Tests
