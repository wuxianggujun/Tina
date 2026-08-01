#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <algorithm>
#include <memory>
#include <utility>

namespace Tina::SampleUI {

struct ShowcaseRenderEvidence final {
    Core::u64 submittedFrames = 0;
    Core::u64 submittedImageFrames = 0;
    Core::u64 submittedImageFreeFrames = 0;
    Core::u32 lastImageQuadCommands = 0;
    Core::u32 maxImageQuadCommands = 0;
    Core::u32 lastImageBatches = 0;
    Core::u32 maxImageBatches = 0;
    Core::u32 maxUniqueImageResources = 0;
    Core::u64 lastPaintOrderChecksum = 0;
    bool sawLinearSampling = false;
    bool sawNearestSampling = false;
};

class ShowcaseRenderDeviceAccess final {
  public:
    void set(Render::IRenderDevice* device) noexcept
    {
        device_ = device;
    }
    [[nodiscard]] Render::IRenderDevice* get() const noexcept
    {
        return device_;
    }
    [[nodiscard]] const ShowcaseRenderEvidence& evidence() const noexcept
    {
        return evidence_;
    }

    void noteSubmittedFrame(const Render::RenderFrame& frame) noexcept
    {
        const Render::UIDisplayListView displayList = frame.primaryWindowUIDisplayList;
        const auto& statistics = displayList.statistics();
        ++evidence_.submittedFrames;
        evidence_.lastImageQuadCommands = statistics.imageQuadCommandCount;
        evidence_.maxImageQuadCommands = (std::max)(evidence_.maxImageQuadCommands, statistics.imageQuadCommandCount);
        evidence_.lastPaintOrderChecksum = displayList.paintOrderChecksum();

        Core::u32 imageBatchCount = 0;
        for (const Render::UIDrawBatch& batch : displayList.batches()) {
            if (batch.kind == Render::UIDrawCommandKind::ImageQuad) {
                ++imageBatchCount;
            }
        }
        evidence_.lastImageBatches = imageBatchCount;
        evidence_.maxImageBatches = (std::max)(evidence_.maxImageBatches, imageBatchCount);

        Core::u32 uniqueResourceCount = 0;
        const auto commands = displayList.commands();
        for (Core::usize index = 0; index < commands.size(); ++index) {
            const Render::UIDrawCommand& command = commands[index];
            if (command.kind != Render::UIDrawCommandKind::ImageQuad) {
                continue;
            }
            evidence_.sawLinearSampling |= command.sampling == Render::UITextureSampling::Linear;
            evidence_.sawNearestSampling |= command.sampling == Render::UITextureSampling::Nearest;
            bool firstUse = true;
            for (Core::usize previous = 0; previous < index; ++previous) {
                if (commands[previous].kind == Render::UIDrawCommandKind::ImageQuad &&
                    commands[previous].resourceOrdinal == command.resourceOrdinal) {
                    firstUse = false;
                    break;
                }
            }
            uniqueResourceCount += firstUse ? 1U : 0U;
        }
        evidence_.maxUniqueImageResources = (std::max)(evidence_.maxUniqueImageResources, uniqueResourceCount);
        if (statistics.imageQuadCommandCount != 0) {
            ++evidence_.submittedImageFrames;
        } else {
            ++evidence_.submittedImageFreeFrames;
        }
    }

  private:
    Render::IRenderDevice* device_ = nullptr;
    ShowcaseRenderEvidence evidence_{};
};

class ShowcaseRenderDevice final : public Render::IRenderDevice {
  public:
    ShowcaseRenderDevice(std::unique_ptr<Render::IRenderDevice> inner, ShowcaseRenderDeviceAccess& access) noexcept
        : inner_(std::move(inner)), access_(&access)
    {
        access_->set(this);
    }

    ~ShowcaseRenderDevice() override
    {
        if (access_ != nullptr && access_->get() == this) {
            access_->set(nullptr);
        }
    }

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame& frame) override
    {
        auto submission = inner_->submitFrame(frame);
        if (submission && submission->requiresPresent() && access_ != nullptr) {
            access_->noteSubmittedFrame(frame);
        }
        return submission;
    }

    [[nodiscard]] Core::Status present() override
    {
        return inner_->present();
    }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return inner_->statistics();
    }
    void shutdown() noexcept override
    {
        inner_->shutdown();
    }

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
    [[nodiscard]] Core::Status setTexture2DBinding(Core::u32 bindingKey, Render::GpuTextureId texture) noexcept override
    {
        return inner_->setTexture2DBinding(bindingKey, texture);
    }
    [[nodiscard]] Core::Result<Render::Rgba8FrameCapture> capturePrimaryFrameRgba8() override
    {
        return inner_->capturePrimaryFrameRgba8();
    }

  private:
    std::unique_ptr<Render::IRenderDevice> inner_;
    ShowcaseRenderDeviceAccess* access_ = nullptr;
};

[[nodiscard]] inline Core::Result<std::unique_ptr<Render::IRenderDevice>>
wrapShowcaseRenderDevice(std::unique_ptr<Render::IRenderDevice> device, ShowcaseRenderDeviceAccess& access)
{
    std::unique_ptr<Render::IRenderDevice> wrapped = std::make_unique<ShowcaseRenderDevice>(std::move(device), access);
    return wrapped;
}

} // namespace Tina::SampleUI
