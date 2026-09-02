#pragma once

#include "EditorIconAtlas.hpp"

#include <tina/render/FramePin.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/Texture2DFrameResourceResolver.hpp>

#include <optional>

namespace Tina::EditorApp::WorkspaceInternal {

class EditorIconResources final {
  public:
    EditorIconResources() noexcept = default;
    EditorIconResources(const EditorIconResources&) = delete;
    EditorIconResources& operator=(const EditorIconResources&) = delete;
    ~EditorIconResources() noexcept;

    [[nodiscard]] Core::Status initialize(Render::IRenderDevice& device);
    void release() noexcept;

    [[nodiscard]] Render::Texture2DFrameResourceResolver resolver() noexcept;
    [[nodiscard]] bool initialized() const noexcept { return texture_.hasValue(); }
    [[nodiscard]] Core::u32 frameBorrowCount() const noexcept
    {
        return frameBorrowCount_;
    }

  private:
    [[nodiscard]] static Core::Result<
        std::optional<Render::Texture2DFrameResourceResolution>>
    resolve(void* userData, Core::AssetId asset,
            Render::FrameResourceSink& sink) noexcept;
    static void releaseFrameBorrow(void* userData) noexcept;

    Render::IRenderDevice* device_ = nullptr;
    Render::GpuTextureId texture_{};
    Core::u32 bindingKey_ = 0;
    Core::u32 frameBorrowCount_ = 0;
};

} // namespace Tina::EditorApp::WorkspaceInternal
