#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <string>

namespace Tina::AssetFormat {
namespace {

TEST(SkinnedMeshPayloadTests, RoundTripsSingleJointMesh)
{
    const std::array<SkinnedMeshJointDesc, 1> joints{
        SkinnedMeshJointDesc{.parentJoint = SkinnedMeshWire::JointIndexNone}};
    const std::array<float, 16> inverseBind{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<StaticMeshSubmeshDesc, 1> submeshes{
        StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3}};
    const std::array<float, 3 * SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> jointIndices{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::array<Core::u16, 12> jointWeights{65535, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};

    const auto payload = writeSkinnedMeshPayloadBytes(SkinnedMeshPayloadDesc{
        .boundsRadius = 1.0F,
        .joints = joints,
        .inverseBindMatrices = inverseBind,
        .submeshes = submeshes,
        .vertices = vertices,
        .jointIndices = jointIndices,
        .jointWeights = jointWeights,
        .indices = indices,
    });
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseSkinnedMeshPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->schemaVersion, SkinnedMeshWire::SchemaVersion);
    EXPECT_EQ(view->jointCount, 1U);
    EXPECT_EQ(view->vertexCount, 3U);
    EXPECT_EQ(view->joint(0)->parentJoint, SkinnedMeshWire::JointIndexNone);
    EXPECT_FLOAT_EQ(view->inverseBindMatrix(0)[15], 1.0F);
    EXPECT_EQ(view->jointWeights[0], SkinnedMeshWire::WeightScale);
}

// Schema v2. A joint name is the only stable identity a joint has -- the cooked index is a
// (depth, sourceIndex) permutation the cooker computes -- so a bone mask, retarget mapping
// or IK goal has to key on it. Empty is legal because glTF nodes need not be named.
TEST(SkinnedMeshPayloadTests, RoundTripsJointNamesAndResolvesThemToIndices)
{
    const std::array<SkinnedMeshJointDesc, 3> joints{
        SkinnedMeshJointDesc{.parentJoint = SkinnedMeshWire::JointIndexNone, .name = "hips"},
        SkinnedMeshJointDesc{.parentJoint = 0, .name = "spine"},
        // Unnamed on purpose: the source did not name it, so neither does the payload.
        SkinnedMeshJointDesc{.parentJoint = 1},
    };
    const std::array<float, 3 * 16> inverseBind{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<StaticMeshSubmeshDesc, 1> submeshes{
        StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3}};
    const std::array<float, 3 * SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> jointIndices{0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
    const std::array<Core::u16, 12> jointWeights{65535, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};

    const auto payload = writeSkinnedMeshPayloadBytes(SkinnedMeshPayloadDesc{
        .boundsRadius = 1.0F,
        .joints = joints,
        .inverseBindMatrices = inverseBind,
        .submeshes = submeshes,
        .vertices = vertices,
        .jointIndices = jointIndices,
        .jointWeights = jointWeights,
        .indices = indices,
    });
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseSkinnedMeshPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;

    EXPECT_EQ(view->jointName(0), "hips");
    EXPECT_EQ(view->jointName(1), "spine");
    EXPECT_TRUE(view->jointName(2).empty());
    EXPECT_EQ(view->joint(1)->name, "spine");
    // Out of range yields empty rather than reading past the block.
    EXPECT_TRUE(view->jointName(3).empty());

    EXPECT_EQ(view->findJoint("hips"), 0U);
    EXPECT_EQ(view->findJoint("spine"), 1U);
    EXPECT_FALSE(view->findJoint("missing").has_value());
    // An unnamed joint is not addressable by name, so an empty query must not resolve to
    // whichever unnamed joint comes first.
    EXPECT_FALSE(view->findJoint("").has_value());
}

TEST(SkinnedMeshPayloadTests, RejectsDuplicateAndOversizedJointNames)
{
    const std::array<float, 2 * 16> inverseBind{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<StaticMeshSubmeshDesc, 1> submeshes{
        StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3}};
    const std::array<float, 3 * SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> jointIndices{0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    const std::array<Core::u16, 12> jointWeights{65535, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};

    const auto encode = [&](std::span<const SkinnedMeshJointDesc> encodeJoints) {
        return writeSkinnedMeshPayloadBytes(SkinnedMeshPayloadDesc{
            .boundsRadius = 1.0F,
            .joints = encodeJoints,
            .inverseBindMatrices = inverseBind,
            .submeshes = submeshes,
            .vertices = vertices,
            .jointIndices = jointIndices,
            .jointWeights = jointWeights,
            .indices = indices,
        });
    };

    const std::array<SkinnedMeshJointDesc, 2> duplicate{
        SkinnedMeshJointDesc{.parentJoint = SkinnedMeshWire::JointIndexNone, .name = "hips"},
        SkinnedMeshJointDesc{.parentJoint = 0, .name = "hips"},
    };
    EXPECT_FALSE(encode(duplicate).has_value());

    // Two unnamed joints are fine: neither is addressable by name.
    const std::array<SkinnedMeshJointDesc, 2> bothUnnamed{
        SkinnedMeshJointDesc{.parentJoint = SkinnedMeshWire::JointIndexNone},
        SkinnedMeshJointDesc{.parentJoint = 0},
    };
    EXPECT_TRUE(encode(bothUnnamed).has_value());

    const std::array<SkinnedMeshJointDesc, 2> tooLong{
        SkinnedMeshJointDesc{
            .parentJoint = SkinnedMeshWire::JointIndexNone,
            .name = std::string(SkinnedMeshWire::MaximumJointNameBytes + 1U, 'a')},
        SkinnedMeshJointDesc{.parentJoint = 0},
    };
    EXPECT_FALSE(encode(tooLong).has_value());

    // Exactly at the limit fits: the terminator occupies the 64th byte.
    const std::array<SkinnedMeshJointDesc, 2> exactlyAtLimit{
        SkinnedMeshJointDesc{
            .parentJoint = SkinnedMeshWire::JointIndexNone,
            .name = std::string(SkinnedMeshWire::MaximumJointNameBytes, 'a')},
        SkinnedMeshJointDesc{.parentJoint = 0},
    };
    EXPECT_TRUE(encode(exactlyAtLimit).has_value());

    const std::array<SkinnedMeshJointDesc, 2> embeddedNul{
        SkinnedMeshJointDesc{.parentJoint = SkinnedMeshWire::JointIndexNone,
                             .name = std::string("hi\0ps", 5)},
        SkinnedMeshJointDesc{.parentJoint = 0},
    };
    EXPECT_FALSE(encode(embeddedNul).has_value());
}

TEST(SkinnedMeshPayloadTests, CookedAssetRoundTripUsesNewKind)
{
    const std::array<SkinnedMeshJointDesc, 1> joints{};
    const std::array<float, 16> inverseBind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<StaticMeshSubmeshDesc, 1> submeshes{StaticMeshSubmeshDesc{.indexCount = 3}};
    const std::array<float, 3 * SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> jointIndices{};
    const std::array<Core::u16, 12> jointWeights{65535, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};
    const auto id = *Core::AssetId::fromBytes(Core::AssetId::Bytes{std::byte{0x41}});
    auto cooked = writeCookedSkinnedMeshAsset(id, SkinnedMeshPayloadDesc{
        .boundsRadius = 1.0F, .joints = joints, .inverseBindMatrices = inverseBind,
        .submeshes = submeshes, .vertices = vertices, .jointIndices = jointIndices,
        .jointWeights = jointWeights, .indices = indices});
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetKind::SkinnedMesh);
    EXPECT_EQ(asset->header().assetTypeVersion, SkinnedMeshWire::SchemaVersion);
}

TEST(SkinnedMeshPayloadTests, RejectsMalformedInfluenceAndTruncation)
{
    const std::array<SkinnedMeshJointDesc, 1> joints{};
    const std::array<float, 16> inverseBind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<StaticMeshSubmeshDesc, 1> submeshes{StaticMeshSubmeshDesc{.indexCount = 3}};
    const std::array<float, 3 * SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> indices{0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::array<Core::u16, 12> weights{65534, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    auto bad = writeSkinnedMeshPayloadBytes(SkinnedMeshPayloadDesc{
        .boundsRadius = 1.0F, .joints = joints, .inverseBindMatrices = inverseBind,
        .submeshes = submeshes, .vertices = vertices, .jointIndices = indices,
        .jointWeights = weights, .indices = std::array<Core::u16, 3>{0, 1, 2}});
    EXPECT_FALSE(bad.has_value());
    auto shortPayload = std::vector<std::byte>(SkinnedMeshWire::HeaderBytes - 1U);
    auto parsed = parseSkinnedMeshPayload(shortPayload);
    EXPECT_FALSE(parsed.has_value());
}

TEST(SkinnedMeshPayloadTests, RejectsMisalignedTypedBlocks)
{
    const std::array<SkinnedMeshJointDesc, 1> joints{};
    const std::array<float, 16> inverseBind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<StaticMeshSubmeshDesc, 1> submeshes{StaticMeshSubmeshDesc{.indexCount = 3}};
    const std::array<float, 3 * SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> jointIndices{};
    const std::array<Core::u16, 12> jointWeights{65535, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};
    const auto payload = writeSkinnedMeshPayloadBytes(SkinnedMeshPayloadDesc{
        .boundsRadius = 1.0F, .joints = joints, .inverseBindMatrices = inverseBind,
        .submeshes = submeshes, .vertices = vertices, .jointIndices = jointIndices,
        .jointWeights = jointWeights, .indices = indices});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;

    std::vector<std::byte> misaligned(payload->size() + 1U, std::byte{0});
    std::memcpy(misaligned.data() + 1U, payload->data(), payload->size());
    const auto parsed = parseSkinnedMeshPayload(
        std::span<const std::byte>{misaligned.data() + 1U, payload->size()});
    EXPECT_FALSE(parsed.has_value());
    if (!parsed)
    {
        EXPECT_EQ(parsed.error().code, AssetFormatErrorCode::InvalidLayout);
    }
}

TEST(SkinnedMeshPayloadTests, RejectsJointCountAboveFrozenLimit)
{
    std::vector<SkinnedMeshJointDesc> joints(SkinnedMeshWire::MaxJointCount + 1U);
    const auto payload = writeSkinnedMeshPayloadBytes(SkinnedMeshPayloadDesc{.joints = joints});
    EXPECT_FALSE(payload.has_value());
}

} // namespace
} // namespace Tina::AssetFormat
