#include <gtest/gtest.h>

#include <tina/math/Mat4.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace Tina::Tests {
namespace {

constexpr float Tolerance = 1.0e-5F;
constexpr float Infinity = std::numeric_limits<float>::infinity();
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();

void expectVectorNear(Math::Vec3 actual, Math::Vec3 expected, float tolerance = Tolerance)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

void expectMatrixNear(const Math::Mat4& actual, const Math::Mat4& expected)
{
    for (usize index = 0; index < 16U; ++index) {
        EXPECT_NEAR(actual.columns[index], expected.columns[index], Tolerance)
            << "element " << index;
    }
}

} // namespace

// Column-major storage is the published contract: element(row, column) lives at
// columns[column * 4 + row]. A transposed implementation passes most symmetric
// tests, so this asserts the raw indices directly.
TEST(MathMat4Test, StorageIsColumnMajor)
{
    Math::Mat4 value{};
    value.at(0, 3) = 5.0F;
    EXPECT_FLOAT_EQ(value.columns[12], 5.0F);

    value.at(3, 0) = 7.0F;
    EXPECT_FLOAT_EQ(value.columns[3], 7.0F);

    const Math::Mat4 translation = Math::translationMat4(Math::Vec3{1.0F, 2.0F, 3.0F});
    EXPECT_FLOAT_EQ(translation.columns[12], 1.0F);
    EXPECT_FLOAT_EQ(translation.columns[13], 2.0F);
    EXPECT_FLOAT_EQ(translation.columns[14], 3.0F);
    EXPECT_FLOAT_EQ(translation.columns[15], 1.0F);
}

TEST(MathMat4Test, AccessorsReadRowsColumnsAndBasis)
{
    const Math::Mat4 value = Math::fromTrs(
        Math::Vec3{10.0F, 20.0F, 30.0F},
        Math::Quaternion{},
        Math::Vec3{2.0F, 3.0F, 4.0F});

    EXPECT_EQ(value.basisX(), (Math::Vec3{2.0F, 0.0F, 0.0F}));
    EXPECT_EQ(value.basisY(), (Math::Vec3{0.0F, 3.0F, 0.0F}));
    EXPECT_EQ(value.basisZ(), (Math::Vec3{0.0F, 0.0F, 4.0F}));
    EXPECT_EQ(value.translation(), (Math::Vec3{10.0F, 20.0F, 30.0F}));
    EXPECT_EQ(value.column(3), (Math::Vec4{10.0F, 20.0F, 30.0F, 1.0F}));
    EXPECT_EQ(value.row(0), (Math::Vec4{2.0F, 0.0F, 0.0F, 10.0F}));
}

TEST(MathMat4Test, IdentityLeavesPointsAndDirectionsUnchanged)
{
    const Math::Mat4 identity = Math::identityMat4();
    EXPECT_EQ(Math::transformPoint(identity, Math::Vec3{1.0F, 2.0F, 3.0F}),
              (Math::Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(Math::transformDirection(identity, Math::Vec3{1.0F, 2.0F, 3.0F}),
              (Math::Vec3{1.0F, 2.0F, 3.0F}));
    expectMatrixNear(Math::multiply(identity, identity), identity);
}

// transformPoint applies translation; transformDirection does not. Confusing the
// two is how a normal or an axis ends up offset by the object's position.
TEST(MathMat4Test, DirectionsIgnoreTranslation)
{
    const Math::Mat4 transform = Math::fromTrs(
        Math::Vec3{100.0F, 0.0F, 0.0F},
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi),
        Math::Vec3{1.0F, 1.0F, 1.0F});

    expectVectorNear(Math::transformPoint(transform, Math::Vec3{1.0F, 0.0F, 0.0F}),
                     Math::Vec3{100.0F, 1.0F, 0.0F});
    expectVectorNear(Math::transformDirection(transform, Math::Vec3{1.0F, 0.0F, 0.0F}),
                     Math::Vec3{0.0F, 1.0F, 0.0F});
}

// fromTrs is Translate * Rotate * Scale with scale innermost, matching the order
// Scene transform composition uses.
TEST(MathMat4Test, FromTrsAppliesScaleThenRotationThenTranslation)
{
    const Math::Quaternion aboutZ =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi);
    const Math::Mat4 transform =
        Math::fromTrs(Math::Vec3{5.0F, 6.0F, 7.0F}, aboutZ, Math::Vec3{2.0F, 3.0F, 4.0F});

    // Scale 2 first (x -> 2), then rotate about Z (+X -> +Y), then translate.
    expectVectorNear(Math::transformPoint(transform, Math::Vec3{1.0F, 0.0F, 0.0F}),
                     Math::Vec3{5.0F, 8.0F, 7.0F});
    // Equivalent to the explicit product of the three matrices.
    expectMatrixNear(transform,
                     Math::multiply(Math::translationMat4(Math::Vec3{5.0F, 6.0F, 7.0F}),
                                    Math::multiply(Math::fromQuaternion(aboutZ),
                                                   Math::scaleMat4(Math::Vec3{2.0F, 3.0F, 4.0F}))));
}

// Regression gate against the hand-written builders this replaced in
// src/render/RenderScene.cpp and src/scene/Animator3D.cpp. Those produced the
// published 3D mesh transforms and skinned joint palettes, so fromTrs must match
// them element for element — a divergence would surface as shifted geometry rather
// than a failed build.
TEST(MathMat4Test, FromTrsMatchesTheReplacedHandWrittenBuilder)
{
    const auto legacyColumnMajorWorldTransform =
        [](Math::Vec3 position, Math::Quaternion rotation, Math::Vec3 scale) {
            const float xx = rotation.x * rotation.x;
            const float yy = rotation.y * rotation.y;
            const float zz = rotation.z * rotation.z;
            const float xy = rotation.x * rotation.y;
            const float xz = rotation.x * rotation.z;
            const float yz = rotation.y * rotation.z;
            const float wx = rotation.w * rotation.x;
            const float wy = rotation.w * rotation.y;
            const float wz = rotation.w * rotation.z;
            return std::array<float, 16>{
                (1.0F - 2.0F * (yy + zz)) * scale.x,
                (2.0F * (xy + wz)) * scale.x,
                (2.0F * (xz - wy)) * scale.x,
                0.0F,
                (2.0F * (xy - wz)) * scale.y,
                (1.0F - 2.0F * (xx + zz)) * scale.y,
                (2.0F * (yz + wx)) * scale.y,
                0.0F,
                (2.0F * (xz + wy)) * scale.z,
                (2.0F * (yz - wx)) * scale.z,
                (1.0F - 2.0F * (xx + yy)) * scale.z,
                0.0F,
                position.x,
                position.y,
                position.z,
                1.0F,
            };
        };

    struct Sample final {
        Math::Vec3 position;
        Math::Vec3 axis;
        float angleRadians;
        Math::Vec3 scale;
    };
    const std::array samples{
        Sample{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 0.0F, {1.0F, 1.0F, 1.0F}},
        Sample{{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 1.0F}, Math::HalfPi, {1.0F, 1.0F, 1.0F}},
        Sample{{-4.0F, 5.5F, 6.25F}, {1.0F, 2.0F, 3.0F}, 0.87F, {2.0F, 3.0F, 4.0F}},
        Sample{{100.0F, -200.0F, 0.5F}, {-1.0F, 0.5F, 2.0F}, -2.3F, {0.25F, 0.5F, 8.0F}},
        Sample{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, Math::Pi, {1.0F, 1.0F, 1.0F}},
    };

    for (const Sample& sample : samples) {
        const Math::Quaternion rotation =
            Math::normalized(Math::fromAxisAngle(sample.axis, sample.angleRadians));
        const std::array<float, 16> expected =
            legacyColumnMajorWorldTransform(sample.position, rotation, sample.scale);
        const Math::Mat4 actual =
            Math::fromTrs(sample.position, rotation, sample.scale);
        for (usize index = 0; index < 16U; ++index) {
            EXPECT_FLOAT_EQ(actual.columns[index], expected[index]) << "element " << index;
        }
    }
}

TEST(MathMat4Test, FromQuaternionMatchesDirectRotation)
{
    const Math::Quaternion rotation =
        Math::fromAxisAngle(Math::Vec3{1.0F, 2.0F, 3.0F}, 0.8F);
    const Math::Mat4 matrix = Math::fromQuaternion(rotation);
    const Math::Vec3 point{4.0F, -5.0F, 6.0F};

    expectVectorNear(Math::transformPoint(matrix, point), Math::rotate(rotation, point));
    // A pure rotation preserves length and has unit determinant.
    EXPECT_NEAR(Math::length(Math::transformDirection(matrix, point)), Math::length(point),
                1.0e-4F);
    EXPECT_NEAR(Math::linearDeterminant(matrix), 1.0F, Tolerance);
}

// Multiplication is associative but not commutative; the operator must agree with
// multiply(), and the argument order must mean "apply right operand first".
TEST(MathMat4Test, MultiplicationOrderAppliesRightOperandFirst)
{
    const Math::Mat4 scale = Math::scaleMat4(Math::Vec3{2.0F, 2.0F, 2.0F});
    const Math::Mat4 translation = Math::translationMat4(Math::Vec3{10.0F, 0.0F, 0.0F});

    // translation * scale: scale first, so the offset is not scaled.
    expectVectorNear(Math::transformPoint(translation * scale, Math::Vec3{1.0F, 0.0F, 0.0F}),
                     Math::Vec3{12.0F, 0.0F, 0.0F});
    // scale * translation: translate first, so the offset is scaled too.
    expectVectorNear(Math::transformPoint(scale * translation, Math::Vec3{1.0F, 0.0F, 0.0F}),
                     Math::Vec3{22.0F, 0.0F, 0.0F});
    expectMatrixNear(translation * scale, Math::multiply(translation, scale));
}

TEST(MathMat4Test, TransposeSwapsRowsAndColumns)
{
    const Math::Mat4 value = Math::fromTrs(
        Math::Vec3{1.0F, 2.0F, 3.0F},
        Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, 0.4F),
        Math::Vec3{1.0F, 2.0F, 3.0F});
    const Math::Mat4 transposed = Math::transpose(value);

    for (usize row = 0; row < 4U; ++row) {
        for (usize column = 0; column < 4U; ++column) {
            EXPECT_FLOAT_EQ(transposed.at(row, column), value.at(column, row));
        }
    }
    expectMatrixNear(Math::transpose(transposed), value);
}

TEST(MathMat4Test, DeterminantMatchesKnownValues)
{
    EXPECT_NEAR(Math::determinant(Math::identityMat4()), 1.0, Tolerance);
    // Affine scale: determinant is the product of the diagonal.
    EXPECT_NEAR(Math::determinant(Math::scaleMat4(Math::Vec3{2.0F, 3.0F, 4.0F})), 24.0,
                Tolerance);
    // Translation does not change volume.
    EXPECT_NEAR(Math::determinant(Math::translationMat4(Math::Vec3{5.0F, 6.0F, 7.0F})), 1.0,
                Tolerance);
    // A rotation preserves volume.
    EXPECT_NEAR(Math::determinant(Math::fromQuaternion(
                    Math::fromAxisAngle(Math::Vec3{1.0F, 1.0F, 1.0F}, 0.7F))),
                1.0, Tolerance);
}

// linearDeterminant covers only the 3x3 basis, which is what decides invertibility
// and handedness for an affine transform.
TEST(MathMat4Test, LinearDeterminantDetectsMirroring)
{
    EXPECT_GT(Math::linearDeterminant(Math::scaleMat4(Math::Vec3{1.0F, 1.0F, 1.0F})), 0.0F);
    EXPECT_LT(Math::linearDeterminant(Math::scaleMat4(Math::Vec3{-1.0F, 1.0F, 1.0F})), 0.0F);
    EXPECT_FLOAT_EQ(Math::linearDeterminant(Math::scaleMat4(Math::Vec3{2.0F, 0.0F, 3.0F})), 0.0F);
}

TEST(MathMat4Test, InverseRoundTripsAffineTransforms)
{
    const Math::Mat4 transform = Math::fromTrs(
        Math::Vec3{3.0F, -4.0F, 5.0F},
        Math::fromAxisAngle(Math::Vec3{1.0F, 2.0F, -3.0F}, 1.2F),
        Math::Vec3{2.0F, 0.5F, 3.0F});

    const std::optional<Math::Mat4> inverted = Math::inverse(transform);
    ASSERT_TRUE(inverted.has_value());
    expectMatrixNear(transform * *inverted, Math::identityMat4());
    expectMatrixNear(*inverted * transform, Math::identityMat4());

    // A point survives the round trip.
    const Math::Vec3 point{7.0F, 8.0F, 9.0F};
    expectVectorNear(Math::transformPoint(*inverted, Math::transformPoint(transform, point)),
                     point, 1.0e-4F);
}

// The inverse is the adjugate transposed over the determinant. Getting that
// transpose wrong still round-trips symmetric matrices, so this uses a translation,
// where the error shows up immediately.
TEST(MathMat4Test, InverseOfTranslationNegatesOffset)
{
    const std::optional<Math::Mat4> inverted =
        Math::inverse(Math::translationMat4(Math::Vec3{1.0F, 2.0F, 3.0F}));
    ASSERT_TRUE(inverted.has_value());
    EXPECT_NEAR(inverted->columns[12], -1.0F, Tolerance);
    EXPECT_NEAR(inverted->columns[13], -2.0F, Tolerance);
    EXPECT_NEAR(inverted->columns[14], -3.0F, Tolerance);
}

TEST(MathMat4Test, InverseRoundTripsAPerspectiveProjection)
{
    const std::optional<Math::Mat4> projection = Math::perspectiveRightHanded(
        Math::radians(60.0F), 16.0F / 9.0F, 0.1F, 100.0F, Math::ClipDepthRange::ZeroToOne);
    ASSERT_TRUE(projection.has_value());

    const std::optional<Math::Mat4> inverted = Math::inverse(*projection);
    ASSERT_TRUE(inverted.has_value());
    expectMatrixNear(*projection * *inverted, Math::identityMat4());
}

// A singular or non-finite matrix returns nullopt rather than a matrix full of
// infinities that would silently corrupt everything derived from it.
TEST(MathMat4Test, InverseRejectsSingularAndNonFiniteMatrices)
{
    EXPECT_FALSE(Math::inverse(Math::scaleMat4(Math::Vec3{0.0F, 1.0F, 1.0F})).has_value());

    Math::Mat4 allZero{};
    allZero.columns.fill(0.0F);
    EXPECT_FALSE(Math::inverse(allZero).has_value());

    Math::Mat4 nonFinite = Math::identityMat4();
    nonFinite.columns[5] = QuietNaN;
    EXPECT_FALSE(Math::inverse(nonFinite).has_value());

    nonFinite = Math::identityMat4();
    nonFinite.columns[5] = Infinity;
    EXPECT_FALSE(Math::inverse(nonFinite).has_value());
}

TEST(MathMat4Test, LookAtPlacesTheCameraLookingDownNegativeZ)
{
    const std::optional<Math::Mat4> view = Math::lookAtRightHanded(
        Math::Vec3{0.0F, 0.0F, 10.0F}, Math::Vec3{}, Math::Vec3{0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(view.has_value());

    // The target maps to -near along view Z, i.e. in front of the camera.
    const Math::Vec3 targetInView = Math::transformPoint(*view, Math::Vec3{});
    expectVectorNear(targetInView, Math::Vec3{0.0F, 0.0F, -10.0F});
    // The eye maps to the view-space origin.
    expectVectorNear(Math::transformPoint(*view, Math::Vec3{0.0F, 0.0F, 10.0F}), Math::Vec3{});
    // World +Y stays view +Y.
    expectVectorNear(Math::transformDirection(*view, Math::Vec3{0.0F, 1.0F, 0.0F}),
                     Math::Vec3{0.0F, 1.0F, 0.0F});
}

// A degenerate basis returns nullopt instead of snapping to an arbitrary default
// orientation, which is far harder to diagnose in a shadow cascade.
TEST(MathMat4Test, LookAtRejectsDegenerateBasis)
{
    EXPECT_FALSE(Math::lookAtRightHanded(Math::Vec3{}, Math::Vec3{},
                                         Math::Vec3{0.0F, 1.0F, 0.0F})
                     .has_value());
    // up parallel to the view axis leaves no unique right vector.
    EXPECT_FALSE(Math::lookAtRightHanded(Math::Vec3{0.0F, 0.0F, 10.0F}, Math::Vec3{},
                                         Math::Vec3{0.0F, 0.0F, 1.0F})
                     .has_value());
    EXPECT_FALSE(Math::lookAtRightHanded(Math::Vec3{QuietNaN, 0.0F, 0.0F}, Math::Vec3{},
                                         Math::Vec3{0.0F, 1.0F, 0.0F})
                     .has_value());
}

// The clip depth range is a device property, so the same camera must produce a
// different depth mapping for each. Confusing them halves or doubles depth
// precision on half the backends.
TEST(MathMat4Test, PerspectiveDepthRangeFollowsTheRequestedConvention)
{
    constexpr float Near = 1.0F;
    constexpr float Far = 100.0F;
    const std::optional<Math::Mat4> zeroToOne = Math::perspectiveRightHanded(
        Math::radians(60.0F), 1.0F, Near, Far, Math::ClipDepthRange::ZeroToOne);
    const std::optional<Math::Mat4> negativeOneToOne = Math::perspectiveRightHanded(
        Math::radians(60.0F), 1.0F, Near, Far, Math::ClipDepthRange::NegativeOneToOne);
    ASSERT_TRUE(zeroToOne.has_value());
    ASSERT_TRUE(negativeOneToOne.has_value());
    EXPECT_NE(zeroToOne->columns[10], negativeOneToOne->columns[10]);

    const auto depthAt = [](const Math::Mat4& projection, float viewDepth) {
        const Math::Vec4 clip =
            Math::transformVec4(projection, Math::toPoint(Math::Vec3{0.0F, 0.0F, -viewDepth}));
        return clip.z / clip.w;
    };
    EXPECT_NEAR(depthAt(*zeroToOne, Near), 0.0F, 1.0e-4F);
    EXPECT_NEAR(depthAt(*zeroToOne, Far), 1.0F, 1.0e-4F);
    EXPECT_NEAR(depthAt(*negativeOneToOne, Near), -1.0F, 1.0e-4F);
    EXPECT_NEAR(depthAt(*negativeOneToOne, Far), 1.0F, 1.0e-4F);
}

TEST(MathMat4Test, PerspectiveMapsFrustumEdgesToClipBoundaries)
{
    constexpr float Aspect = 2.0F;
    const float verticalFov = Math::radians(90.0F);
    const std::optional<Math::Mat4> projection = Math::perspectiveRightHanded(
        verticalFov, Aspect, 1.0F, 100.0F, Math::ClipDepthRange::ZeroToOne);
    ASSERT_TRUE(projection.has_value());

    // At depth 10 with a 90 degree vertical FOV the half-height is 10.
    const Math::Vec4 topEdge = Math::transformVec4(
        *projection, Math::toPoint(Math::Vec3{0.0F, 10.0F, -10.0F}));
    EXPECT_NEAR(topEdge.y / topEdge.w, 1.0F, 1.0e-4F);
    // The half-width is aspect times the half-height.
    const Math::Vec4 rightEdge = Math::transformVec4(
        *projection, Math::toPoint(Math::Vec3{20.0F, 0.0F, -10.0F}));
    EXPECT_NEAR(rightEdge.x / rightEdge.w, 1.0F, 1.0e-4F);
    // w is the positive view depth, so a caller can detect points behind the eye.
    EXPECT_NEAR(topEdge.w, 10.0F, 1.0e-4F);
}

TEST(MathMat4Test, PerspectiveRejectsInvalidParameters)
{
    using Math::ClipDepthRange;
    // Zero and full-turn field of view.
    EXPECT_FALSE(Math::perspectiveRightHanded(0.0F, 1.0F, 1.0F, 10.0F,
                                              ClipDepthRange::ZeroToOne)
                     .has_value());
    EXPECT_FALSE(Math::perspectiveRightHanded(Math::Pi, 1.0F, 1.0F, 10.0F,
                                              ClipDepthRange::ZeroToOne)
                     .has_value());
    // Non-positive aspect ratio and near plane.
    EXPECT_FALSE(Math::perspectiveRightHanded(1.0F, 0.0F, 1.0F, 10.0F,
                                              ClipDepthRange::ZeroToOne)
                     .has_value());
    EXPECT_FALSE(Math::perspectiveRightHanded(1.0F, 1.0F, 0.0F, 10.0F,
                                              ClipDepthRange::ZeroToOne)
                     .has_value());
    // Far at or behind near.
    EXPECT_FALSE(Math::perspectiveRightHanded(1.0F, 1.0F, 10.0F, 10.0F,
                                              ClipDepthRange::ZeroToOne)
                     .has_value());
    EXPECT_FALSE(Math::perspectiveRightHanded(1.0F, 1.0F, 10.0F, 1.0F,
                                              ClipDepthRange::ZeroToOne)
                     .has_value());
    EXPECT_FALSE(Math::perspectiveRightHanded(QuietNaN, 1.0F, 1.0F, 10.0F,
                                              ClipDepthRange::ZeroToOne)
                     .has_value());
}

TEST(MathMat4Test, OrthographicMapsBoundsToClipBoundaries)
{
    const std::optional<Math::Mat4> projection = Math::orthographicRightHanded(
        -4.0F, 4.0F, -2.0F, 2.0F, 1.0F, 11.0F, Math::ClipDepthRange::ZeroToOne);
    ASSERT_TRUE(projection.has_value());

    const auto clipOf = [&projection](Math::Vec3 point) {
        return Math::transformVec4(*projection, Math::toPoint(point));
    };
    // Orthographic keeps w at 1, so clip coordinates need no division.
    EXPECT_NEAR(clipOf(Math::Vec3{4.0F, 2.0F, 0.0F}).w, 1.0F, Tolerance);
    EXPECT_NEAR(clipOf(Math::Vec3{4.0F, 0.0F, 0.0F}).x, 1.0F, Tolerance);
    EXPECT_NEAR(clipOf(Math::Vec3{-4.0F, 0.0F, 0.0F}).x, -1.0F, Tolerance);
    EXPECT_NEAR(clipOf(Math::Vec3{0.0F, 2.0F, 0.0F}).y, 1.0F, Tolerance);
    // Depth: view -near maps to 0 and view -far maps to 1.
    EXPECT_NEAR(clipOf(Math::Vec3{0.0F, 0.0F, -1.0F}).z, 0.0F, Tolerance);
    EXPECT_NEAR(clipOf(Math::Vec3{0.0F, 0.0F, -11.0F}).z, 1.0F, Tolerance);
}

TEST(MathMat4Test, OrthographicRejectsDegenerateBounds)
{
    using Math::ClipDepthRange;
    EXPECT_FALSE(Math::orthographicRightHanded(1.0F, 1.0F, -1.0F, 1.0F, 1.0F, 10.0F,
                                               ClipDepthRange::ZeroToOne)
                     .has_value());
    EXPECT_FALSE(Math::orthographicRightHanded(-1.0F, 1.0F, 2.0F, 2.0F, 1.0F, 10.0F,
                                               ClipDepthRange::ZeroToOne)
                     .has_value());
    EXPECT_FALSE(Math::orthographicRightHanded(-1.0F, 1.0F, -1.0F, 1.0F, 5.0F, 5.0F,
                                               ClipDepthRange::ZeroToOne)
                     .has_value());
    EXPECT_FALSE(Math::orthographicRightHanded(-1.0F, 1.0F, -1.0F, 1.0F, Infinity, 10.0F,
                                               ClipDepthRange::ZeroToOne)
                     .has_value());
}

TEST(MathMat4Test, IsFiniteRejectsNonFiniteElements)
{
    EXPECT_TRUE(Math::isFinite(Math::identityMat4()));
    Math::Mat4 value = Math::identityMat4();
    value.columns[7] = QuietNaN;
    EXPECT_FALSE(Math::isFinite(value));
}

} // namespace Tina::Tests
