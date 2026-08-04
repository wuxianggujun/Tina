#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory>
#include <utility>

namespace Tina::Sample3D {

// Product-gate capture: observes live IRenderDevice for mesh/texture bind without
// hand-rolling EngineCompositionFactories. Desktop::CreateEngine wrap installs it.
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
    void observeSubmittedScene(const Render::RenderSceneView& scene) noexcept
    {
        const Render::RenderSceneStatistics& statistics = scene.statistics();
        if (!statistics.mesh3DLightingConfigured)
        {
            return;
        }
        const auto& camera = scene.perspectiveCamera();
        if (!camera.has_value())
        {
            return;
        }
        Core::u32 directionalShadowCasterCount = 0;
        if (scene.mesh3DLighting().has_value())
        {
            for (const Render::Mesh3DDirectionalLight& light :
                 scene.mesh3DLighting()->directionalLights())
            {
                directionalShadowCasterCount += light.castsShadows ? 1U : 0U;
            }
        }
        if (submittedLightingFrames_ == 0)
        {
            directionalLightCount_ = statistics.directionalLightCount;
            directionalShadowCasterCount_ = directionalShadowCasterCount;
            pointLight3DCount_ = statistics.pointLight3DCount;
            spotLight3DCount_ = statistics.spotLight3DCount;
            submittedCameraAspectRatio_ = camera->aspectRatio;
        }
        else
        {
            if (directionalLightCount_ != statistics.directionalLightCount ||
                directionalShadowCasterCount_ != directionalShadowCasterCount ||
                pointLight3DCount_ != statistics.pointLight3DCount ||
                spotLight3DCount_ != statistics.spotLight3DCount)
            {
                lightingCountsStable_ = false;
            }
            if (submittedCameraAspectRatio_ != camera->aspectRatio)
            {
                ++cameraAspectChanges_;
                submittedCameraAspectRatio_ = camera->aspectRatio;
            }
        }
        ++submittedLightingFrames_;
    }
    [[nodiscard]] Core::u64 submittedLightingFrames() const noexcept
    {
        return submittedLightingFrames_;
    }
    [[nodiscard]] Core::u32 directionalLightCount() const noexcept
    {
        return directionalLightCount_;
    }
    [[nodiscard]] Core::u32 directionalShadowCasterCount() const noexcept
    {
        return directionalShadowCasterCount_;
    }
    [[nodiscard]] Core::u32 pointLight3DCount() const noexcept
    {
        return pointLight3DCount_;
    }
    [[nodiscard]] Core::u32 spotLight3DCount() const noexcept
    {
        return spotLight3DCount_;
    }
    [[nodiscard]] bool lightingCountsStable() const noexcept
    {
        return lightingCountsStable_;
    }
    [[nodiscard]] float submittedCameraAspectRatio() const noexcept
    {
        return submittedCameraAspectRatio_;
    }
    [[nodiscard]] Core::u64 cameraAspectChanges() const noexcept
    {
        return cameraAspectChanges_;
    }
    void recordTangentMeshUpload() noexcept { ++tangentMeshesUploaded_; }
    [[nodiscard]] Core::u64 tangentMeshesUploaded() const noexcept
    {
        return tangentMeshesUploaded_;
    }

  private:
    Render::IRenderDevice* device_ = nullptr;
    bool captureNextPresent_ = false;
    bool hasLastCapture_ = false;
    Render::Rgba8FrameCapture lastCapture_{};
    Core::u64 submittedLightingFrames_ = 0;
    Core::u32 directionalLightCount_ = 0;
    Core::u32 directionalShadowCasterCount_ = 0;
    Core::u32 pointLight3DCount_ = 0;
    Core::u32 spotLight3DCount_ = 0;
    float submittedCameraAspectRatio_ = 0.0F;
    Core::u64 cameraAspectChanges_ = 0;
    Core::u64 tangentMeshesUploaded_ = 0;
    bool lightingCountsStable_ = true;
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
        if (submission && submission->requiresPresent() && capture_ != nullptr)
        {
            capture_->observeSubmittedScene(frame.primaryWorldScene);
        }
        return submission;
    }
    [[nodiscard]] Core::Status present() override
    {
        auto status = inner_->present();
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
    void shutdown() noexcept override { inner_->shutdown(); }
    [[nodiscard]] Core::Result<Render::GpuTextureId>
    createTexture2DRgba8(const Render::Texture2DUploadDesc& desc) override
    {
        return inner_->createTexture2DRgba8(desc);
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
        auto mesh = inner_->createStaticMesh(desc);
        if (mesh && capture_ != nullptr)
        {
            capture_->recordTangentMeshUpload();
        }
        return mesh;
    }
    [[nodiscard]] Core::Status destroyStaticMesh(Render::GpuMeshId mesh) noexcept override
    {
        return inner_->destroyStaticMesh(mesh);
    }
    [[nodiscard]] Core::Status retireStaticMesh(Render::GpuMeshId mesh,
                                                Render::FramePin& completionPin) noexcept override
    {
        return inner_->retireStaticMesh(mesh, completionPin);
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

} // namespace Tina::Sample3D
