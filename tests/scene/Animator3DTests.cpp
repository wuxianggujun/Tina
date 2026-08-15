#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/scene/Animator3D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/World.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <memory_resource>
#include <vector>

namespace Tina::Scene {
namespace {

class CountingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return m_allocationCount; }
    void rejectAllocations(bool reject) noexcept { m_rejectAllocations = reject; }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (m_rejectAllocations) {
            throw std::bad_alloc{};
        }
        ++m_allocationCount;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    bool m_rejectAllocations = false;
};

[[nodiscard]] std::vector<std::byte> makeMeshPayload()
{
    const std::array<AssetFormat::SkinnedMeshJointDesc, 2> joints{
        AssetFormat::SkinnedMeshJointDesc{
            .parentJoint = AssetFormat::SkinnedMeshWire::JointIndexNone,
            .bindTranslation = {1.0F, 0.0F, 0.0F},
        },
        AssetFormat::SkinnedMeshJointDesc{
            .parentJoint = 0,
            .bindTranslation = {0.0F, 2.0F, 0.0F},
        },
    };
    const std::array<float, 32> inverseBind{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    const std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{
        AssetFormat::StaticMeshSubmeshDesc{.indexCount = 3},
    };
    const std::array<float, 3 * AssetFormat::SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1,
    };
    const std::array<u16, 12> jointIndices{};
    const std::array<u16, 12> jointWeights{
        65535, 0, 0, 0,
        65535, 0, 0, 0,
        65535, 0, 0, 0,
    };
    const std::array<u16, 3> indices{0, 1, 2};
    auto payload = AssetFormat::writeSkinnedMeshPayloadBytes(
        AssetFormat::SkinnedMeshPayloadDesc{
            .boundsRadius = 1.0F,
            .joints = joints,
            .inverseBindMatrices = inverseBind,
            .submeshes = submeshes,
            .vertices = vertices,
            .jointIndices = jointIndices,
            .jointWeights = jointWeights,
            .indices = indices,
        });
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    return payload ? std::move(*payload) : std::vector<std::byte>{};
}

[[nodiscard]] std::vector<std::byte> makeClipPayload(
    AssetFormat::AnimationClip3DPlaybackMode mode,
    u16 jointCount = 2)
{
    const std::array<float, 2> times{0.0F, 1.0F};
    const std::array<float, 6> translations{1, 0, 0, 3, 0, 0};
    // The second key is the negated representation of a +90 degree Z rotation.
    // Shortest-path SLERP must flip it before interpolation.
    constexpr float HalfSqrtTwo = 0.70710677F;
    const std::array<float, 8> rotations{
        0, 0, 0, 1,
        0, 0, -HalfSqrtTwo, -HalfSqrtTwo,
    };
    const std::array<float, 6> scales{1, 1, 1, 2, 2, 2};
    const std::array tracks{
        AssetFormat::AnimationTrackDesc{
            .jointIndex = 0,
            .channel = AssetFormat::AnimationChannel::Translation,
            .interpolation = AssetFormat::AnimationInterpolation::Linear,
            .times = times,
            .values = translations,
        },
        AssetFormat::AnimationTrackDesc{
            .jointIndex = 1,
            .channel = AssetFormat::AnimationChannel::Rotation,
            .interpolation = AssetFormat::AnimationInterpolation::Linear,
            .times = times,
            .values = rotations,
        },
        AssetFormat::AnimationTrackDesc{
            .jointIndex = 1,
            .channel = AssetFormat::AnimationChannel::Scale,
            .interpolation = AssetFormat::AnimationInterpolation::Step,
            .times = times,
            .values = scales,
        },
    };
    auto payload = AssetFormat::writeAnimationClip3DPayloadBytes(
        AssetFormat::AnimationClip3DPayloadDesc{
            .playbackMode = mode,
            .jointCount = jointCount,
            .durationSeconds = 1.0F,
            .tracks = tracks,
        });
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    return payload ? std::move(*payload) : std::vector<std::byte>{};
}

struct ParsedFixture final {
    std::vector<std::byte> meshBytes = makeMeshPayload();
    std::vector<std::byte> clipBytes = makeClipPayload(
        AssetFormat::AnimationClip3DPlaybackMode::Once);
    AssetFormat::SkinnedMeshPayloadView mesh{};
    AssetFormat::AnimationClip3DPayloadView clip{};

    ParsedFixture()
    {
        auto parsedMesh = AssetFormat::parseSkinnedMeshPayload(meshBytes);
        auto parsedClip = AssetFormat::parseAnimationClip3DPayload(clipBytes);
        EXPECT_TRUE(parsedMesh.has_value()) << (parsedMesh ? "" : parsedMesh.error().message);
        EXPECT_TRUE(parsedClip.has_value()) << (parsedClip ? "" : parsedClip.error().message);
        if (parsedMesh) {
            mesh = *parsedMesh;
        }
        if (parsedClip) {
            clip = *parsedClip;
        }
    }
};

TEST(Animator3DTests, EvaluatesLinearStepSlerpAndHierarchyIntoCpuPose)
{
    ParsedFixture fixture;
    auto animator = Animator3D::Create(fixture.mesh, fixture.clip);
    ASSERT_TRUE(animator.has_value()) << (animator ? "" : animator.error().message);
    ASSERT_EQ(animator->localPose().size(), 2U);
    EXPECT_FLOAT_EQ(animator->localPose()[0].position.x, 1.0F);
    EXPECT_FLOAT_EQ(animator->skinningMatrices()[12], 1.0F);
    EXPECT_FLOAT_EQ(animator->skinningMatrices()[16U + 12U], 1.0F);
    EXPECT_FLOAT_EQ(animator->skinningMatrices()[16U + 13U], 2.0F);

    const auto update = animator->update(Core::Duration{0.5});
    ASSERT_TRUE(update.has_value()) << (update ? "" : update.error().message);
    EXPECT_TRUE(update->poseChanged);
    EXPECT_FLOAT_EQ(animator->localPose()[0].position.x, 2.0F);
    EXPECT_FLOAT_EQ(animator->localPose()[1].scale.x, 1.0F);
    EXPECT_NEAR(animator->localPose()[1].rotation.z, 0.38268343F, 1.0e-5F);
    EXPECT_NEAR(animator->localPose()[1].rotation.w, 0.92387953F, 1.0e-5F);
    EXPECT_FLOAT_EQ(animator->skinningMatrices()[12], 2.0F);
    EXPECT_FLOAT_EQ(animator->skinningMatrices()[16U + 12U], 2.0F);
    EXPECT_FLOAT_EQ(animator->skinningMatrices()[16U + 13U], 2.0F);

    const auto completed = animator->update(Core::Duration{0.5});
    ASSERT_TRUE(completed.has_value());
    EXPECT_TRUE(completed->completedThisUpdate);
    EXPECT_FALSE(animator->isPlaying());
    EXPECT_TRUE(animator->isCompleted());
    EXPECT_FLOAT_EQ(animator->localPose()[1].scale.x, 2.0F);
}

TEST(Animator3DTests, ImplementsOnceLoopPingPongPauseStopAndSpeed)
{
    ParsedFixture fixture;
    auto animator = Animator3D::Create(fixture.mesh, fixture.clip);
    ASSERT_TRUE(animator.has_value());
    ASSERT_TRUE(animator->setPlaybackSpeed(2.0F));
    ASSERT_TRUE(animator->update(Core::Duration{0.75}));
    EXPECT_FLOAT_EQ(animator->timeSeconds(), 1.0F);
    EXPECT_TRUE(animator->isCompleted());

    animator->play();
    EXPECT_TRUE(animator->isPlaying());
    EXPECT_FLOAT_EQ(animator->timeSeconds(), 0.0F);
    animator->pause();
    ASSERT_TRUE(animator->update(Core::Duration{0.25}));
    EXPECT_FLOAT_EQ(animator->timeSeconds(), 0.0F);

    auto loopBytes = makeClipPayload(AssetFormat::AnimationClip3DPlaybackMode::Loop);
    auto loop = AssetFormat::parseAnimationClip3DPayload(loopBytes);
    ASSERT_TRUE(loop.has_value());
    ASSERT_TRUE(animator->setClip(*loop));
    ASSERT_TRUE(animator->setPlaybackSpeed(1.0F));
    ASSERT_TRUE(animator->update(Core::Duration{1.25}));
    EXPECT_FLOAT_EQ(animator->timeSeconds(), 0.25F);

    auto pingPongBytes = makeClipPayload(AssetFormat::AnimationClip3DPlaybackMode::PingPong);
    auto pingPong = AssetFormat::parseAnimationClip3DPayload(pingPongBytes);
    ASSERT_TRUE(pingPong.has_value());
    ASSERT_TRUE(animator->setClip(*pingPong));
    ASSERT_TRUE(animator->update(Core::Duration{1.25}));
    EXPECT_FLOAT_EQ(animator->timeSeconds(), 0.75F);
    animator->stop();
    EXPECT_FALSE(animator->isPlaying());
    EXPECT_FLOAT_EQ(animator->timeSeconds(), 0.0F);
}

TEST(Animator3DTests, RejectsInvalidInputAndPreservesClipOnFailedReplacement)
{
    ParsedFixture fixture;
    auto animator = Animator3D::Create(fixture.mesh, fixture.clip);
    ASSERT_TRUE(animator.has_value());
    ASSERT_TRUE(animator->update(Core::Duration{0.5}));
    const float timeBefore = animator->timeSeconds();
    const LocalTransform poseBefore = animator->localPose()[0];

    AssetFormat::AnimationClip3DPayloadView mismatched = fixture.clip;
    mismatched.jointCount = 1;
    const Core::Status replacement = animator->setClip(mismatched);
    ASSERT_FALSE(replacement);
    EXPECT_EQ(replacement.error().code, SceneErrorCode::InvalidAnimation);
    EXPECT_FLOAT_EQ(animator->timeSeconds(), timeBefore);
    EXPECT_EQ(animator->localPose()[0], poseBefore);

    EXPECT_FALSE(animator->setPlaybackSpeed(0.0F));
    EXPECT_FALSE(animator->setPlaybackSpeed(
        std::numeric_limits<float>::infinity()));
    const auto negative = animator->update(Core::Duration{-0.1});
    ASSERT_FALSE(negative);
    EXPECT_FLOAT_EQ(animator->timeSeconds(), timeBefore);
}

TEST(Animator3DTests, UpdateIsAllocationFreeAndClipAllocationFailureIsAtomic)
{
    ParsedFixture fixture;
    CountingMemoryResource memory;
    auto animator = Animator3D::Create(fixture.mesh, fixture.clip, memory);
    ASSERT_TRUE(animator.has_value()) << (animator ? "" : animator.error().message);
    const usize allocationsAfterCreate = memory.allocationCount();
    memory.rejectAllocations(true);
    ASSERT_TRUE(animator->update(Core::Duration{0.25}));
    EXPECT_EQ(memory.allocationCount(), allocationsAfterCreate);
    const float timeBefore = animator->timeSeconds();
    const Core::Status replacement = animator->setClip(fixture.clip);
    ASSERT_FALSE(replacement);
    EXPECT_EQ(replacement.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_FLOAT_EQ(animator->timeSeconds(), timeBefore);
}

TEST(Animator3DTests, WorldStoresMutuallyExclusiveStaticAndSkinnedMeshComponentsAtomically)
{
    auto world = World::Create(WorldConfig{4});
    ASSERT_TRUE(world.has_value()) << (world ? "" : world.error().message);
    const EntityId entity = world->createEntity().value();
    MeshRenderer3D staticComponent{
        .submeshIndex = 1,
        .localBounds = {.radius = 1.0F},
    };
    ASSERT_TRUE(world->setMeshRenderer3D(entity, staticComponent));
    ASSERT_NE(world->meshRenderer3D(entity), nullptr);

    SkinnedMeshRenderer3D component{
        .submeshIndex = 3,
        .localBounds = {.centerX = 1.0F, .radius = 2.0F},
        .visible = false,
    };
    ASSERT_TRUE(world->setSkinnedMeshRenderer3D(entity, component));
    EXPECT_EQ(world->meshRenderer3D(entity), nullptr);
    const SkinnedMeshRenderer3D* stored = world->skinnedMeshRenderer3D(entity);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->mesh, component.mesh);
    EXPECT_EQ(stored->material, component.material);
    EXPECT_EQ(stored->submeshIndex, component.submeshIndex);
    EXPECT_FLOAT_EQ(stored->localBounds.centerX, component.localBounds.centerX);
    EXPECT_FLOAT_EQ(stored->localBounds.radius, component.localBounds.radius);
    EXPECT_EQ(stored->visible, component.visible);

    component.localBounds.radius = 0.0F;
    EXPECT_EQ(
        world->setSkinnedMeshRenderer3D(entity, component).error().code,
        SceneErrorCode::InvalidComponent);
    EXPECT_FLOAT_EQ(world->skinnedMeshRenderer3D(entity)->localBounds.radius, 2.0F);
    EXPECT_EQ(world->meshRenderer3D(entity), nullptr);

    staticComponent.localBounds.radius = 0.0F;
    EXPECT_EQ(
        world->setMeshRenderer3D(entity, staticComponent).error().code,
        SceneErrorCode::InvalidComponent);
    EXPECT_NE(world->skinnedMeshRenderer3D(entity), nullptr);
    EXPECT_EQ(world->meshRenderer3D(entity), nullptr);

    staticComponent.localBounds.radius = 1.0F;
    ASSERT_TRUE(world->setMeshRenderer3D(entity, staticComponent));
    EXPECT_NE(world->meshRenderer3D(entity), nullptr);
    EXPECT_EQ(world->skinnedMeshRenderer3D(entity), nullptr);

    ASSERT_TRUE(world->clearSkinnedMeshRenderer3D(entity));
    EXPECT_EQ(world->skinnedMeshRenderer3D(entity), nullptr);
}

} // namespace
} // namespace Tina::Scene
