#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
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
        if (statistics.transparent3DDrawCount != 0)
        {
            if (submittedTransparent3DFrames_ != 0 &&
                submittedTransparent3DSortOrderChecksum_ !=
                    statistics.transparent3DSortOrderChecksum)
            {
                transparent3DSortOrderStable_ = false;
            }
            ++submittedTransparent3DFrames_;
            submittedTransparentStaticMesh3DCount_ =
                statistics.transparentMesh3DCount;
            submittedTransparentSkinnedMesh3DCount_ =
                statistics.transparentSkinnedMesh3DCount;
            submittedTransparent3DDrawCount_ =
                statistics.transparent3DDrawCount;
            submittedTransparent3DSortOrderChecksum_ =
                statistics.transparent3DSortOrderChecksum;
        }
        if (statistics.submittedSkinnedMesh3DCount != 0)
        {
            const Core::u64 poseFingerprint = fingerprintSkinnedPose(scene.skinnedMesh3DPalette());
            if (submittedSkinnedMesh3DFrames_ == 0)
            {
                firstSubmittedSkinnedPoseFingerprint_ = poseFingerprint;
            }
            else if (submittedSkinnedPoseFingerprint_ != poseFingerprint)
            {
                ++submittedSkinnedPoseFingerprintChanges_;
            }
            ++submittedSkinnedMesh3DFrames_;
            submittedSkinnedMesh3DCount_ = statistics.submittedSkinnedMesh3DCount;
            visibleSkinnedMesh3DCount_ = statistics.visibleSkinnedMesh3DCount;
            submittedSkinnedPaletteJointCount_ = statistics.skinnedMesh3DPaletteJointCount;
            submittedSkinnedPoseFingerprint_ = poseFingerprint;
        }
        if (!statistics.mesh3DLightingConfigured)
        {
            return;
        }
        const auto& camera = scene.perspectiveCamera();
        if (!camera.has_value())
        {
            return;
        }
        Core::u32 cascadedDirectionalShadowCount = 0;
        Core::u32 cascadedDirectionalShadowCascadeCount = 0;
        std::optional<Render::Mesh3DSpotLightShadow> spotLightShadow;
        std::optional<Render::Mesh3DPointLightShadow> pointLightShadow;
        if (scene.mesh3DLighting().has_value())
        {
            if (scene.mesh3DLighting()->cascadedDirectionalShadow().has_value())
            {
                cascadedDirectionalShadowCount = 1U;
                cascadedDirectionalShadowCascadeCount =
                    Render::Mesh3DCascadedDirectionalShadow::CascadeCount;
            }
            spotLightShadow = scene.mesh3DLighting()->spotLightShadow();
            pointLightShadow = scene.mesh3DLighting()->pointLightShadow();
        }
        if (submittedLightingFrames_ == 0)
        {
            directionalLightCount_ = statistics.directionalLightCount;
            cascadedDirectionalShadowCount_ = cascadedDirectionalShadowCount;
            cascadedDirectionalShadowCascadeCount_ = cascadedDirectionalShadowCascadeCount;
            spotLightShadow_ = spotLightShadow;
            pointLightShadow_ = pointLightShadow;
            pointLight3DCount_ = statistics.pointLight3DCount;
            spotLight3DCount_ = statistics.spotLight3DCount;
            submittedCameraAspectRatio_ = camera->aspectRatio;
        }
        else
        {
            if (directionalLightCount_ != statistics.directionalLightCount ||
                cascadedDirectionalShadowCount_ != cascadedDirectionalShadowCount ||
                cascadedDirectionalShadowCascadeCount_ != cascadedDirectionalShadowCascadeCount ||
                spotLightShadow_ != spotLightShadow ||
                pointLightShadow_ != pointLightShadow ||
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
    [[nodiscard]] Core::u32 cascadedDirectionalShadowCount() const noexcept
    {
        return cascadedDirectionalShadowCount_;
    }
    [[nodiscard]] Core::u32 cascadedDirectionalShadowCascadeCount() const noexcept
    {
        return cascadedDirectionalShadowCascadeCount_;
    }
    [[nodiscard]] Core::u32 spotLightShadowCount() const noexcept
    {
        return spotLightShadow_.has_value() ? 1U : 0U;
    }
    [[nodiscard]] Core::u32 pointLightShadowCount() const noexcept
    {
        return pointLightShadow_.has_value() ? 1U : 0U;
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
    void recordSkinnedMeshUpload(const Render::SkinnedMeshUploadDesc& desc) noexcept
    {
        ++skinnedMeshesUploaded_;
        uploadedSkinnedJointCount_ = desc.jointCount;
    }
    [[nodiscard]] Core::u64 skinnedMeshesUploaded() const noexcept
    {
        return skinnedMeshesUploaded_;
    }
    [[nodiscard]] Core::u32 uploadedSkinnedJointCount() const noexcept
    {
        return uploadedSkinnedJointCount_;
    }
    [[nodiscard]] Core::u64 submittedSkinnedMesh3DFrames() const noexcept
    {
        return submittedSkinnedMesh3DFrames_;
    }
    [[nodiscard]] Core::u32 submittedSkinnedMesh3DCount() const noexcept
    {
        return submittedSkinnedMesh3DCount_;
    }
    [[nodiscard]] Core::u32 visibleSkinnedMesh3DCount() const noexcept
    {
        return visibleSkinnedMesh3DCount_;
    }
    [[nodiscard]] Core::u32 submittedSkinnedPaletteJointCount() const noexcept
    {
        return submittedSkinnedPaletteJointCount_;
    }
    [[nodiscard]] Core::u64 submittedSkinnedPoseFingerprint() const noexcept
    {
        return submittedSkinnedPoseFingerprint_;
    }
    [[nodiscard]] Core::u64 firstSubmittedSkinnedPoseFingerprint() const noexcept
    {
        return firstSubmittedSkinnedPoseFingerprint_;
    }
    [[nodiscard]] Core::u64 submittedSkinnedPoseFingerprintChanges() const noexcept
    {
        return submittedSkinnedPoseFingerprintChanges_;
    }
    [[nodiscard]] Core::u64 submittedTransparent3DFrames() const noexcept
    {
        return submittedTransparent3DFrames_;
    }
    [[nodiscard]] Core::u32 submittedTransparentStaticMesh3DCount() const noexcept
    {
        return submittedTransparentStaticMesh3DCount_;
    }
    [[nodiscard]] Core::u32 submittedTransparentSkinnedMesh3DCount() const noexcept
    {
        return submittedTransparentSkinnedMesh3DCount_;
    }
    [[nodiscard]] Core::u32 submittedTransparent3DDrawCount() const noexcept
    {
        return submittedTransparent3DDrawCount_;
    }
    [[nodiscard]] Core::u64 submittedTransparent3DSortOrderChecksum() const noexcept
    {
        return submittedTransparent3DSortOrderChecksum_;
    }
    [[nodiscard]] bool transparent3DSortOrderStable() const noexcept
    {
        return transparent3DSortOrderStable_;
    }
    void recordEnvironmentMapUpload(const Render::EnvironmentMapUploadDesc& desc) noexcept
    {
        ++environmentMapsUploaded_;
        environmentDiffuseFaceSize_ = desc.diffuseFaceSize;
        environmentSpecularFaceSize_ = desc.specularFaceSize;
        environmentSpecularMipCount_ = desc.specularMipCount;
        environmentBrdfWidth_ = desc.brdfWidth;
        environmentBrdfHeight_ = desc.brdfHeight;
    }
    void recordImageBasedLightingBinding() noexcept { ++imageBasedLightingBindings_; }
    void recordImageBasedLightingClear() noexcept { ++imageBasedLightingClears_; }
    void recordEnvironmentMapRetirement() noexcept { ++environmentMapRetirements_; }
    [[nodiscard]] Core::u64 environmentMapsUploaded() const noexcept
    {
        return environmentMapsUploaded_;
    }
    [[nodiscard]] Core::u64 imageBasedLightingBindings() const noexcept
    {
        return imageBasedLightingBindings_;
    }
    [[nodiscard]] Core::u64 imageBasedLightingClears() const noexcept
    {
        return imageBasedLightingClears_;
    }
    [[nodiscard]] Core::u64 environmentMapRetirements() const noexcept
    {
        return environmentMapRetirements_;
    }
    [[nodiscard]] Core::u16 environmentDiffuseFaceSize() const noexcept
    {
        return environmentDiffuseFaceSize_;
    }
    [[nodiscard]] Core::u16 environmentSpecularFaceSize() const noexcept
    {
        return environmentSpecularFaceSize_;
    }
    [[nodiscard]] Core::u16 environmentSpecularMipCount() const noexcept
    {
        return environmentSpecularMipCount_;
    }
    [[nodiscard]] Core::u16 environmentBrdfWidth() const noexcept { return environmentBrdfWidth_; }
    [[nodiscard]] Core::u16 environmentBrdfHeight() const noexcept { return environmentBrdfHeight_; }

  private:
    [[nodiscard]] static Core::u64 fingerprintSkinnedPose(std::span<const float> palette) noexcept
    {
        constexpr Core::u64 OffsetBasis = 14'695'981'039'346'656'037ULL;
        constexpr Core::u64 Prime = 1'099'511'628'211ULL;
        Core::u64 fingerprint = OffsetBasis;
        const auto bytes = std::as_bytes(palette);
        for (const std::byte value : bytes)
        {
            fingerprint ^= static_cast<Core::u64>(std::to_integer<unsigned char>(value));
            fingerprint *= Prime;
        }
        return fingerprint;
    }

    Render::IRenderDevice* device_ = nullptr;
    bool captureNextPresent_ = false;
    bool hasLastCapture_ = false;
    Render::Rgba8FrameCapture lastCapture_{};
    Core::u64 submittedLightingFrames_ = 0;
    Core::u32 directionalLightCount_ = 0;
    Core::u32 cascadedDirectionalShadowCount_ = 0;
    Core::u32 cascadedDirectionalShadowCascadeCount_ = 0;
    std::optional<Render::Mesh3DSpotLightShadow> spotLightShadow_{};
    std::optional<Render::Mesh3DPointLightShadow> pointLightShadow_{};
    Core::u32 pointLight3DCount_ = 0;
    Core::u32 spotLight3DCount_ = 0;
    float submittedCameraAspectRatio_ = 0.0F;
    Core::u64 cameraAspectChanges_ = 0;
    Core::u64 tangentMeshesUploaded_ = 0;
    Core::u64 skinnedMeshesUploaded_ = 0;
    Core::u32 uploadedSkinnedJointCount_ = 0;
    Core::u64 submittedSkinnedMesh3DFrames_ = 0;
    Core::u32 submittedSkinnedMesh3DCount_ = 0;
    Core::u32 visibleSkinnedMesh3DCount_ = 0;
    Core::u32 submittedSkinnedPaletteJointCount_ = 0;
    Core::u64 firstSubmittedSkinnedPoseFingerprint_ = 0;
    Core::u64 submittedSkinnedPoseFingerprint_ = 0;
    Core::u64 submittedSkinnedPoseFingerprintChanges_ = 0;
    Core::u64 submittedTransparent3DFrames_ = 0;
    Core::u32 submittedTransparentStaticMesh3DCount_ = 0;
    Core::u32 submittedTransparentSkinnedMesh3DCount_ = 0;
    Core::u32 submittedTransparent3DDrawCount_ = 0;
    Core::u64 submittedTransparent3DSortOrderChecksum_ = 0;
    bool transparent3DSortOrderStable_ = true;
    Core::u64 environmentMapsUploaded_ = 0;
    Core::u64 imageBasedLightingBindings_ = 0;
    Core::u64 imageBasedLightingClears_ = 0;
    Core::u64 environmentMapRetirements_ = 0;
    Core::u16 environmentDiffuseFaceSize_ = 0;
    Core::u16 environmentSpecularFaceSize_ = 0;
    Core::u16 environmentSpecularMipCount_ = 0;
    Core::u16 environmentBrdfWidth_ = 0;
    Core::u16 environmentBrdfHeight_ = 0;
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
        auto environmentMap = inner_->createEnvironmentMap(desc);
        if (environmentMap && capture_ != nullptr)
        {
            capture_->recordEnvironmentMapUpload(desc);
        }
        return environmentMap;
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
        auto status = inner_->retireEnvironmentMap(environmentMap, completionPin);
        if (status && capture_ != nullptr)
        {
            capture_->recordEnvironmentMapRetirement();
        }
        return status;
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
    [[nodiscard]] Core::Result<Render::GpuMeshId>
    createSkinnedMesh(const Render::SkinnedMeshUploadDesc& desc) override
    {
        auto mesh = inner_->createSkinnedMesh(desc);
        if (mesh && capture_ != nullptr)
        {
            capture_->recordSkinnedMeshUpload(desc);
        }
        return mesh;
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
        auto status = inner_->setMesh3DImageBasedLighting(lighting);
        if (status && capture_ != nullptr)
        {
            capture_->recordImageBasedLightingBinding();
        }
        return status;
    }
    [[nodiscard]] Core::Status clearMesh3DImageBasedLighting() noexcept override
    {
        auto status = inner_->clearMesh3DImageBasedLighting();
        if (status && capture_ != nullptr)
        {
            capture_->recordImageBasedLightingClear();
        }
        return status;
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
