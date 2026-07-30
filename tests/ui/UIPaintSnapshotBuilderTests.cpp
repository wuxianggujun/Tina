#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>

#include "detail/UIPaintSnapshotBuilder.hpp"

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Tests {
namespace {

struct PaintSnapshotSource final {
    mutable usize countCallCount = 0;
    usize appendCallCount = 0;
    bool rejectNegativeX = false;
};

[[nodiscard]] Core::Result<usize> countEntries(const void* context, const UI::UICommittedLayoutEntry& layoutEntry)
{
    const auto& source = *static_cast<const PaintSnapshotSource*>(context);
    ++source.countCallCount;
    if (source.rejectNegativeX && layoutEntry.worldRect.x < 0.0F)
    {
        return Core::failure(UI::UIErrorCode::InvalidNode, "test paint source rejected the node");
    }
    return static_cast<usize>(layoutEntry.worldRect.width);
}

Core::Status appendEntries(void* context, std::pmr::vector<UI::UICommittedPaintEntry>& output,
                           const UI::UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal)
{
    auto& source = *static_cast<PaintSnapshotSource*>(context);
    ++source.appendCallCount;
    const usize entryCount = static_cast<usize>(layoutEntry.worldRect.width);
    for (usize index = 0; index < entryCount; ++index)
    {
        output.push_back(UI::UICommittedPaintEntry{
            .node = layoutEntry.node,
            .worldRect = layoutEntry.worldRect,
            .effectiveClip = layoutEntry.effectiveClip,
            .paintOrdinal = nextPaintOrdinal++,
        });
    }
    return Core::success();
}

constexpr UI::Detail::UIPaintSnapshotSourceAdapter PaintSourceAdapter{
    .countEntries = &countEntries,
    .appendEntries = &appendEntries,
};

TEST(UIPaintSnapshotBuilderTests, SkipsInvisibleEntriesAndBuildsStableOrdinals)
{
    UI::Detail::UIPaintSnapshotBuilder builder(4);
    PaintSnapshotSource source{};
    const std::array layoutEntries{
        UI::UICommittedLayoutEntry{.worldRect = {.width = 2.0F}},
        UI::UICommittedLayoutEntry{
            .worldRect = {.width = 4.0F},
            .effectiveVisibility = UI::UIVisibility::Hidden,
        },
        UI::UICommittedLayoutEntry{.worldRect = {.width = 1.0F}},
    };

    const Core::Result<usize> count = builder.validateCapacity(layoutEntries, &source, PaintSourceAdapter);
    ASSERT_TRUE(count.has_value()) << count.error().message;
    EXPECT_EQ(*count, 3U);
    EXPECT_EQ(source.countCallCount, 2U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(4);
    output.push_back(UI::UICommittedPaintEntry{.paintOrdinal = 99});
    const Core::Status buildStatus = builder.build(output, layoutEntries, &source, PaintSourceAdapter);
    ASSERT_TRUE(buildStatus.has_value()) << buildStatus.error().message;

    ASSERT_EQ(output.size(), 3U);
    EXPECT_EQ(output[0].paintOrdinal, 1U);
    EXPECT_EQ(output[1].paintOrdinal, 2U);
    EXPECT_EQ(output[2].paintOrdinal, 3U);
    EXPECT_EQ(source.appendCallCount, 2U);
}

TEST(UIPaintSnapshotBuilderTests, ReportsCapacityAndSourceFailures)
{
    UI::Detail::UIPaintSnapshotBuilder builder(2);
    PaintSnapshotSource source{};
    const std::array oversized{
        UI::UICommittedLayoutEntry{.worldRect = {.width = 2.0F}},
        UI::UICommittedLayoutEntry{.worldRect = {.width = 1.0F}},
    };

    const Core::Result<usize> exhausted = builder.validateCapacity(oversized, &source, PaintSourceAdapter);
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);

    source.rejectNegativeX = true;
    const std::array invalid{
        UI::UICommittedLayoutEntry{.worldRect = {.x = -1.0F, .width = 1.0F}},
    };
    const Core::Result<usize> rejected = builder.validateCapacity(invalid, &source, PaintSourceAdapter);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidNode);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    const Core::Status missingAppendSource = builder.build(output, invalid, nullptr, PaintSourceAdapter);
    ASSERT_FALSE(missingAppendSource.has_value());
    EXPECT_EQ(missingAppendSource.error().code, Core::CoreErrorCode::Internal);
}

} // namespace
} // namespace Tina::Tests
