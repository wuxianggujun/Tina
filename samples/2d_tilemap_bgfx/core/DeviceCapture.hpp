#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory>
#include <utility>

namespace Tina::Sample2D {

// Product-gate frame capture, installed by the Desktop::CreateEngine options wrap. It
// exists for requestCaptureNextPresent() and the post-run pixel/statistics readback,
// which are not IRenderDevice capabilities. Reaching the device itself goes through the
// phase contexts instead (ADR 0046); get() is only for main() after run() returns.
class DeviceCapture final {
  public:
    void set(Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Render::IRenderDevice* get() const noexcept { return device_; }
    void requestCaptureNextPresent() noexcept { captureNextPresent_ = true; }
    [[nodiscard]] bool consumeCaptureNextPresent() noexcept
    {
        const bool requested = captureNextPresent_;
        captureNextPresent_ = false;
        return requested;
    }
    void setLastCapture(Render::Rgba8FrameCapture capture) noexcept
    {
        lastCapture_ = std::move(capture);
        hasLastCapture_ = true;
    }
    [[nodiscard]] bool hasLastCapture() const noexcept { return hasLastCapture_; }
    [[nodiscard]] const Render::Rgba8FrameCapture* lastCapture() const noexcept
    {
        return hasLastCapture_ ? &lastCapture_ : nullptr;
    }
    void setLastRenderStatistics(Render::RenderStatistics statistics) noexcept
    {
        lastRenderStatistics_ = statistics;
        hasLastRenderStatistics_ = true;
    }
    [[nodiscard]] const Render::RenderStatistics* lastRenderStatistics() const noexcept
    {
        return hasLastRenderStatistics_ ? &lastRenderStatistics_ : nullptr;
    }
    void noteSubmittedWorldScene(const Render::RenderSceneView& scene) noexcept
    {
        lastSubmittedWorldSceneStatistics_ = scene.statistics();
        hasLastSubmittedWorldSceneStatistics_ = true;
        lastSubmittedSoftShadowPointLight2DCount_ = 0;
        lastSubmittedNormalMappedSpriteCount_ = 0;
        if (scene.sprite2DLighting().has_value())
        {
            for (const Render::Sprite2DPointLight& light : scene.sprite2DLighting()->pointLights())
            {
                if (light.sourceRadiusMeters > 0.0F)
                {
                    ++lastSubmittedSoftShadowPointLight2DCount_;
                }
            }
        }
        for (const Render::RenderSprite2DItem& sprite : scene.sprites2D())
        {
            if (sprite.normalTexture.hasValue())
            {
                ++lastSubmittedNormalMappedSpriteCount_;
            }
        }
        if (lastSubmittedWorldSceneStatistics_.sprite2DLightingConfigured)
        {
            ++sprite2DLightingFrameCount_;
        }
    }
    [[nodiscard]] const Render::RenderSceneStatistics* lastSubmittedWorldSceneStatistics() const noexcept
    {
        return hasLastSubmittedWorldSceneStatistics_ ? &lastSubmittedWorldSceneStatistics_ : nullptr;
    }
    [[nodiscard]] Core::u64 sprite2DLightingFrameCount() const noexcept
    {
        return sprite2DLightingFrameCount_;
    }
    [[nodiscard]] Core::u32 lastSubmittedSoftShadowPointLight2DCount() const noexcept
    {
        return lastSubmittedSoftShadowPointLight2DCount_;
    }
    [[nodiscard]] Core::u32 lastSubmittedNormalMappedSpriteCount() const noexcept
    {
        return lastSubmittedNormalMappedSpriteCount_;
    }

  private:
    Render::IRenderDevice* device_ = nullptr;
    bool captureNextPresent_ = false;
    bool hasLastCapture_ = false;
    Render::Rgba8FrameCapture lastCapture_{};
    bool hasLastRenderStatistics_ = false;
    Render::RenderStatistics lastRenderStatistics_{};
    bool hasLastSubmittedWorldSceneStatistics_ = false;
    Render::RenderSceneStatistics lastSubmittedWorldSceneStatistics_{};
    Core::u64 sprite2DLightingFrameCount_ = 0;
    Core::u32 lastSubmittedSoftShadowPointLight2DCount_ = 0;
    Core::u32 lastSubmittedNormalMappedSpriteCount_ = 0;
};

class CapturingRenderDevice final : public Render::IRenderDevice {
  public:
    CapturingRenderDevice(std::unique_ptr<Render::IRenderDevice> inner, DeviceCapture& capture) noexcept
        : inner_(std::move(inner)), capture_(&capture)
    {
        capture_->set(this);
    }

    ~CapturingRenderDevice() override
    {
        if (capture_ != nullptr && capture_->get() == this)
        {
            capture_->set(nullptr);
        }
    }

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame& frame) override
    {
        auto submission = inner_->submitFrame(frame);
        if (capture_ != nullptr)
        {
            capture_->setLastRenderStatistics(inner_->statistics());
        }
        if (submission && submission->requiresPresent() && capture_ != nullptr)
        {
            capture_->noteSubmittedWorldScene(frame.primaryWorldScene);
        }
        return submission;
    }
    [[nodiscard]] Core::Status present() override
    {
        auto status = inner_->present();
        if (capture_ != nullptr)
        {
            capture_->setLastRenderStatistics(inner_->statistics());
        }
        if (status && capture_ != nullptr && capture_->consumeCaptureNextPresent())
        {
            auto captured = inner_->capturePrimaryFrameRgba8();
            if (captured.has_value() && !captured->empty())
            {
                capture_->setLastCapture(std::move(*captured));
            }
        }
        return status;
    }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return inner_->statistics();
    }
    void shutdown() noexcept override
    {
        if (capture_ != nullptr)
        {
            capture_->setLastRenderStatistics(inner_->statistics());
        }
        inner_->shutdown();
    }
    [[nodiscard]] Core::Result<Render::GpuTextureId>
    createTexture2D(const Render::Texture2DUploadDesc& desc) override
    {
        return inner_->createTexture2D(desc);
    }
    [[nodiscard]] Core::Status validateTexture2D(Render::GpuTextureId texture) const noexcept override
    {
        return inner_->validateTexture2D(texture);
    }
    [[nodiscard]] Core::Status destroyTexture2D(Render::GpuTextureId texture) noexcept override
    {
        return inner_->destroyTexture2D(texture);
    }
    [[nodiscard]] Core::Status retireTexture2D(Render::GpuTextureId texture,
                                               Render::FramePin& completionPin) noexcept override
    {
        return inner_->retireTexture2D(texture, completionPin);
    }
    [[nodiscard]] Core::Result<Render::GpuEnvironmentMapId>
    createEnvironmentMap(const Render::EnvironmentMapUploadDesc& desc) override
    {
        return inner_->createEnvironmentMap(desc);
    }
    [[nodiscard]] Core::Status validateEnvironmentMap(
        Render::GpuEnvironmentMapId environmentMap) const noexcept override
    {
        return inner_->validateEnvironmentMap(environmentMap);
    }
    [[nodiscard]] Core::Status destroyEnvironmentMap(
        Render::GpuEnvironmentMapId environmentMap) noexcept override
    {
        return inner_->destroyEnvironmentMap(environmentMap);
    }
    [[nodiscard]] Core::Status retireEnvironmentMap(
        Render::GpuEnvironmentMapId environmentMap,
        Render::FramePin& completionPin) noexcept override
    {
        return inner_->retireEnvironmentMap(environmentMap, completionPin);
    }
    [[nodiscard]] Core::Status setTexture2DBinding(Core::u32 spriteKey,
                                                   Render::GpuTextureId texture) noexcept override
    {
        return inner_->setTexture2DBinding(spriteKey, texture);
    }
    [[nodiscard]] Core::Result<Render::Rgba8FrameCapture> capturePrimaryFrameRgba8() override
    {
        return inner_->capturePrimaryFrameRgba8();
    }
    [[nodiscard]] Core::Result<Render::GpuMeshId>
    createStaticMesh(const Render::StaticMeshUploadDesc& desc) override
    {
        return inner_->createStaticMesh(desc);
    }
    [[nodiscard]] Core::Status destroyGpuMesh(Render::GpuMeshId mesh) noexcept override
    {
        return inner_->destroyGpuMesh(mesh);
    }
    [[nodiscard]] Core::Status retireGpuMesh(Render::GpuMeshId mesh,
                                             Render::FramePin& completionPin) noexcept override
    {
        return inner_->retireGpuMesh(mesh, completionPin);
    }
    [[nodiscard]] Core::Status drainGpuRetirements() noexcept override
    {
        return inner_->drainGpuRetirements();
    }
    [[nodiscard]] Core::Status setMesh3DBinding(Core::u32 meshKey, Render::GpuMeshId mesh) noexcept override
    {
        return inner_->setMesh3DBinding(meshKey, mesh);
    }
    [[nodiscard]] Core::Status setMesh3DMaterialBinding(
        Core::u32 materialKey, const Render::Mesh3DMaterialBindingDesc& desc) noexcept override
    {
        return inner_->setMesh3DMaterialBinding(materialKey, desc);
    }
    [[nodiscard]] Core::Status clearMesh3DMaterialBinding(Core::u32 materialKey) noexcept override
    {
        return inner_->clearMesh3DMaterialBinding(materialKey);
    }
    [[nodiscard]] Core::Status setMesh3DMaterialTextureBinding(Core::u32 materialKey,
                                                               Render::GpuTextureId texture) noexcept override
    {
        return inner_->setMesh3DMaterialTextureBinding(materialKey, texture);
    }
    [[nodiscard]] Core::Status setMesh3DMaterialMetallicRoughnessTextureBinding(
        Core::u32 materialKey, Render::GpuTextureId texture) noexcept override
    {
        return inner_->setMesh3DMaterialMetallicRoughnessTextureBinding(materialKey, texture);
    }
    [[nodiscard]] Core::Status setMesh3DMaterialFactors(Core::u32 materialKey, float metallic,
                                                        float roughness) noexcept override
    {
        return inner_->setMesh3DMaterialFactors(materialKey, metallic, roughness);
    }
    [[nodiscard]] Core::Status setMesh3DMaterialNormalTextureBinding(
        Core::u32 materialKey, Render::GpuTextureId texture) noexcept override
    {
        return inner_->setMesh3DMaterialNormalTextureBinding(materialKey, texture);
    }
    [[nodiscard]] Core::Status setMesh3DLighting(const Render::Mesh3DLightingDesc& lighting) noexcept override
    {
        return inner_->setMesh3DLighting(lighting);
    }
    [[nodiscard]] Core::Status setMesh3DImageBasedLighting(
        const Render::Mesh3DImageBasedLightingDesc& lighting) noexcept override
    {
        return inner_->setMesh3DImageBasedLighting(lighting);
    }
    [[nodiscard]] Core::Status clearMesh3DImageBasedLighting() noexcept override
    {
        return inner_->clearMesh3DImageBasedLighting();
    }

  private:
    std::unique_ptr<Render::IRenderDevice> inner_;
    DeviceCapture* capture_ = nullptr;
};

[[nodiscard]] inline Core::Result<std::unique_ptr<Render::IRenderDevice>>
wrapCapturingRenderDevice(std::unique_ptr<Render::IRenderDevice> device, DeviceCapture& capture)
{
    std::unique_ptr<Render::IRenderDevice> capturing =
        std::make_unique<CapturingRenderDevice>(std::move(device), capture);
    return capturing;
}

} // namespace Tina::Sample2D
