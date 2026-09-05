#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory_resource>
#include <new>
#include <numbers>
#include <span>

namespace Tina::Render {
namespace {

class CountingResource final : public std::pmr::memory_resource {
  public:
    usize allocations = 0;
    usize deallocations = 0;
    bool rejectAllocations = false;
    usize successfulAllocationLimit = (std::numeric_limits<usize>::max)();

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (rejectAllocations || allocations >= successfulAllocationLimit)
        {
            throw std::bad_alloc{};
        }
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++deallocations;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

void countFrameResourceRelease(void* userData) noexcept
{
    ++(*static_cast<u32*>(userData));
}

class FrameResourceScope final {
  public:
    FrameResourceScope()
    {
        EXPECT_TRUE(packet_.beginFrame(0));
    }

    [[nodiscard]] FrameResourceRef resource(FrameResourceKind kind, u64 deviceBindingKey)
    {
        FramePin pin{FramePinKind::Custom, deviceBindingKey, &releaseCount_, &countFrameResourceRelease};
        auto result = packet_.intern(
            FrameResourceDescriptor{
                .kind = kind,
                .deviceBindingKey = deviceBindingKey,
            },
            std::move(pin));
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? *result : FrameResourceRef{};
    }

    [[nodiscard]] FrameResourceRef texture(u64 deviceBindingKey)
    {
        return resource(FrameResourceKind::Texture2D, deviceBindingKey);
    }

    [[nodiscard]] FrameResourceRef mesh(u64 deviceBindingKey)
    {
        return resource(FrameResourceKind::Mesh3DGeometry, deviceBindingKey);
    }

    [[nodiscard]] FrameResourceRef skinnedMesh(u64 deviceBindingKey)
    {
        return resource(FrameResourceKind::SkinnedMesh3DGeometry, deviceBindingKey);
    }

    [[nodiscard]] FrameResourceRef material(u64 deviceBindingKey)
    {
        return resource(FrameResourceKind::Mesh3DMaterial, deviceBindingKey);
    }

    [[nodiscard]] u64 bindingKey(
        FrameResourceRef ref,
        FrameResourceKind kind = FrameResourceKind::Texture2D) const noexcept
    {
        const FrameResourceDescriptor* descriptor =
            packet_.resourceTableView().resolve(ref, kind);
        return descriptor != nullptr ? descriptor->deviceBindingKey : 0;
    }

  private:
    u32 releaseCount_ = 0;
    RenderFramePacket packet_{};
};

[[nodiscard]] RenderSceneBuilder makeBuilder(u32 capacity = 16)
{
    auto result = RenderSceneBuilder::Create(RenderSceneCapacity{capacity});
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return std::move(*result);
}

[[nodiscard]] RenderCamera2DInput camera(float centerX = 0.0F, float centerY = 0.0F)
{
    return RenderCamera2DInput{
        .stableCameraKey = 7,
        .centerX = centerX,
        .centerY = centerY,
        .rotationRadians = 0.0F,
        .worldWidth = 10.0F,
        .worldHeight = 10.0F,
        .actualPixelsPerMeter = 10.0F,
    };
}

[[nodiscard]] RenderSprite2DInput sprite(FrameResourceScope& resources, u64 bindingKey,
                                         u64 stableKey, float x, float y,
                                         i16 layer = 0, i32 order = 0)
{
    return RenderSprite2DInput{
        .texture = resources.texture(bindingKey),
        .stableEntityKey = stableKey,
        .centerX = x,
        .centerY = y,
        .sortingLayer = layer,
        .orderInLayer = order,
    };
}

[[nodiscard]] RenderSceneFrameParameters perspectiveFrame(float aspectRatio = 16.0F / 9.0F)
{
    return RenderSceneFrameParameters{.primarySurfaceAspectRatio = aspectRatio};
}

[[nodiscard]] RenderPerspectiveCameraInput perspectiveCamera(float positionZ = 6.0F)
{
    return RenderPerspectiveCameraInput{
        .stableCameraKey = 17,
        .worldPose = RenderPose3DInput{.positionZ = positionZ},
        .verticalFovDegrees = 60.0F,
        .nearPlaneMeters = 0.1F,
        .farPlaneMeters = 100.0F,
    };
}

[[nodiscard]] RenderMesh3DInput mesh3D(FrameResourceScope& resources, u32 meshKey, u32 materialKey,
                                       u64 stableEntityKey,
                                       float x, float y, float z)
{
    return RenderMesh3DInput{
        .mesh = resources.mesh(meshKey),
        .material = resources.material(materialKey),
        .stableEntityKey = stableEntityKey,
        .worldTransform = RenderTransform3DInput{
            .pose = RenderPose3DInput{
                .positionX = x,
                .positionY = y,
                .positionZ = z,
            },
        },
        .localBounds = RenderBoundingSphereInput{.radius = 0.5F},
    };
}

[[nodiscard]] RenderSkinnedMesh3DInput skinnedMesh3D(
    FrameResourceScope& resources,
    u32 meshKey,
    u32 materialKey,
    u64 stableEntityKey,
    float x,
    float y,
    float z,
    std::span<const float> palette)
{
    return RenderSkinnedMesh3DInput{
        .mesh = resources.skinnedMesh(meshKey),
        .material = resources.material(materialKey),
        .stableEntityKey = stableEntityKey,
        .worldTransform = RenderTransform3DInput{
            .pose = RenderPose3DInput{
                .positionX = x,
                .positionY = y,
                .positionZ = z,
            },
        },
        .localBounds = RenderBoundingSphereInput{.radius = 0.5F},
        .paletteColumnMajorJointMatrices = palette,
    };
}

TEST(RenderSceneBuilderTest, RejectsInvalidCapacityBeforeAllocating)
{
    CountingResource resource;
    const auto result = RenderSceneBuilder::Create(RenderSceneCapacity{0}, resource);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidRenderSceneCapacity);
    EXPECT_EQ(resource.allocations, 0U);
}

TEST(RenderSceneBuilderTest, MapsFixedStorageAllocationFailure)
{
    CountingResource resource;
    resource.rejectAllocations = true;
    const auto result = RenderSceneBuilder::Create(RenderSceneCapacity{4}, resource);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, RenderErrorCode::RenderSceneStorageAllocationFailed);
}

TEST(RenderSceneBuilderTest, ReleasesPartialFixedStorageWhenLaterAllocationFails)
{
    for (usize successfulAllocationCount = 1; successfulAllocationCount < 5;
         ++successfulAllocationCount)
    {
        CountingResource resource;
        resource.successfulAllocationLimit = successfulAllocationCount;
        auto allocationFailure = RenderSceneBuilder::Create(RenderSceneCapacity{}, resource);
        ASSERT_FALSE(allocationFailure);
        EXPECT_EQ(allocationFailure.error().code,
                  RenderErrorCode::RenderSceneStorageAllocationFailed);
        EXPECT_EQ(resource.allocations, successfulAllocationCount);
        EXPECT_EQ(resource.deallocations, successfulAllocationCount);
    }
}

TEST(RenderSceneBuilderTest, RejectsInvalidMeshAndBatchCapacitiesBeforeAllocating)
{
    CountingResource resource;
    RenderSceneCapacity capacity{};
    capacity.mesh3DItemCapacity = 0;
    auto invalidItems = RenderSceneBuilder::Create(capacity, resource);
    ASSERT_FALSE(invalidItems);
    EXPECT_EQ(invalidItems.error().code, RenderErrorCode::InvalidRenderSceneCapacity);
    EXPECT_EQ(resource.allocations, 0U);

    capacity = {};
    capacity.mesh3DBatchCapacity = RenderSceneCapacity::MaximumMesh3DBatchCapacity + 1U;
    auto invalidBatches = RenderSceneBuilder::Create(capacity, resource);
    ASSERT_FALSE(invalidBatches);
    EXPECT_EQ(invalidBatches.error().code, RenderErrorCode::InvalidRenderSceneCapacity);
    EXPECT_EQ(resource.allocations, 0U);
}

TEST(RenderSceneBuilderTest, SortsCullsAndSnapsWithoutChangingInput)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    RenderCamera2DInput cameraInput = camera(0.13F, -0.07F);
    cameraInput.pixelSnap = RenderPixelSnapPolicy::CameraAndSprites;
    ASSERT_TRUE(builder.writer().setCamera2D(cameraInput));

    RenderSprite2DInput farAway = sprite(resources, 99, 99, 100.0F, 100.0F);
    ASSERT_TRUE(builder.writer().addSprite2D(farAway));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 2, 20, 0.14F, 0.04F, 2, 0)));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 1, 10, 0.12F, 0.02F, 1, 4)));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_TRUE(committed->camera2D().has_value());
    EXPECT_FLOAT_EQ(committed->camera2D()->centerX, 0.1F);
    ASSERT_EQ(committed->sprites2D().size(), 2U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[0].texture), 1U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[1].texture), 2U);
    EXPECT_EQ(committed->statistics().culledSpriteCount, 1U);
    EXPECT_EQ(committed->statistics().visibleSpriteCount, 2U);
    EXPECT_FLOAT_EQ(committed->sprites2D()[0].centerX, 0.1F);
}

TEST(RenderSceneBuilderTest, ConservativelyCullsRotatedSpritesAtTheCameraBoundary)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    RenderCamera2DInput cameraInput = camera();
    cameraInput.worldWidth = 4.0F;
    cameraInput.worldHeight = 4.0F;
    ASSERT_TRUE(builder.writer().setCamera2D(cameraInput));

    RenderSprite2DInput intersectsAfterRotation = sprite(resources, 1, 1, 3.05F, 0.0F);
    intersectsAfterRotation.widthMeters = 2.0F;
    intersectsAfterRotation.heightMeters = 1.0F;
    intersectsAfterRotation.rotationRadians = std::numbers::pi_v<float> * 0.25F;
    ASSERT_TRUE(builder.writer().addSprite2D(intersectsAfterRotation));

    RenderSprite2DInput outsideAfterRotation = intersectsAfterRotation;
    outsideAfterRotation.texture = resources.texture(2);
    outsideAfterRotation.stableEntityKey = 2;
    outsideAfterRotation.centerX = 3.2F;
    ASSERT_TRUE(builder.writer().addSprite2D(outsideAfterRotation));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_EQ(committed->sprites2D().size(), 1U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D().front().texture), 1U);
    EXPECT_EQ(committed->statistics().culledSpriteCount, 1U);
}

TEST(RenderSceneBuilderTest, UsesEntityThenInsertionOrderForEqualLayers)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 3, 30, 0.0F, 0.0F, 2, 7)));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 2, 20, 0.0F, 0.0F, 2, 7)));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 1, 20, 0.0F, 0.0F, 2, 7)));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_EQ(committed->sprites2D().size(), 3U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[0].texture), 2U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[1].texture), 1U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[2].texture), 3U);
    EXPECT_LT(committed->sprites2D()[0].insertionOrder, committed->sprites2D()[1].insertionOrder);
}

TEST(RenderSceneBuilderTest, CopiesOptionalNormalTextureWithoutChangingSpriteSortOrder)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));

    RenderSprite2DInput last = sprite(resources, 30, 30, 0.0F, 0.0F, 2, 7);
    last.normalTexture = resources.texture(300);
    ASSERT_TRUE(builder.writer().addSprite2D(last));

    RenderSprite2DInput first = sprite(resources, 10, 10, 0.0F, 0.0F, 2, 7);
    first.normalTexture = resources.texture(100);
    ASSERT_TRUE(builder.writer().addSprite2D(first));
    first.normalTexture = resources.texture(999);

    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 20, 20, 0.0F, 0.0F, 2, 7)));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_EQ(committed->sprites2D().size(), 3U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[0].texture), 10U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[0].normalTexture), 100U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[1].texture), 20U);
    EXPECT_FALSE(committed->sprites2D()[1].normalTexture.hasValue());
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[2].texture), 30U);
    EXPECT_EQ(resources.bindingKey(committed->sprites2D()[2].normalTexture), 300U);
}

TEST(RenderSceneBuilderTest, RequiresOneCameraForWorldSpritesAndRejectsDuplicates)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 1, 1, 0.0F, 0.0F)));
    auto missingCamera = builder.commit();
    ASSERT_FALSE(missingCamera);
    EXPECT_EQ(missingCamera.error().code, RenderErrorCode::RenderSceneMissingCamera);

    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    const auto duplicate = builder.writer().setCamera2D(camera());
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, RenderErrorCode::RenderSceneCameraConflict);
    builder.rollback();
}

TEST(RenderSceneBuilderTest, RejectsInvalidSpriteTextureRefAtomically)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));

    const auto invalid = builder.writer().addSprite2D(RenderSprite2DInput{
        .stableEntityKey = 1,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, RenderErrorCode::InvalidRenderSceneInput);
    auto commit = builder.commit();
    ASSERT_FALSE(commit.has_value());
    EXPECT_EQ(commit.error().code, RenderErrorCode::InvalidRenderSceneInput);
    EXPECT_TRUE(builder.publishedView().empty());
}

TEST(RenderSceneBuilderTest, PrunesInvisibleAndTransparentSpritesAndReportsCapacity)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder(1);
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));

    auto invisible = sprite(resources, 1, 1, 0.0F, 0.0F);
    invisible.visible = false;
    ASSERT_TRUE(builder.writer().addSprite2D(invisible));
    auto transparent = sprite(resources, 2, 2, 0.0F, 0.0F);
    transparent.alpha = 0;
    ASSERT_TRUE(builder.writer().addSprite2D(transparent));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 3, 3, 0.0F, 0.0F)));
    const auto overflow = builder.writer().addSprite2D(sprite(resources, 4, 4, 0.0F, 0.0F));
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
    const auto commit = builder.commit();
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
}

TEST(RenderSceneBuilderTest, RejectsInvalidIdentityAndDerivedGeometryAtomically)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());

    RenderCamera2DInput invalidCamera = camera();
    invalidCamera.stableCameraKey = 0;
    const auto cameraFailure = builder.writer().setCamera2D(invalidCamera);
    ASSERT_FALSE(cameraFailure);
    EXPECT_EQ(cameraFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);

    const auto stickyFailure = builder.writer().setCamera2D(camera());
    ASSERT_FALSE(stickyFailure);
    EXPECT_EQ(stickyFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);
    const auto failedCommit = builder.commit();
    ASSERT_FALSE(failedCommit);
    EXPECT_EQ(failedCommit.error().code, RenderErrorCode::InvalidRenderSceneInput);
    EXPECT_TRUE(builder.publishedView().empty());

    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    RenderSprite2DInput overflow = sprite(resources, 1, 1, 0.0F, 0.0F);
    overflow.widthMeters = (std::numeric_limits<float>::max)();
    overflow.scaleX = 2.0F;
    const auto spriteFailure = builder.writer().addSprite2D(overflow);
    ASSERT_FALSE(spriteFailure);
    EXPECT_EQ(spriteFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);
    const auto secondFailedCommit = builder.commit();
    ASSERT_FALSE(secondFailedCommit);
    EXPECT_EQ(secondFailedCommit.error().code, RenderErrorCode::InvalidRenderSceneInput);

    const RenderSceneBuilderStatistics statistics = builder.statistics();
    EXPECT_EQ(statistics.invalidInputFailureCount, 2U);
    EXPECT_EQ(statistics.rolledBackBuildCount, 2U);
}

TEST(RenderSceneBuilderTest, ReplacementBuildInvalidatesOldPublicationAndFailurePublishesNothing)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder(1);
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 1, 1, 0.0F, 0.0F)));
    auto first = builder.commit();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->sprites2D().size(), 1U);

    ASSERT_TRUE(builder.beginFrame());
    EXPECT_TRUE(builder.publishedView().empty());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 2, 2, 0.0F, 0.0F)));
    ASSERT_FALSE(builder.writer().addSprite2D(sprite(resources, 3, 3, 0.0F, 0.0F)));
    ASSERT_FALSE(builder.commit());
    EXPECT_TRUE(builder.publishedView().empty());
}

TEST(RenderSceneBuilderTest, Sprite2DLightingCopiesIntoTheCommittedFrameSnapshot)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    std::array lights{
        Sprite2DPointLight{
            .positionX = 1.0F,
            .positionY = 2.0F,
            .radiusMeters = 3.0F,
            .sourceRadiusMeters = 0.5F,
            .colorR = 0.5F,
            .colorG = 0.6F,
            .colorB = 0.7F,
        },
        Sprite2DPointLight{
            .positionX = -4.0F,
            .positionY = 5.0F,
            .radiusMeters = 6.0F,
            .colorR = 0.2F,
            .colorG = 0.3F,
            .colorB = 0.4F,
        },
    };
    std::array shadowSegments{
        Sprite2DShadowSegment{.startX = -1.0F, .startY = 2.0F, .endX = 3.0F, .endY = 4.0F},
        Sprite2DShadowSegment{.startX = 5.0F, .startY = 6.0F, .endX = 7.0F, .endY = 8.0F},
    };
    ASSERT_TRUE(builder.writer().setSprite2DLighting({
        .pointLights = lights,
        .shadowSegments = shadowSegments,
        .ambientScale = 0.25F,
    }));
    lights[0].colorR = 9.0F;
    shadowSegments[0].startX = 9.0F;

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value());
    ASSERT_TRUE(committed->sprite2DLighting().has_value());
    const RenderSprite2DLighting& lighting = *committed->sprite2DLighting();
    ASSERT_EQ(lighting.pointLights().size(), 2U);
    EXPECT_FLOAT_EQ(lighting.pointLights()[0].colorR, 0.5F);
    EXPECT_FLOAT_EQ(lighting.pointLights()[0].sourceRadiusMeters, 0.5F);
    EXPECT_FLOAT_EQ(lighting.pointLights()[1].positionX, -4.0F);
    ASSERT_EQ(lighting.shadowSegments().size(), 2U);
    EXPECT_FLOAT_EQ(lighting.shadowSegments()[0].startX, -1.0F);
    EXPECT_FLOAT_EQ(lighting.shadowSegments()[1].endY, 8.0F);
    EXPECT_FLOAT_EQ(lighting.ambientScale(), 0.25F);
    EXPECT_TRUE(committed->statistics().sprite2DLightingConfigured);
    EXPECT_EQ(committed->statistics().pointLight2DCount, 2U);
    EXPECT_EQ(committed->statistics().shadowOccluder2DCount, 2U);
    EXPECT_FALSE(committed->empty());
}

TEST(RenderSceneBuilderTest, InvalidOrDuplicateSprite2DLightingFailsTheBuildAtomically)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    const std::array invalidLights{
        Sprite2DPointLight{.radiusMeters = 0.0F},
    };
    auto invalid = builder.writer().setSprite2DLighting({.pointLights = invalidLights});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, RenderErrorCode::InvalidSprite2DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array invalidSourceRadius{
        Sprite2DPointLight{.radiusMeters = 1.0F, .sourceRadiusMeters = 1.1F},
    };
    auto invalidSource =
        builder.writer().setSprite2DLighting({.pointLights = invalidSourceRadius});
    ASSERT_FALSE(invalidSource);
    EXPECT_EQ(invalidSource.error().code, RenderErrorCode::InvalidSprite2DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array nonFiniteSourceRadius{
        Sprite2DPointLight{
            .radiusMeters = 1.0F,
            .sourceRadiusMeters = std::numeric_limits<float>::quiet_NaN(),
        },
    };
    auto nonFiniteSource =
        builder.writer().setSprite2DLighting({.pointLights = nonFiniteSourceRadius});
    ASSERT_FALSE(nonFiniteSource);
    EXPECT_EQ(nonFiniteSource.error().code, RenderErrorCode::InvalidSprite2DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array invalidSegments{
        Sprite2DShadowSegment{.startX = 1.0F, .startY = 2.0F, .endX = 1.0F, .endY = 2.0F},
    };
    auto invalidSegment =
        builder.writer().setSprite2DLighting({.shadowSegments = invalidSegments});
    ASSERT_FALSE(invalidSegment);
    EXPECT_EQ(invalidSegment.error().code, RenderErrorCode::InvalidSprite2DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array nonFiniteSegments{
        Sprite2DShadowSegment{
            .startX = std::numeric_limits<float>::quiet_NaN(),
            .startY = 0.0F,
            .endX = 1.0F,
            .endY = 0.0F,
        },
    };
    auto nonFiniteSegment =
        builder.writer().setSprite2DLighting({.shadowSegments = nonFiniteSegments});
    ASSERT_FALSE(nonFiniteSegment);
    EXPECT_EQ(nonFiniteSegment.error().code, RenderErrorCode::InvalidSprite2DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array<Sprite2DShadowSegment,
                     Sprite2DLightingDesc::MaximumShadowSegmentCount + 1U>
        tooManySegments{};
    auto tooManyOccluders =
        builder.writer().setSprite2DLighting({.shadowSegments = tooManySegments});
    ASSERT_FALSE(tooManyOccluders);
    EXPECT_EQ(tooManyOccluders.error().code, RenderErrorCode::InvalidSprite2DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array<Sprite2DPointLight, Sprite2DLightingDesc::MaximumPointLightCount + 1U>
        tooManyLights{};
    auto tooMany = builder.writer().setSprite2DLighting({.pointLights = tooManyLights});
    ASSERT_FALSE(tooMany);
    EXPECT_EQ(tooMany.error().code, RenderErrorCode::InvalidSprite2DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array validLights{Sprite2DPointLight{}};
    ASSERT_TRUE(builder.writer().setSprite2DLighting({.pointLights = validLights}));
    auto duplicate = builder.writer().setSprite2DLighting({.pointLights = validLights});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, RenderErrorCode::RenderSceneLightingConflict);
    EXPECT_FALSE(builder.commit().has_value());
}

TEST(RenderSceneBuilderTest, Mesh3DLightingCopiesIntoTheCommittedFrameSnapshot)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    std::array lights{
        Mesh3DDirectionalLight{
            .directionTowardLightX = 1.0F,
            .directionTowardLightY = 2.0F,
            .directionTowardLightZ = 3.0F,
            .colorR = 0.5F,
            .colorG = 0.6F,
            .colorB = 0.7F,
        },
        Mesh3DDirectionalLight{
            .directionTowardLightX = -1.0F,
            .colorR = 0.2F,
            .colorG = 0.3F,
            .colorB = 0.4F,
        },
    };
    std::array pointLights{
        Mesh3DPointLight{
            .positionX = 1.0F,
            .positionY = 2.0F,
            .positionZ = 3.0F,
            .influenceRadius = 5.0F,
            .colorR = 0.7F,
            .colorG = 0.6F,
            .colorB = 0.5F,
        },
        Mesh3DPointLight{
            .positionX = -4.0F,
            .influenceRadius = 2.0F,
            .colorR = 0.2F,
            .colorG = 0.3F,
            .colorB = 0.4F,
        },
    };
    std::array spotLights{
        Mesh3DSpotLight{
            .positionX = 7.0F,
            .positionY = 8.0F,
            .positionZ = 9.0F,
            .influenceRadius = 6.0F,
            .directionFromLightX = -1.0F,
            .directionFromLightY = 0.0F,
            .directionFromLightZ = 0.0F,
            .innerConeCosine = 0.9F,
            .outerConeCosine = 0.8F,
            .colorR = 0.4F,
            .colorG = 0.5F,
            .colorB = 0.6F,
        },
        Mesh3DSpotLight{
            .positionX = -7.0F,
            .influenceRadius = 3.0F,
            .directionFromLightY = -1.0F,
            .directionFromLightZ = 0.0F,
            .innerConeCosine = 0.95F,
            .outerConeCosine = 0.7F,
            .colorR = 0.1F,
            .colorG = 0.2F,
            .colorB = 0.3F,
        },
    };
    Mesh3DSpotLightShadow spotLightShadow{
        .spotLightIndex = 1U,
        .nearPlaneMeters = 0.1F,
        .depthBias = 0.002F,
        .normalBiasMeters = 0.03F,
    };
    Mesh3DPointLightShadow pointLightShadow{
        .pointLightIndex = 1U,
        .nearPlaneMeters = 0.1F,
        .depthBias = 0.003F,
        .normalBiasMeters = 0.04F,
    };
    ASSERT_TRUE(builder.writer().setMesh3DLighting({
        .directionalLights = lights,
        .pointLights = pointLights,
        .spotLights = spotLights,
        .pointLightShadow = pointLightShadow,
        .spotLightShadow = spotLightShadow,
        .ambientScale = 0.25F,
    }));
    lights[0].colorR = 9.0F;
    pointLights[0].positionX = 99.0F;
    pointLights[1].colorB = 99.0F;
    spotLights[0].positionX = 99.0F;
    spotLights[1].outerConeCosine = 99.0F;
    pointLightShadow.nearPlaneMeters = 99.0F;
    spotLightShadow.nearPlaneMeters = 99.0F;

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value());
    ASSERT_TRUE(committed->mesh3DLighting().has_value());
    const RenderMesh3DLighting& lighting = *committed->mesh3DLighting();
    ASSERT_EQ(lighting.directionalLights().size(), 2U);
    EXPECT_FLOAT_EQ(lighting.directionalLights()[0].colorR, 0.5F);
    EXPECT_FLOAT_EQ(lighting.directionalLights()[1].directionTowardLightX, -1.0F);
    ASSERT_EQ(lighting.pointLights().size(), 2U);
    EXPECT_FLOAT_EQ(lighting.pointLights()[0].positionX, 1.0F);
    EXPECT_FLOAT_EQ(lighting.pointLights()[0].influenceRadius, 5.0F);
    EXPECT_FLOAT_EQ(lighting.pointLights()[1].colorB, 0.4F);
    EXPECT_EQ(lighting.descriptor().pointLights.data(), lighting.pointLights().data());
    ASSERT_EQ(lighting.spotLights().size(), 2U);
    EXPECT_FLOAT_EQ(lighting.spotLights()[0].positionX, 7.0F);
    EXPECT_FLOAT_EQ(lighting.spotLights()[0].directionFromLightX, -1.0F);
    EXPECT_FLOAT_EQ(lighting.spotLights()[1].outerConeCosine, 0.7F);
    EXPECT_EQ(lighting.descriptor().spotLights.data(), lighting.spotLights().data());
    ASSERT_TRUE(lighting.pointLightShadow().has_value());
    EXPECT_EQ(lighting.pointLightShadow()->pointLightIndex, 1U);
    EXPECT_FLOAT_EQ(lighting.pointLightShadow()->nearPlaneMeters, 0.1F);
    EXPECT_FLOAT_EQ(lighting.pointLightShadow()->depthBias, 0.003F);
    EXPECT_FLOAT_EQ(lighting.pointLightShadow()->normalBiasMeters, 0.04F);
    EXPECT_EQ(lighting.descriptor().pointLightShadow, lighting.pointLightShadow());
    ASSERT_TRUE(lighting.spotLightShadow().has_value());
    EXPECT_EQ(lighting.spotLightShadow()->spotLightIndex, 1U);
    EXPECT_FLOAT_EQ(lighting.spotLightShadow()->nearPlaneMeters, 0.1F);
    EXPECT_FLOAT_EQ(lighting.spotLightShadow()->depthBias, 0.002F);
    EXPECT_FLOAT_EQ(lighting.spotLightShadow()->normalBiasMeters, 0.03F);
    EXPECT_EQ(lighting.descriptor().spotLightShadow, lighting.spotLightShadow());
    EXPECT_FLOAT_EQ(lighting.ambientScale(), 0.25F);
    EXPECT_TRUE(committed->statistics().mesh3DLightingConfigured);
    EXPECT_EQ(committed->statistics().directionalLightCount, 2U);
    EXPECT_EQ(committed->statistics().pointLight3DCount, 2U);
    EXPECT_EQ(committed->statistics().spotLight3DCount, 2U);
    EXPECT_FALSE(committed->empty());
}

// ADR 0042.
TEST(RenderSceneBuilderTest, AFrameThatNeverSetsAClearColorKeepsTheEngineDefault)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value());
    EXPECT_EQ(committed->clearColor(), DefaultSceneClearColor);
    EXPECT_FALSE(committed->statistics().clearColorConfigured);
    // The background is not scene content: a frame carrying only the default clear colour
    // is still empty, or every idle frame would report as non-empty.
    EXPECT_TRUE(committed->empty());
}

TEST(RenderSceneBuilderTest, ClearColorReachesTheCommittedFrameAndIsRecordedInStatistics)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    constexpr RenderLinearColor sky{.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 1.0F};
    ASSERT_TRUE(builder.writer().setClearColor(sky));
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value());
    EXPECT_EQ(committed->clearColor(), sky);
    EXPECT_TRUE(committed->statistics().clearColorConfigured);
}

TEST(RenderSceneBuilderTest, ASecondClearColorInOneFrameIsRejected)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setClearColor(RenderLinearColor{
        .red = 0.1F, .green = 0.1F, .blue = 0.1F, .alpha = 1.0F}));
    const Core::Status second = builder.writer().setClearColor(RenderLinearColor{
        .red = 0.2F, .green = 0.2F, .blue = 0.2F, .alpha = 1.0F});
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, RenderErrorCode::InvalidSceneClearColor);
}

TEST(RenderSceneBuilderTest, ClearColorRejectsNonFiniteNegativeAndOverRangeComponents)
{
    constexpr std::array rejected{
        RenderLinearColor{.red = std::numeric_limits<float>::quiet_NaN()},
        RenderLinearColor{.green = std::numeric_limits<float>::infinity()},
        RenderLinearColor{.blue = -0.001F},
        // Over-range is refused here rather than clamped in a backend, so the caller
        // learns that a background cannot carry radiance above full white.
        RenderLinearColor{.red = 1.001F},
        RenderLinearColor{.alpha = 2.0F},
    };
    for (const RenderLinearColor& color : rejected)
    {
        RenderSceneBuilder builder = makeBuilder();
        ASSERT_TRUE(builder.beginFrame());
        const Core::Status status = builder.writer().setClearColor(color);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, RenderErrorCode::InvalidSceneClearColor);
    }
}

TEST(RenderSceneBuilderTest, ANewFrameReturnsToTheDefaultClearColorRatherThanLatchingTheLastOne)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setClearColor(RenderLinearColor{
        .red = 0.9F, .green = 0.8F, .blue = 0.7F, .alpha = 1.0F}));
    ASSERT_TRUE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    auto second = builder.commit();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->clearColor(), DefaultSceneClearColor);
    EXPECT_FALSE(second->statistics().clearColorConfigured);
}

TEST(RenderSceneBuilderTest, PointLightShadowValidationRejectsInvalidCasterIndexNearPlaneAndBias)
{
    const std::array pointLights{Mesh3DPointLight{.influenceRadius = 5.0F}};
    const auto expectInvalid = [&pointLights](Mesh3DPointLightShadow shadow) {
        const Core::Status status = validateMesh3DLightingDesc(Mesh3DLightingDesc{
            .pointLights = pointLights,
            .pointLightShadow = shadow,
        });
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, RenderErrorCode::InvalidMesh3DLighting);
    };

    EXPECT_TRUE(validateMesh3DLightingDesc(Mesh3DLightingDesc{
        .pointLights = pointLights,
        .pointLightShadow = Mesh3DPointLightShadow{
            .nearPlaneMeters = 4.999F,
            .depthBias = Mesh3DPointLightShadow::MaximumDepthBias,
            .normalBiasMeters = Mesh3DPointLightShadow::MaximumNormalBiasMeters,
        },
    }));
    expectInvalid(Mesh3DPointLightShadow{.pointLightIndex = 1U});
    expectInvalid(Mesh3DPointLightShadow{.nearPlaneMeters = 0.0F});
    expectInvalid(Mesh3DPointLightShadow{.nearPlaneMeters = 5.0F});
    expectInvalid(Mesh3DPointLightShadow{
        .nearPlaneMeters = std::numeric_limits<float>::quiet_NaN(),
    });
    expectInvalid(Mesh3DPointLightShadow{.depthBias = -0.001F});
    expectInvalid(Mesh3DPointLightShadow{
        .depthBias = Mesh3DPointLightShadow::MaximumDepthBias + 0.001F,
    });
    expectInvalid(Mesh3DPointLightShadow{.normalBiasMeters = -0.001F});
    expectInvalid(Mesh3DPointLightShadow{
        .normalBiasMeters = Mesh3DPointLightShadow::MaximumNormalBiasMeters + 0.001F,
    });
}

TEST(RenderSceneBuilderTest, SpotLightShadowValidationRejectsInvalidCasterIndexNearPlaneAndBias)
{
    const std::array spotLights{Mesh3DSpotLight{.influenceRadius = 5.0F}};
    const auto expectInvalid = [&spotLights](Mesh3DSpotLightShadow shadow) {
        const Core::Status status = validateMesh3DLightingDesc(Mesh3DLightingDesc{
            .spotLights = spotLights,
            .spotLightShadow = shadow,
        });
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, RenderErrorCode::InvalidMesh3DLighting);
    };

    EXPECT_TRUE(validateMesh3DLightingDesc(Mesh3DLightingDesc{
        .spotLights = spotLights,
        .spotLightShadow = Mesh3DSpotLightShadow{
            .nearPlaneMeters = 4.999F,
            .depthBias = Mesh3DSpotLightShadow::MaximumDepthBias,
            .normalBiasMeters = Mesh3DSpotLightShadow::MaximumNormalBiasMeters,
        },
    }));

    const std::array wideShadowLights{Mesh3DSpotLight{
        .influenceRadius = 5.0F,
        .innerConeCosine = 0.5F,
        .outerConeCosine = 0.25F,
    }};
    EXPECT_TRUE(validateMesh3DLightingDesc(Mesh3DLightingDesc{
        .spotLights = wideShadowLights,
        .spotLightShadow = Mesh3DSpotLightShadow{},
    }));

    const std::array nonProjectableShadowLights{Mesh3DSpotLight{
        .influenceRadius = 5.0F,
        .innerConeCosine = 0.5F,
        .outerConeCosine = 0.0F,
    }};
    EXPECT_TRUE(validateMesh3DLightingDesc(Mesh3DLightingDesc{
        .spotLights = nonProjectableShadowLights,
    }));
    const Core::Status nonProjectableShadow = validateMesh3DLightingDesc(Mesh3DLightingDesc{
        .spotLights = nonProjectableShadowLights,
        .spotLightShadow = Mesh3DSpotLightShadow{},
    });
    ASSERT_FALSE(nonProjectableShadow);
    EXPECT_EQ(nonProjectableShadow.error().code, RenderErrorCode::InvalidMesh3DLighting);

    expectInvalid(Mesh3DSpotLightShadow{.spotLightIndex = 1U});
    expectInvalid(Mesh3DSpotLightShadow{.nearPlaneMeters = 0.0F});
    expectInvalid(Mesh3DSpotLightShadow{.nearPlaneMeters = 5.0F});
    expectInvalid(Mesh3DSpotLightShadow{
        .nearPlaneMeters = std::numeric_limits<float>::quiet_NaN(),
    });
    expectInvalid(Mesh3DSpotLightShadow{
        .nearPlaneMeters = std::numeric_limits<float>::infinity(),
    });
    expectInvalid(Mesh3DSpotLightShadow{.depthBias = -0.001F});
    expectInvalid(Mesh3DSpotLightShadow{
        .depthBias = Mesh3DSpotLightShadow::MaximumDepthBias + 0.001F,
    });
    expectInvalid(Mesh3DSpotLightShadow{
        .depthBias = std::numeric_limits<float>::quiet_NaN(),
    });
    expectInvalid(Mesh3DSpotLightShadow{.normalBiasMeters = -0.001F});
    expectInvalid(Mesh3DSpotLightShadow{
        .normalBiasMeters = Mesh3DSpotLightShadow::MaximumNormalBiasMeters + 0.001F,
    });
    expectInvalid(Mesh3DSpotLightShadow{
        .normalBiasMeters = std::numeric_limits<float>::infinity(),
    });
}

TEST(RenderSceneBuilderTest, InvalidOrDuplicateMesh3DLightingFailsTheBuildAtomically)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    const std::array invalidLights{
        Mesh3DDirectionalLight{
            .directionTowardLightX = 0.0F,
            .directionTowardLightY = 0.0F,
            .directionTowardLightZ = 0.0F,
        },
    };
    auto invalid = builder.writer().setMesh3DLighting({.directionalLights = invalidLights});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array invalidSpotLights{
        Mesh3DSpotLight{
            .directionFromLightX = 0.0F,
            .directionFromLightY = 0.0F,
            .directionFromLightZ = 0.0F,
        },
    };
    auto invalidSpot =
        builder.writer().setMesh3DLighting({.spotLights = invalidSpotLights});
    ASSERT_FALSE(invalidSpot);
    EXPECT_EQ(invalidSpot.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array invalidSpotCone{
        Mesh3DSpotLight{
            .innerConeCosine = 0.7F,
            .outerConeCosine = 0.8F,
        },
    };
    invalidSpot = builder.writer().setMesh3DLighting({.spotLights = invalidSpotCone});
    ASSERT_FALSE(invalidSpot);
    EXPECT_EQ(invalidSpot.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array<Mesh3DSpotLight, Mesh3DLightingDesc::MaximumSpotLightCount + 1U>
        tooManySpotLights{};
    auto tooManySpots =
        builder.writer().setMesh3DLighting({.spotLights = tooManySpotLights});
    ASSERT_FALSE(tooManySpots);
    EXPECT_EQ(tooManySpots.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array overflowingLights{
        Mesh3DDirectionalLight{
            .directionTowardLightX = std::numeric_limits<float>::max(),
            .directionTowardLightY = std::numeric_limits<float>::max(),
        },
    };
    auto overflowing =
        builder.writer().setMesh3DLighting({.directionalLights = overflowingLights});
    ASSERT_FALSE(overflowing);
    EXPECT_EQ(overflowing.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array invalidPointLights{
        Mesh3DPointLight{.influenceRadius = 0.0F},
    };
    auto invalidPoint =
        builder.writer().setMesh3DLighting({.pointLights = invalidPointLights});
    ASSERT_FALSE(invalidPoint);
    EXPECT_EQ(invalidPoint.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array negativePointColor{
        Mesh3DPointLight{.influenceRadius = 1.0F, .colorR = -0.1F},
    };
    auto invalidPointColor =
        builder.writer().setMesh3DLighting({.pointLights = negativePointColor});
    ASSERT_FALSE(invalidPointColor);
    EXPECT_EQ(invalidPointColor.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array<Mesh3DPointLight, Mesh3DLightingDesc::MaximumPointLightCount + 1U>
        tooManyPointLights{};
    auto tooManyPoints =
        builder.writer().setMesh3DLighting({.pointLights = tooManyPointLights});
    ASSERT_FALSE(tooManyPoints);
    EXPECT_EQ(tooManyPoints.error().code, RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame());
    const std::array validLights{Mesh3DDirectionalLight{}};
    ASSERT_TRUE(builder.writer().setMesh3DLighting({.directionalLights = validLights}));
    auto duplicate = builder.writer().setMesh3DLighting({.directionalLights = validLights});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, RenderErrorCode::RenderSceneLightingConflict);
    EXPECT_FALSE(builder.commit().has_value());
}

TEST(RenderSceneBuilderTest, PerspectiveCameraRequiresCurrentSurfaceAspectAndValidProjection)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    auto missingAspect = builder.writer().setPerspectiveCamera(perspectiveCamera());
    ASSERT_FALSE(missingAspect);
    EXPECT_EQ(missingAspect.error().code, RenderErrorCode::InvalidRenderSceneInput);
    ASSERT_FALSE(builder.commit());

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    auto invalidProjection = perspectiveCamera();
    invalidProjection.nearPlaneMeters = invalidProjection.farPlaneMeters;
    auto projectionFailure = builder.writer().setPerspectiveCamera(invalidProjection);
    ASSERT_FALSE(projectionFailure);
    EXPECT_EQ(projectionFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);
    ASSERT_FALSE(builder.commit());

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    auto invalidRotation = perspectiveCamera();
    invalidRotation.worldPose.rotationW = 0.0F;
    auto rotationFailure = builder.writer().setPerspectiveCamera(invalidRotation);
    ASSERT_FALSE(rotationFailure);
    EXPECT_EQ(rotationFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);
    ASSERT_FALSE(builder.commit());
}

TEST(RenderSceneBuilderTest, InvalidFrameAspectPreservesThePreviousPublicationWithoutOpeningABuild)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 1, 1, 0.0F, 0.0F)));
    ASSERT_TRUE(builder.commit());

    auto invalidFrame = builder.beginFrame(RenderSceneFrameParameters{
        .primarySurfaceAspectRatio = std::numeric_limits<float>::quiet_NaN(),
    });
    ASSERT_FALSE(invalidFrame);
    EXPECT_EQ(invalidFrame.error().code, RenderErrorCode::InvalidRenderSceneInput);
    EXPECT_EQ(builder.publishedView().sprites2D().size(), 1U);

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    EXPECT_TRUE(builder.publishedView().empty());
    builder.rollback();
}

TEST(RenderSceneBuilderTest, PerspectiveCameraUsesSurfaceAndNormalizedViewportAspect)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame(perspectiveFrame(2.0F)));
    auto cameraInput = perspectiveCamera();
    cameraInput.normalizedViewport.width = 0.5F;
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(cameraInput));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_TRUE(committed->perspectiveCamera().has_value());
    EXPECT_FLOAT_EQ(committed->perspectiveCamera()->aspectRatio, 1.0F);
    EXPECT_FLOAT_EQ(committed->perspectiveCamera()->forwardX, 0.0F);
    EXPECT_FLOAT_EQ(committed->perspectiveCamera()->forwardY, 0.0F);
    EXPECT_FLOAT_EQ(committed->perspectiveCamera()->forwardZ, -1.0F);
    EXPECT_FLOAT_EQ(committed->perspectiveCamera()->upY, 1.0F);
    EXPECT_EQ(committed->statistics().cameraCount, 1U);
    EXPECT_EQ(committed->statistics().perspectiveCameraCount, 1U);
}

TEST(RenderSceneBuilderTest, CullsSortsAndFinalizesStableMeshInstanceBatches)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));

    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 2, 1, 30, 0.0F, 0.0F, -2.0F)));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 2, 1, 20, 0.0F, 0.0F, 0.0F)));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 1, 2, 10, 1.0F, 0.0F, 0.0F)));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 9, 9, 40, 100.0F, 0.0F, 0.0F)));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 9, 9, 50, 0.0F, 0.0F, 10.0F)));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_EQ(committed->meshes3D().size(), 3U);
    ASSERT_EQ(committed->mesh3DBatches().size(), 2U);

    EXPECT_EQ(resources.bindingKey(committed->meshes3D()[0].material,
                                   FrameResourceKind::Mesh3DMaterial), 1U);
    EXPECT_EQ(committed->meshes3D()[0].stableEntityKey, 20U);
    EXPECT_EQ(committed->meshes3D()[1].stableEntityKey, 30U);
    EXPECT_LT(committed->meshes3D()[0].depthBucket, committed->meshes3D()[1].depthBucket);
    EXPECT_FLOAT_EQ(committed->meshes3D()[0].columnMajorWorldTransform[12], 0.0F);
    EXPECT_FLOAT_EQ(committed->meshes3D()[0].columnMajorWorldTransform[14], 0.0F);

    EXPECT_EQ(committed->mesh3DBatches()[0].firstItem, 0U);
    EXPECT_EQ(committed->mesh3DBatches()[0].itemCount, 2U);
    EXPECT_EQ(resources.bindingKey(committed->mesh3DBatches()[0].mesh,
                                   FrameResourceKind::Mesh3DGeometry), 2U);
    EXPECT_EQ(committed->mesh3DBatches()[1].firstItem, 2U);
    EXPECT_EQ(committed->mesh3DBatches()[1].itemCount, 1U);

    const RenderSceneStatistics statistics = committed->statistics();
    EXPECT_EQ(statistics.submittedMesh3DCount, 5U);
    EXPECT_EQ(statistics.visibleMesh3DCount, 3U);
    EXPECT_EQ(statistics.culledMesh3DCount, 2U);
    EXPECT_EQ(statistics.mesh3DBatchCount, 2U);
    EXPECT_NE(statistics.mesh3DSortOrderChecksum, 0U);
}

TEST(RenderSceneBuilderTest, OpaqueBatchSortGroupsInterleavedShaderAndUniformBindings)
{
    FrameResourceScope resources;
    const auto shaderA = resources.resource(FrameResourceKind::Shader, 1U);
    const auto shaderB = resources.resource(FrameResourceKind::Shader, 2U);
    const auto uniformsA = resources.resource(FrameResourceKind::ShaderUniforms, 1U);
    const auto uniformsB = resources.resource(FrameResourceKind::ShaderUniforms, 2U);
    struct Pipeline final {
        FrameResourceRef shader;
        FrameResourceRef uniforms;
    };
    const std::array pipelines{
        Pipeline{}, Pipeline{shaderA, uniformsA}, Pipeline{shaderB, uniformsA}, Pipeline{shaderA, uniformsB}};
    constexpr u32 itemCount = 4'000U;
    RenderSceneCapacity capacity{};
    capacity.mesh3DItemCapacity = itemCount;
    capacity.mesh3DBatchCapacity = static_cast<u32>(pipelines.size());
    CountingResource storage;
    auto created = RenderSceneBuilder::Create(capacity, storage);
    ASSERT_TRUE(created);
    auto builder = std::move(*created);
    storage.rejectAllocations = true;
    for (const bool reverse : {false, true})
    {
        ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
        ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));
        for (u32 index = 0; index < itemCount; ++index)
        {
            const u32 stableIndex = reverse ? itemCount - index - 1U : index;
            const auto& pipeline = pipelines[stableIndex % pipelines.size()];
            auto item = mesh3D(resources, 1, 1, stableIndex + 1U, 0.0F, 0.0F, 0.0F);
            item.shader = pipeline.shader;
            item.shaderUniforms = pipeline.uniforms;
            ASSERT_TRUE(builder.writer().addMesh3D(item));
        }
        auto committed = builder.commit();
        ASSERT_TRUE(committed) << committed.error().message;
        ASSERT_EQ(committed->mesh3DBatches().size(), pipelines.size());
        ASSERT_EQ(committed->meshes3D().size(), itemCount);
        u32 totalBatched = 0;
        for (const auto& batch : committed->mesh3DBatches())
        {
            EXPECT_EQ(batch.firstItem, totalBatched);
            EXPECT_EQ(batch.itemCount, itemCount / pipelines.size());
            u64 previousKey = 0;
            for (const auto& item : committed->meshes3D().subspan(batch.firstItem, batch.itemCount))
            {
                EXPECT_EQ(item.shader, batch.shader);
                EXPECT_EQ(item.shaderUniforms, batch.shaderUniforms);
                EXPECT_GT(item.stableEntityKey, previousKey);
                previousKey = item.stableEntityKey;
            }
            totalBatched += batch.itemCount;
        }
        EXPECT_EQ(totalBatched, itemCount);
    }
}

TEST(RenderSceneBuilderTest, PerspectiveSphereCullingKeepsBoundaryIntersections)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame(perspectiveFrame(1.0F)));
    auto cameraInput = perspectiveCamera(0.0F);
    cameraInput.nearPlaneMeters = 1.0F;
    cameraInput.farPlaneMeters = 10.0F;
    cameraInput.verticalFovDegrees = 90.0F;
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(cameraInput));

    auto nearIntersection = mesh3D(resources, 1, 1, 1, 0.0F, 0.0F, -0.6F);
    nearIntersection.localBounds.radius = 0.5F;
    ASSERT_TRUE(builder.writer().addMesh3D(nearIntersection));

    auto sideIntersection = mesh3D(resources, 1, 1, 2, 2.4F, 0.0F, -2.0F);
    sideIntersection.localBounds.radius = 0.5F;
    ASSERT_TRUE(builder.writer().addMesh3D(sideIntersection));

    auto sideOutside = mesh3D(resources, 1, 1, 3, 3.0F, 0.0F, -2.0F);
    sideOutside.localBounds.radius = 0.25F;
    ASSERT_TRUE(builder.writer().addMesh3D(sideOutside));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_EQ(committed->meshes3D().size(), 2U);
    EXPECT_EQ(committed->statistics().culledMesh3DCount, 1U);
}

TEST(RenderSceneBuilderTest, MeshInputValidationIsStickyAndRejectsNonOpaqueOrDegenerateTransforms)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));

    auto invalidMesh = mesh3D(resources, 1, 1, 1, 0.0F, 0.0F, 0.0F);
    invalidMesh.worldTransform.scaleX = 0.0F;
    invalidMesh.baseColorFactor.alpha = 0.5F;
    auto invalid = builder.writer().addMesh3D(invalidMesh);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, RenderErrorCode::InvalidRenderSceneInput);

    auto sticky = builder.writer().addMesh3D(mesh3D(resources, 1, 1, 2, 0.0F, 0.0F, 0.0F));
    ASSERT_FALSE(sticky);
    EXPECT_EQ(sticky.error().code, RenderErrorCode::InvalidRenderSceneInput);
    auto commit = builder.commit();
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().code, RenderErrorCode::InvalidRenderSceneInput);
}

TEST(RenderSceneBuilderTest, MeshesRequirePerspectiveCameraAndBatchCapacityIsTransactional)
{
    FrameResourceScope resources;
    RenderSceneCapacity capacity{};
    capacity.mesh3DItemCapacity = 4;
    capacity.mesh3DBatchCapacity = 1;
    auto builderResult = RenderSceneBuilder::Create(capacity);
    ASSERT_TRUE(builderResult.has_value());
    RenderSceneBuilder builder = std::move(*builderResult);

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 1, 1, 1, 0.0F, 0.0F, 0.0F)));
    auto missingCamera = builder.commit();
    ASSERT_FALSE(missingCamera);
    EXPECT_EQ(missingCamera.error().code, RenderErrorCode::RenderSceneMissingCamera);

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 1, 1, 1, 0.0F, 0.0F, 0.0F)));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 2, 2, 2, 1.0F, 0.0F, 0.0F)));
    auto batchOverflow = builder.commit();
    ASSERT_FALSE(batchOverflow);
    EXPECT_EQ(batchOverflow.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
    EXPECT_TRUE(builder.publishedView().empty());
}

TEST(RenderSceneBuilderTest, MeshItemCapacityFailureIsStickyAndTransactional)
{
    FrameResourceScope resources;
    RenderSceneCapacity capacity{};
    capacity.mesh3DItemCapacity = 1;
    auto builderResult = RenderSceneBuilder::Create(capacity);
    ASSERT_TRUE(builderResult.has_value());
    RenderSceneBuilder builder = std::move(*builderResult);

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 1, 1, 1, 0.0F, 0.0F, 0.0F)));
    auto overflow = builder.writer().addMesh3D(mesh3D(resources, 1, 1, 2, 1.0F, 0.0F, 0.0F));
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, RenderErrorCode::RenderSceneCapacityExceeded);

    auto commit = builder.commit();
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
    EXPECT_TRUE(builder.publishedView().empty());
}

TEST(RenderSceneBuilderTest, SkinnedMeshesDeepCopySortCullAndReportPaletteOwnership)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame(perspectiveFrame(1.0F)));
    auto cameraInput = perspectiveCamera(0.0F);
    cameraInput.verticalFovDegrees = 90.0F;
    cameraInput.nearPlaneMeters = 1.0F;
    cameraInput.farPlaneMeters = 10.0F;
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(cameraInput));

    std::array<float, SkinnedMesh3DPaletteFloatsPerJoint> firstPalette{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        2, 3, 4, 1,
    };
    std::array<float, SkinnedMesh3DPaletteFloatsPerJoint> secondPalette = firstPalette;
    secondPalette[12] = 5.0F;
    ASSERT_TRUE(builder.writer().addSkinnedMesh3D(
        skinnedMesh3D(resources, 2, 1, 30, 0.0F, 0.0F, -4.0F, firstPalette)));
    ASSERT_TRUE(builder.writer().addSkinnedMesh3D(
        skinnedMesh3D(resources, 2, 1, 20, 0.0F, 0.0F, -2.0F, secondPalette)));
    ASSERT_TRUE(builder.writer().addSkinnedMesh3D(
        skinnedMesh3D(resources, 9, 9, 40, 100.0F, 0.0F, -2.0F, firstPalette)));
    auto invisible = skinnedMesh3D(resources, 3, 3, 50, 0.0F, 0.0F, -2.0F, firstPalette);
    invisible.visible = false;
    ASSERT_TRUE(builder.writer().addSkinnedMesh3D(invisible));
    firstPalette[12] = 99.0F;
    secondPalette[12] = 98.0F;

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);
    ASSERT_EQ(committed->skinnedMeshes3D().size(), 2U);
    EXPECT_EQ(committed->skinnedMeshes3D()[0].stableEntityKey, 20U);
    EXPECT_EQ(committed->skinnedMeshes3D()[1].stableEntityKey, 30U);
    EXPECT_EQ(committed->skinnedMeshes3D()[0].paletteJointOffset, 1U);
    EXPECT_EQ(committed->skinnedMeshes3D()[1].paletteJointOffset, 0U);
    ASSERT_EQ(committed->skinnedMesh3DPalette().size(), 3U * SkinnedMesh3DPaletteFloatsPerJoint);
    EXPECT_FLOAT_EQ(committed->skinnedMesh3DPalette()[12], 2.0F);
    EXPECT_FLOAT_EQ(
        committed->skinnedMesh3DPalette()[SkinnedMesh3DPaletteFloatsPerJoint + 12U],
        5.0F);

    const RenderSceneStatistics statistics = committed->statistics();
    EXPECT_EQ(statistics.submittedSkinnedMesh3DCount, 4U);
    EXPECT_EQ(statistics.visibleSkinnedMesh3DCount, 2U);
    EXPECT_EQ(statistics.culledSkinnedMesh3DCount, 1U);
    EXPECT_EQ(statistics.prunedInvisibleSkinnedMesh3DCount, 1U);
    EXPECT_EQ(statistics.skinnedMesh3DPaletteJointCount, 3U);
    EXPECT_NE(statistics.skinnedMesh3DSortOrderChecksum, 0U);
}

TEST(RenderSceneBuilderTest, SkinnedMeshValidationAndCapacitiesAreStickyAndTransactional)
{
    FrameResourceScope resources;
    RenderSceneCapacity capacity{};
    capacity.skinnedMesh3DItemCapacity = 1;
    capacity.skinnedMesh3DPaletteJointCapacity = MaxSkinnedMesh3DPaletteJointCount;
    auto builderResult = RenderSceneBuilder::Create(capacity);
    ASSERT_TRUE(builderResult.has_value());
    RenderSceneBuilder builder = std::move(*builderResult);
    std::array<float, SkinnedMesh3DPaletteFloatsPerJoint> palette{};

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));
    auto invalid = skinnedMesh3D(resources, 1, 1, 1, 0.0F, 0.0F, 0.0F, palette);
    invalid.paletteColumnMajorJointMatrices =
        std::span<const float>{palette}.first(palette.size() - 1U);
    const Core::Status malformed = builder.writer().addSkinnedMesh3D(invalid);
    ASSERT_FALSE(malformed);
    EXPECT_EQ(malformed.error().code, RenderErrorCode::InvalidRenderSceneInput);
    EXPECT_FALSE(builder.commit().has_value());

    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));
    ASSERT_TRUE(builder.writer().addSkinnedMesh3D(
        skinnedMesh3D(resources, 1, 1, 1, 0.0F, 0.0F, 0.0F, palette)));
    auto overflow = builder.writer().addSkinnedMesh3D(
        skinnedMesh3D(resources, 1, 1, 2, 1.0F, 0.0F, 0.0F, palette));
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
    EXPECT_FALSE(builder.commit().has_value());
    EXPECT_TRUE(builder.publishedView().empty());

    capacity.skinnedMesh3DItemCapacity = 2;
    auto paletteBuilderResult = RenderSceneBuilder::Create(capacity);
    ASSERT_TRUE(paletteBuilderResult.has_value());
    RenderSceneBuilder paletteBuilder = std::move(*paletteBuilderResult);
    std::array<float,
               MaxSkinnedMesh3DPaletteJointCount * SkinnedMesh3DPaletteFloatsPerJoint>
        fullPalette{};
    ASSERT_TRUE(paletteBuilder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(paletteBuilder.writer().setPerspectiveCamera(perspectiveCamera()));
    ASSERT_TRUE(paletteBuilder.writer().addSkinnedMesh3D(
        skinnedMesh3D(resources, 1, 1, 1, 0.0F, 0.0F, 0.0F, fullPalette)));
    auto paletteOverflow = paletteBuilder.writer().addSkinnedMesh3D(
        skinnedMesh3D(resources, 1, 1, 2, 1.0F, 0.0F, 0.0F, palette));
    ASSERT_FALSE(paletteOverflow);
    EXPECT_EQ(paletteOverflow.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
    EXPECT_FALSE(paletteBuilder.commit().has_value());
    EXPECT_TRUE(paletteBuilder.publishedView().empty());
}

TEST(RenderSceneBuilderTest, TwoDimensionalAndPerspectiveCamerasCanShareOneWorldScene)
{
    FrameResourceScope resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 1, 1, 0.0F, 0.0F)));
    ASSERT_TRUE(builder.writer().addMesh3D(mesh3D(resources, 1, 1, 2, 0.0F, 0.0F, 0.0F)));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_TRUE(committed->camera2D().has_value());
    EXPECT_TRUE(committed->perspectiveCamera().has_value());
    EXPECT_EQ(committed->sprites2D().size(), 1U);
    EXPECT_EQ(committed->meshes3D().size(), 1U);
    EXPECT_EQ(committed->statistics().cameraCount, 2U);
    EXPECT_EQ(committed->statistics().camera2DCount, 1U);
    EXPECT_EQ(committed->statistics().perspectiveCameraCount, 1U);
}

TEST(RenderSceneBuilderTest, ReusesFixedStorageAcrossThreeHundredFrames)
{
    FrameResourceScope resources;
    CountingResource resource;
    {
        auto builderResult = RenderSceneBuilder::Create(RenderSceneCapacity{4}, resource);
        ASSERT_TRUE(builderResult.has_value());
        RenderSceneBuilder builder = std::move(*builderResult);
        const usize allocationsAfterCreate = resource.allocations;

        for (u32 frame = 0; frame < 300; ++frame)
        {
            ASSERT_TRUE(builder.beginFrame(perspectiveFrame()));
            ASSERT_TRUE(builder.writer().setCamera2D(camera()));
            ASSERT_TRUE(builder.writer().setPerspectiveCamera(perspectiveCamera()));
            ASSERT_TRUE(builder.writer().addSprite2D(sprite(resources, 1, frame + 1U, 0.0F, 0.0F)));
            ASSERT_TRUE(builder.writer().addMesh3D(
                mesh3D(resources, 1, 1, frame + 10'000U, 0.0F, 0.0F, 0.0F)));
            auto committed = builder.commit();
            ASSERT_TRUE(committed.has_value());
            ASSERT_EQ(committed->sprites2D().size(), 1U);
            ASSERT_EQ(committed->meshes3D().size(), 1U);
            ASSERT_EQ(committed->mesh3DBatches().size(), 1U);
        }

        EXPECT_EQ(resource.allocations, allocationsAfterCreate);
    }
    EXPECT_EQ(resource.allocations, resource.deallocations);
}

TEST(RenderSceneBuilderTest, MoveTransfersFixedStorageExactlyOnce)
{
    FrameResourceScope resources;
    CountingResource resource;
    {
        auto builderResult = RenderSceneBuilder::Create(RenderSceneCapacity{4}, resource);
        ASSERT_TRUE(builderResult.has_value());
        const usize allocationsAfterCreate = resource.allocations;
        RenderSceneBuilder original = std::move(*builderResult);
        RenderSceneBuilder moved = std::move(original);
        EXPECT_EQ(resource.allocations, allocationsAfterCreate);

        ASSERT_TRUE(moved.beginFrame());
        ASSERT_TRUE(moved.writer().setCamera2D(camera()));
        ASSERT_TRUE(moved.writer().addSprite2D(sprite(resources, 1, 1, 0.0F, 0.0F)));
        ASSERT_TRUE(moved.commit());
        EXPECT_EQ(resource.allocations, allocationsAfterCreate);
    }
    EXPECT_EQ(resource.allocations, resource.deallocations);
}

} // namespace
} // namespace Tina::Render
