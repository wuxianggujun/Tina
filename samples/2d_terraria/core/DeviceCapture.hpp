#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory>
#include <utility>

namespace Tina::SampleTerraria {

// Observes the live IRenderDevice so onEnter can upload the atlas and bind it.
// Desktop::CreateEngine hands the real device to a wrap hook and keeps ownership
// of the result, which is the only place a sample can see the device at all.
class DeviceCapture final {
  public:
    void set(Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Render::IRenderDevice* device_ = nullptr;
};

// Forwards the four pure virtuals plus the Texture2D calls this sample uses.
// Every other IRenderDevice hook has a base implementation, so leaving them
// alone keeps the shim to the surface the sample actually touches.
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
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override { return inner_->statistics(); }
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
    [[nodiscard]] Core::Status setTexture2DBinding(Core::u32 deviceBindingKey,
                                                  Render::GpuTextureId texture) noexcept override
    {
        return inner_->setTexture2DBinding(deviceBindingKey, texture);
    }
    [[nodiscard]] Core::Status drainGpuRetirements() noexcept override { return inner_->drainGpuRetirements(); }

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

} // namespace Tina::SampleTerraria
