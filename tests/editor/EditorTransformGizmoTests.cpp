#include <tina/editor/EditorTransformGizmo.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <type_traits>

namespace Tina::Editor {
namespace {

[[nodiscard]] EditorTransformGizmoFrame makeFrame(
    EditorTransformGizmoDimension dimension =
        EditorTransformGizmoDimension::TwoD) noexcept
{
    EditorTransformGizmoFrame frame{
        .dimension = dimension,
        .screenOrigin = {100.0F, 100.0F},
        .worldAxes = {{
            {.screenPerWorldUnit = {40.0F, 0.0F},
             .worldDirection = {1.0F, 0.0F, 0.0F}},
            {.screenPerWorldUnit = {-24.0F, -30.0F},
             .worldDirection = {0.0F, 1.0F, 0.0F}},
            {.screenPerWorldUnit = {0.0F, 40.0F},
             .worldDirection = {0.0F, 0.0F, 1.0F}},
        }},
        .localAxes = {{
            {.screenPerWorldUnit = {0.0F, -40.0F},
             .worldDirection = {0.0F, 1.0F, 0.0F}},
            {.screenPerWorldUnit = {-40.0F, 0.0F},
             .worldDirection = {-1.0F, 0.0F, 0.0F}},
            {.screenPerWorldUnit = {0.0F, 40.0F},
             .worldDirection = {0.0F, 0.0F, 1.0F}},
        }},
    };
    if (dimension == EditorTransformGizmoDimension::TwoD) {
        frame.worldAxes[1].screenPerWorldUnit = {0.0F, -40.0F};
    }
    return frame;
}

TEST(EditorTransformGizmoTests, PublishesFixed2DAxisAndPlaneHandlesAndHitTestsThem)
{
    static_assert(std::is_trivially_copyable_v<EditorTransformGizmoSnapshot>);
    static_assert(EditorTransformGizmoHandleCapacity == 7U);

    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.publishFrame(makeFrame()),
              EditorTransformGizmoOperation::Success);

    const auto& snapshot = gizmo.snapshot();
    EXPECT_TRUE(snapshot.framePublished);
    EXPECT_FALSE(snapshot.dragging());
    EXPECT_EQ(snapshot.dimension, EditorTransformGizmoDimension::TwoD);
    EXPECT_EQ(snapshot.mode, EditorTransformGizmoMode::Translate);
    ASSERT_EQ(snapshot.handles().size(), 3U);
    EXPECT_EQ(snapshot.handles()[0].handle, EditorTransformGizmoHandle::AxisX);
    EXPECT_EQ(snapshot.handles()[1].handle, EditorTransformGizmoHandle::AxisY);
    EXPECT_EQ(snapshot.handles()[2].handle, EditorTransformGizmoHandle::PlaneXY);
    EXPECT_EQ(gizmo.hitTest({150.0F, 100.0F}),
              EditorTransformGizmoHandle::AxisX);
    EXPECT_EQ(gizmo.hitTest({125.0F, 75.0F}),
              EditorTransformGizmoHandle::PlaneXY);
    EXPECT_EQ(gizmo.hitTest({300.0F, 300.0F}),
              EditorTransformGizmoHandle::None);
}

TEST(EditorTransformGizmoTests, AxisTranslationUsesTotalDragAndConfiguredSnap)
{
    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.setSnap({
                  .enabled = true,
                  .translationStep = 0.5F,
                  .rotationStepDegrees = 15.0F,
                  .scaleStep = 0.25F,
              }),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.publishFrame(makeFrame()),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.beginDrag(7U, {150.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.updateDrag(7U, {175.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_TRUE(gizmo.snapshot().dragging());
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.x, 0.5F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.y, 0.0F);
    EXPECT_EQ(gizmo.snapshot().delta.handle, EditorTransformGizmoHandle::AxisX);

    ASSERT_EQ(gizmo.endDrag(7U, {180.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_FALSE(gizmo.snapshot().dragging());
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.x, 1.0F);
}

TEST(EditorTransformGizmoTests, ThreeDPlaneTranslationSolvesProjectedAxes)
{
    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.publishFrame(makeFrame(EditorTransformGizmoDimension::ThreeD)),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().handles().size(), 6U);
    ASSERT_EQ(gizmo.beginDrag(11U, {125.0F, 125.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().activeHandle,
              EditorTransformGizmoHandle::PlaneXZ);
    ASSERT_EQ(gizmo.updateDrag(11U, {165.0F, 165.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.x, 1.0F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.y, 0.0F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.z, 1.0F);
}

TEST(EditorTransformGizmoTests, LocalOrientationChangesProjectedAndTransformAxis)
{
    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.setOrientation(EditorTransformGizmoOrientation::Local),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.publishFrame(makeFrame()),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.hitTest({100.0F, 50.0F}),
              EditorTransformGizmoHandle::AxisX);
    ASSERT_EQ(gizmo.beginDrag(3U, {100.0F, 50.0F}),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.updateDrag(3U, {100.0F, 10.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().delta.orientation,
              EditorTransformGizmoOrientation::Local);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.x, 0.0F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.translation.y, 1.0F);
}

TEST(EditorTransformGizmoTests, RotationPublishesAxisAngleAndSnapsDegrees)
{
    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.setMode(EditorTransformGizmoMode::Rotate),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.setSnap({
                  .enabled = true,
                  .translationStep = 1.0F,
                  .rotationStepDegrees = 15.0F,
                  .scaleStep = 0.1F,
              }),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.publishFrame(makeFrame()),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.snapshot().handles().size(), 1U);
    EXPECT_EQ(gizmo.snapshot().handles()[0].handle,
              EditorTransformGizmoHandle::AxisZ);
    ASSERT_EQ(gizmo.beginDrag(19U, {158.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.endDrag(19U, {100.0F, 42.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().delta.handle, EditorTransformGizmoHandle::AxisZ);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.rotationAxis.z, 1.0F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.rotationDegrees, -90.0F);
}

TEST(EditorTransformGizmoTests, ScaleSupportsAxisPlaneAndUniformHandles)
{
    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.setMode(EditorTransformGizmoMode::Scale),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.publishFrame(makeFrame(EditorTransformGizmoDimension::ThreeD)),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.snapshot().handles().size(), 7U);

    ASSERT_EQ(gizmo.beginDrag(23U, {150.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.endDrag(23U, {170.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.scaleFactors.x, 1.5F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.scaleFactors.y, 1.0F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.scaleFactors.z, 1.0F);

    ASSERT_EQ(gizmo.beginDrag(24U, {100.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().activeHandle,
              EditorTransformGizmoHandle::Uniform);
    ASSERT_EQ(gizmo.endDrag(24U, {140.0F, 60.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.scaleFactors.x, 2.0F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.scaleFactors.y, 2.0F);
    EXPECT_FLOAT_EQ(gizmo.snapshot().delta.scaleFactors.z, 2.0F);
}

TEST(EditorTransformGizmoTests, CancelAndRejectedPointerPreserveAtomicDragState)
{
    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.publishFrame(makeFrame()),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.beginDrag(31U, {150.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    ASSERT_EQ(gizmo.updateDrag(31U, {170.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    const auto beforeMismatch = gizmo.snapshot();
    EXPECT_EQ(gizmo.cancelDrag(32U),
              EditorTransformGizmoOperation::PointerMismatch);
    EXPECT_EQ(gizmo.snapshot().revision, beforeMismatch.revision);
    EXPECT_EQ(gizmo.snapshot().delta, beforeMismatch.delta);

    ASSERT_EQ(gizmo.cancelDrag(31U), EditorTransformGizmoOperation::Success);
    EXPECT_FALSE(gizmo.snapshot().dragging());
    EXPECT_EQ(gizmo.snapshot().delta, EditorTransformGizmoDelta{});
    EXPECT_EQ(gizmo.updateDrag(31U, {180.0F, 100.0F}),
              EditorTransformGizmoOperation::NoActiveDrag);
}

TEST(EditorTransformGizmoTests, InvalidPublicationPreservesFixedSnapshot)
{
    EditorTransformGizmo gizmo;
    ASSERT_EQ(gizmo.publishFrame(makeFrame()),
              EditorTransformGizmoOperation::Success);
    const auto before = gizmo.snapshot();

    auto invalid = makeFrame();
    invalid.screenOrigin.x = (std::numeric_limits<float>::quiet_NaN)();
    EXPECT_EQ(gizmo.publishFrame(invalid),
              EditorTransformGizmoOperation::InvalidInput);
    EXPECT_EQ(gizmo.snapshot().revision, before.revision);
    EXPECT_EQ(gizmo.snapshot().handleCount, before.handleCount);
    EXPECT_EQ(gizmo.snapshot().handleStorage, before.handleStorage);
}

TEST(EditorTransformGizmoTests, IdenticalOperationsDoNotAdvanceRevision)
{
    EditorTransformGizmo gizmo;
    EXPECT_EQ(gizmo.configure({}), EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.setMode(EditorTransformGizmoMode::Translate),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.setOrientation(EditorTransformGizmoOrientation::World),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.setSnap({}), EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().revision, 0U);

    const auto frame = makeFrame();
    ASSERT_EQ(gizmo.publishFrame(frame), EditorTransformGizmoOperation::Success);
    const Core::u64 publishedRevision = gizmo.snapshot().revision;
    EXPECT_EQ(gizmo.publishFrame(frame), EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().revision, publishedRevision);

    ASSERT_EQ(gizmo.beginDrag(41U, {150.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    const Core::u64 dragRevision = gizmo.snapshot().revision;
    EXPECT_EQ(gizmo.updateDrag(41U, {150.0F, 100.0F}),
              EditorTransformGizmoOperation::Success);
    EXPECT_EQ(gizmo.snapshot().revision, dragRevision);
}

} // namespace
} // namespace Tina::Editor
