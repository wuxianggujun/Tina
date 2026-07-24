#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory>
#include <optional>
#include <utility>

namespace Tina::Sample3D {

// Product-gate capture: observes live IRenderDevice for mesh/texture bind without
// hand-rolling EngineCompositionFactories. Desktop::CreateEngine wrap installs it.
class DeviceCapture final {
  public:
    void set(Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Render::IRenderDevice* device_ = nullptr;
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
        return inner_->submitFrame(frame);
    }
    [[nodiscard]] Core::Status present() override { return inner_->present(); }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return inner_->statistics();
    }
    [[nodiscard]] std::optional<Core::u64> lastPresentFrameToken() const noexcept override
    {
        return inner_->lastPresentFrameToken();
    }
    void shutdown() noexcept override { inner_->shutdown(); }
    [[nodiscard]] Core::Result<Render::GpuTextureId>
    createTexture2DRgba8(const Render::Texture2DUploadDesc& desc) override
    {
        return inner_->createTexture2DRgba8(desc);
    }
    [[nodiscard]] Core::Status destroyTexture2D(Render::GpuTextureId texture) noexcept override
    {
        return inner_->destroyTexture2D(texture);
    }
    [[nodiscard]] Core::Status setSprite2DTextureBinding(Core::u32 spriteKey,
                                                         Render::GpuTextureId texture) noexcept override
    {
        return inner_->setSprite2DTextureBinding(spriteKey, texture);
    }
    [[nodiscard]] Core::Result<Render::Rgba8FrameCapture> capturePrimaryFrameRgba8() override
    {
        return inner_->capturePrimaryFrameRgba8();
    }
    [[nodiscard]] Core::Result<Render::GpuMeshId>
    createStaticMeshP3N3UV2(const Render::StaticMeshUploadDesc& desc) override
    {
        return inner_->createStaticMeshP3N3UV2(desc);
    }
    [[nodiscard]] Core::Status destroyStaticMesh(Render::GpuMeshId mesh) noexcept override
    {
        return inner_->destroyStaticMesh(mesh);
    }
    [[nodiscard]] Core::Status setMesh3DBinding(Core::u32 meshKey, Render::GpuMeshId mesh) noexcept override
    {
        return inner_->setMesh3DBinding(meshKey, mesh);
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
