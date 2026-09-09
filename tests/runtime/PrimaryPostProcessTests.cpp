#include "runtime/PrimaryPostProcess.hpp"

#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace Tina::Tests {
namespace {

class RecordingTargetDevice final : public Render::IRenderDevice {
  public:
    explicit RecordingTargetDevice(std::unique_ptr<Render::IRenderDevice> device) noexcept
        : device_(std::move(device)) {}

    Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame& frame) override
    { return device_->submitFrame(frame); }
    Core::Status present() override { return device_->present(); }
    Render::RenderStatistics statistics() const noexcept override { return device_->statistics(); }
    void shutdown() noexcept override { device_->shutdown(); }

    Core::Result<Render::GpuRenderTextureId> createRenderTexture(const Render::RenderTextureDesc& desc) override
    {
        ++createAttempts;
        if (createAttempts == rejectCreateAttempt)
            return Core::failure(Render::RenderErrorCode::RenderTextureUnsupported, "injected target creation failure");
        descriptions.push_back(desc);
        return device_->createRenderTexture(desc);
    }
    Core::Status destroyRenderTexture(Render::GpuRenderTextureId texture) noexcept override
    {
        ++destroyAttempts;
        if (destroyAttempts == rejectDestroyAttempt)
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported, "injected target retirement failure");
        return device_->destroyRenderTexture(texture);
    }
    Core::Status setRenderTextureBinding(Core::u32 key, Render::GpuRenderTextureId texture) noexcept override
    { return device_->setRenderTextureBinding(key, texture); }
    Core::Status clearRenderTextureBinding(Core::u32 key) noexcept override
    { return device_->clearRenderTextureBinding(key); }

    Core::u32 createAttempts = 0;
    Core::u32 destroyAttempts = 0;
    Core::u32 rejectCreateAttempt = 0;
    Core::u32 rejectDestroyAttempt = 0;
    std::vector<Render::RenderTextureDesc> descriptions{};
  private:
    std::unique_ptr<Render::IRenderDevice> device_;
};

Render::RenderSurfaceState activeSurface(Core::u32 width = 800, Core::u32 height = 600)
{
    return {.surface = {1, 0, 1}, .framebufferExtent = {width, height},
            .sourceMetricsRevision = 1, .surfaceRevision = 1,
            .availability = Render::RenderSurfaceAvailability::Active};
}

class PrimaryPostProcessTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        auto created = Render::createNullRenderDevice({});
        ASSERT_TRUE(created);
        device = std::make_unique<RecordingTargetDevice>(std::move(*created));
        ASSERT_TRUE(packet.beginFrame(0));
        auto shader = intern(Render::FrameResourceKind::Shader, 11);
        auto uniforms = intern(Render::FrameResourceKind::ShaderUniforms, 27);
        ASSERT_TRUE(shader);
        ASSERT_TRUE(uniforms);
        settings.enabled = true;
        settings.customEffectCount = 3;
        for (auto& effect : settings.customEffects) effect = {*shader, *uniforms};
    }
    void TearDown() override
    {
        if (device) EXPECT_TRUE(targets.shutdown(*device));
    }
    Core::Result<Render::FrameResourceRef> intern(Render::FrameResourceKind kind, Core::u32 key)
    {
        Render::FramePin pin{Render::FramePinKind::Custom, key, &releasedPins,
            [](void* value) noexcept { ++*static_cast<Core::u32*>(value); }};
        return packet.resourceSink().intern({kind, key}, std::move(pin));
    }
    Core::Result<Render::RenderPostProcessChainView> prepare(Render::RenderSurfaceState surface = activeSurface())
    { return targets.prepare(*device, surface, settings, packet.resourceTableView()); }

    std::unique_ptr<RecordingTargetDevice> device{};
    Runtime::Detail::PrimaryPostProcess targets{};
    Core::u32 releasedPins = 0;
    Render::RenderFramePacket packet{};
    Render::PrimaryPostProcessSettings settings{};
};

TEST_F(PrimaryPostProcessTest, ReusesTwoFullResolutionTargetsForMultipleEffects)
{
    auto chain = prepare();
    ASSERT_TRUE(chain) << chain.error().message;
    EXPECT_EQ(device->createAttempts, 4U); // scene color/depth + two effect targets
    ASSERT_EQ(chain->customSteps.size(), 3U);
    EXPECT_EQ(chain->customSteps[0].sourceBindingKey, chain->sceneColorTargetBindingKey);
    EXPECT_EQ(chain->customSteps[1].sourceBindingKey, chain->customSteps[0].destinationBindingKey);
    EXPECT_NE(chain->customSteps[0].destinationBindingKey, chain->customSteps[1].destinationBindingKey);
    EXPECT_EQ(chain->customSteps[2].destinationBindingKey, chain->customSteps[0].destinationBindingKey);
    EXPECT_EQ(chain->customSteps[2].shaderBindingKey, 11U);
    EXPECT_EQ(chain->customSteps[2].shaderUniformBindingKey, 27U);
    for (const auto& description : device->descriptions) {
        EXPECT_EQ(description.width, 800U);
        EXPECT_EQ(description.height, 600U);
    }
    settings.customEffectCount = Render::PrimaryPostProcessSettings::MaximumCustomEffectCount;
    ASSERT_TRUE(prepare());
    EXPECT_EQ(device->createAttempts, 4U);
}

TEST_F(PrimaryPostProcessTest, RejectsStaleOrWrongKindPacketRefsBeforeAllocatingTargets)
{
    auto wrongKind = settings;
    wrongKind.customEffects[0].shader = wrongKind.customEffects[0].shaderUniforms;
    auto rejected = targets.prepare(*device, activeSurface(), wrongKind, packet.resourceTableView());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::InvalidFrameResource);
    Render::RenderFramePacket foreign;
    ASSERT_TRUE(foreign.beginFrame(0));
    rejected = targets.prepare(*device, activeSurface(), settings, foreign.resourceTableView());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->createAttempts, 0U);
}

TEST_F(PrimaryPostProcessTest, FailedResizeKeepsPreviousBindingsAndRetiresCandidatesOnRetry)
{
    auto original = prepare();
    ASSERT_TRUE(original);
    const auto originalSceneKey = original->sceneColorTargetBindingKey;
    const auto originalEffectKey = original->customSteps.front().destinationBindingKey;
    device->rejectCreateAttempt = device->createAttempts + 3;
    auto failed = prepare(activeSurface(1200, 800));
    ASSERT_FALSE(failed);
    EXPECT_EQ(device->statistics().liveRenderTextures, 6U);
    auto retained = prepare();
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained->sceneColorTargetBindingKey, originalSceneKey);
    EXPECT_EQ(retained->customSteps.front().destinationBindingKey, originalEffectKey);
    EXPECT_EQ(device->statistics().liveRenderTextures, 4U);
    EXPECT_EQ(device->createAttempts, 7U);
    auto resized = prepare(activeSurface(1200, 800));
    ASSERT_TRUE(resized);
    EXPECT_NE(resized->sceneColorTargetBindingKey, originalSceneKey);
    ASSERT_TRUE(prepare(activeSurface(1200, 800)));
    EXPECT_EQ(device->statistics().liveRenderTextures, 4U);
}

TEST_F(PrimaryPostProcessTest, SuspendPreservesTargetsAndDisableRetiresThem)
{
    auto original = prepare();
    ASSERT_TRUE(original);
    const auto key = original->sceneColorTargetBindingKey;
    auto suspended = activeSurface(0, 0);
    suspended.availability = Render::RenderSurfaceAvailability::Suspended;
    auto skipped = prepare(suspended);
    ASSERT_TRUE(skipped);
    EXPECT_FALSE(skipped->enabled());
    EXPECT_EQ(device->statistics().liveRenderTextures, 4U);
    auto resumed = prepare();
    ASSERT_TRUE(resumed);
    EXPECT_EQ(resumed->sceneColorTargetBindingKey, key);
    EXPECT_EQ(device->createAttempts, 4U);
    settings.enabled = false;
    ASSERT_TRUE(prepare());
    EXPECT_EQ(device->statistics().liveRenderTextures, 0U);
}

TEST_F(PrimaryPostProcessTest, FailedRetirementRemainsRetryable)
{
    ASSERT_TRUE(prepare());
    device->rejectDestroyAttempt = 1;
    EXPECT_FALSE(targets.shutdown(*device));
    EXPECT_EQ(device->statistics().liveRenderTextures, 4U);
    ASSERT_TRUE(targets.shutdown(*device));
    EXPECT_EQ(device->statistics().liveRenderTextures, 0U);
}

TEST_F(PrimaryPostProcessTest, ValidatesCustomEffectCountAndRequiredRefs)
{
    settings.customEffectCount = Render::PrimaryPostProcessSettings::MaximumCustomEffectCount + 1;
    EXPECT_FALSE(Render::validatePrimaryPostProcessSettings(settings));
    settings.customEffectCount = 1;
    settings.customEffects[0].shader = {};
    EXPECT_FALSE(Render::validatePrimaryPostProcessSettings(settings));
    EXPECT_EQ(device->createAttempts, 0U);
}

} // namespace
} // namespace Tina::Tests
