#include <tina/editor/EditorViewportGrid.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] bool isAxisAligned(const EditorViewportGridSegment& segment)
{
    return segment.startX == segment.endX || segment.startY == segment.endY;
}

[[nodiscard]] bool isNormalized(const EditorViewportGridSegment& segment)
{
    const auto valid = [](float coordinate) {
        return std::isfinite(coordinate) && coordinate >= 0.0F &&
               coordinate <= 1.0F;
    };
    return valid(segment.startX) && valid(segment.startY) &&
           valid(segment.endX) && valid(segment.endY);
}

[[nodiscard]] bool isDiagonal(const EditorViewportGridSegment& segment)
{
    return segment.startX != segment.endX && segment.startY != segment.endY;
}

TEST(EditorViewportGridTests, PublishesClippedOrthographicMinorMajorAndAxisLines)
{
    EditorViewportGrid grid;
    auto updated = grid.update({
        .projection = EditorViewportGridProjection::Orthographic2D,
        .logicalWidth = 900.0F,
        .logicalHeight = 600.0F,
    });
    ASSERT_TRUE(updated) << (updated ? "" : updated.error().message);
    EXPECT_TRUE(*updated);
    EXPECT_EQ(grid.stats().revision, 1U);
    EXPECT_GT(grid.stats().minorSegmentCount, 0U);
    EXPECT_GT(grid.stats().majorSegmentCount, 0U);
    EXPECT_EQ(grid.stats().axisSegmentCount, 2U);
    EXPECT_EQ(grid.segments().size(), grid.stats().segmentCount);
    EXPECT_TRUE(std::ranges::all_of(grid.segments(), isAxisAligned));
    EXPECT_TRUE(std::ranges::all_of(grid.segments(), isNormalized));
}

TEST(EditorViewportGridTests, StableConfigDoesNotRepublishAndZoomChangesGrid)
{
    const EditorViewportGridConfig config{
        .logicalWidth = 800.0F,
        .logicalHeight = 500.0F,
    };
    EditorViewportGrid grid;
    ASSERT_TRUE(grid.update(config));
    const auto firstSegments =
        std::vector(grid.segments().begin(), grid.segments().end());
    auto unchanged = grid.update(config);
    ASSERT_TRUE(unchanged);
    EXPECT_FALSE(*unchanged);
    EXPECT_EQ(grid.stats().revision, 1U);

    EditorViewportGridConfig zoomed = config;
    zoomed.zoomPercent = 200.0F;
    auto updated = grid.update(zoomed);
    ASSERT_TRUE(updated) << (updated ? "" : updated.error().message);
    EXPECT_TRUE(*updated);
    EXPECT_EQ(grid.stats().revision, 2U);
    EXPECT_NE(std::vector(grid.segments().begin(), grid.segments().end()),
              firstSegments);
}

TEST(EditorViewportGridTests, InvalidUpdatePreservesPublishedSegmentsAndRevision)
{
    EditorViewportGrid grid;
    ASSERT_TRUE(grid.update({.logicalWidth = 640.0F, .logicalHeight = 360.0F}));
    const auto before = std::vector(grid.segments().begin(), grid.segments().end());
    const EditorViewportGridStats statsBefore = grid.stats();

    const auto rejected =
        grid.update({.logicalWidth = 0.0F, .logicalHeight = 360.0F});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(grid.stats(), statsBefore);
    EXPECT_EQ(std::vector(grid.segments().begin(), grid.segments().end()), before);
}

TEST(EditorViewportGridTests, PerspectiveGridProjectsClippedXZGroundLines)
{
    EditorViewportGrid grid;
    auto updated = grid.update({
        .projection = EditorViewportGridProjection::Perspective3D,
        .logicalWidth = 900.0F,
        .logicalHeight = 600.0F,
    });
    ASSERT_TRUE(updated) << (updated ? "" : updated.error().message);
    EXPECT_TRUE(*updated);
    EXPECT_GT(grid.stats().segmentCount, 10U);
    EXPECT_GT(grid.stats().minorSegmentCount, 0U);
    EXPECT_GT(grid.stats().majorSegmentCount, 0U);
    EXPECT_EQ(grid.stats().axisSegmentCount, 2U);
    EXPECT_TRUE(std::ranges::all_of(grid.segments(), isNormalized));
    EXPECT_TRUE(std::ranges::any_of(grid.segments(), isDiagonal));
    EXPECT_LE(grid.segments().size(), EditorViewportGridSegmentCapacity);
}

TEST(EditorViewportGridTests, PerspectiveCameraStateChangesProjectedGroundGrid)
{
    EditorViewportGridConfig config{
        .projection = EditorViewportGridProjection::Perspective3D,
        .logicalWidth = 960.0F,
        .logicalHeight = 540.0F,
        .cameraTargetY = 0.75F,
    };
    EditorViewportGrid grid;
    ASSERT_TRUE(grid.update(config));
    const auto initial =
        std::vector(grid.segments().begin(), grid.segments().end());

    config.cameraYawRadians = 0.65F;
    ASSERT_TRUE(grid.update(config));
    const auto orbited =
        std::vector(grid.segments().begin(), grid.segments().end());
    EXPECT_NE(orbited, initial);

    config.cameraTargetX = 2.0F;
    config.cameraTargetZ = -1.5F;
    ASSERT_TRUE(grid.update(config));
    const auto panned =
        std::vector(grid.segments().begin(), grid.segments().end());
    EXPECT_NE(panned, orbited);

    config.cameraDistance = 5.0F;
    ASSERT_TRUE(grid.update(config));
    const auto dollied =
        std::vector(grid.segments().begin(), grid.segments().end());
    EXPECT_NE(dollied, panned);

    config.cameraPitchRadians = 0.6F;
    ASSERT_TRUE(grid.update(config));
    const auto pitched =
        std::vector(grid.segments().begin(), grid.segments().end());
    EXPECT_NE(pitched, dollied);

    config.verticalFovDegrees = 75.0F;
    ASSERT_TRUE(grid.update(config));
    const auto widerFov =
        std::vector(grid.segments().begin(), grid.segments().end());
    EXPECT_NE(widerFov, pitched);

    config.logicalWidth = 540.0F;
    ASSERT_TRUE(grid.update(config));
    EXPECT_NE(std::vector(grid.segments().begin(), grid.segments().end()),
              widerFov);
    EXPECT_TRUE(std::ranges::all_of(grid.segments(), isNormalized));
}

TEST(EditorViewportGridTests, InvalidPerspectiveCameraPreservesPublication)
{
    EditorViewportGrid grid;
    EditorViewportGridConfig config{
        .projection = EditorViewportGridProjection::Perspective3D,
        .logicalWidth = 900.0F,
        .logicalHeight = 600.0F,
    };
    ASSERT_TRUE(grid.update(config));
    const auto before =
        std::vector(grid.segments().begin(), grid.segments().end());
    const EditorViewportGridStats statsBefore = grid.stats();

    config.verticalFovDegrees = 180.0F;
    const auto rejected = grid.update(config);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(grid.stats(), statsBefore);
    EXPECT_EQ(std::vector(grid.segments().begin(), grid.segments().end()),
              before);
}

TEST(EditorViewportGridTests, FirstEmptyPerspectivePublicationStillAdvancesRevision)
{
    EditorViewportGrid grid;
    auto updated = grid.update({
        .projection = EditorViewportGridProjection::Perspective3D,
        .logicalWidth = 900.0F,
        .logicalHeight = 600.0F,
        .cameraTargetY = -10'000'000.0F,
        .cameraPitchRadians = 1.55334306F,
        .cameraDistance = 0.01F,
    });
    ASSERT_TRUE(updated) << (updated ? "" : updated.error().message);
    EXPECT_TRUE(*updated);
    EXPECT_TRUE(grid.segments().empty());
    EXPECT_EQ(grid.stats().revision, 1U);
    ASSERT_NE(grid.config(), nullptr);
    EXPECT_EQ(grid.config()->projection,
              EditorViewportGridProjection::Perspective3D);
}

TEST(EditorViewportGridTests, RejectsUnboundedOrthographicCenterAtomically)
{
    EditorViewportGrid grid;
    ASSERT_TRUE(grid.update({.logicalWidth = 640.0F, .logicalHeight = 360.0F}));
    const auto before = std::vector(grid.segments().begin(), grid.segments().end());
    const EditorViewportGridStats statsBefore = grid.stats();

    const auto rejected = grid.update({
        .logicalWidth = 640.0F,
        .logicalHeight = 360.0F,
        .cameraCenterX = 10'000'001.0F,
    });
    ASSERT_FALSE(rejected);
    EXPECT_EQ(grid.stats(), statsBefore);
    EXPECT_EQ(std::vector(grid.segments().begin(), grid.segments().end()), before);
}

TEST(EditorViewportGridTests, CameraPanMovesOrthographicAxisWithoutHeapGrowth)
{
    EditorViewportGrid grid;
    ASSERT_TRUE(grid.update({
        .logicalWidth = 800.0F,
        .logicalHeight = 500.0F,
        .cameraCenterX = 0.0F,
    }));
    const auto firstAxisY = std::ranges::find_if(
        grid.segments(), [](const EditorViewportGridSegment& segment) {
            return segment.kind == EditorViewportGridSegmentKind::AxisY;
        });
    ASSERT_NE(firstAxisY, grid.segments().end());
    const float firstX = firstAxisY->startX;

    ASSERT_TRUE(grid.update({
        .logicalWidth = 800.0F,
        .logicalHeight = 500.0F,
        .cameraCenterX = 2.0F,
    }));
    const auto movedAxisY = std::ranges::find_if(
        grid.segments(), [](const EditorViewportGridSegment& segment) {
            return segment.kind == EditorViewportGridSegmentKind::AxisY;
        });
    ASSERT_NE(movedAxisY, grid.segments().end());
    EXPECT_LT(movedAxisY->startX, firstX);
    EXPECT_LE(grid.segments().size(), EditorViewportGridSegmentCapacity);
}

} // namespace
} // namespace Tina::Editor
