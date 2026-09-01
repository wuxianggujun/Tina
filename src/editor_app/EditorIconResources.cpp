#include "EditorIconResources.hpp"

#include <tina/render/RenderErrors.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <utility>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

#include "EditorIconAtlas.generated.inc"

using EditorIconAtlasPixels =
    std::array<std::byte, EditorIconAtlasWidth * EditorIconAtlasHeight * 4U>;

[[nodiscard]] EditorIconAtlasPixels expandEditorIconAtlasRgba8() noexcept
{
    EditorIconAtlasPixels pixels{};
    for (Core::usize pixelIndex = 0; pixelIndex < EditorIconAtlasAlpha.size();
         ++pixelIndex) {
        const Core::usize offset = pixelIndex * 4U;
        pixels[offset] = std::byte{255};
        pixels[offset + 1U] = std::byte{255};
        pixels[offset + 2U] = std::byte{255};
        pixels[offset + 3U] = static_cast<std::byte>(EditorIconAtlasAlpha[pixelIndex]);
    }
    return pixels;
}

} // namespace

EditorIconResources::~EditorIconResources() noexcept
{
    release();
    if (frameBorrowCount_ != 0U) {
        std::terminate();
    }
}

Core::Status EditorIconResources::initialize(Render::IRenderDevice& device)
{
    if (initialized() || bindingKey_ != 0U || device_ != nullptr) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor icon atlas is already initialized");
    }
    static const EditorIconAtlasPixels AtlasPixels =
        expandEditorIconAtlasRgba8();
    const std::array<Render::Texture2DUploadLevel, 1> levels{
        Render::Texture2DUploadLevel{.width = static_cast<Core::u16>(EditorIconAtlasWidth),
                                     .height = static_cast<Core::u16>(EditorIconAtlasHeight),
                                     .bytes = AtlasPixels}};
    auto uploaded = device.createTexture2D(Render::Texture2DUploadDesc{.levels = levels});
    if (!uploaded) {
        return Core::failure(std::move(uploaded.error()));
    }
    auto binding = device.createTexture2DBinding(*uploaded);
    if (!binding) {
        (void)device.destroyTexture2D(*uploaded);
        return Core::failure(std::move(binding.error()));
    }
    device_ = &device;
    texture_ = *uploaded;
    bindingKey_ = *binding;
    return Core::success();
}

void EditorIconResources::release() noexcept
{
    if (device_ != nullptr && texture_) {
        (void)device_->destroyTexture2D(texture_);
    }
    texture_ = {};
    bindingKey_ = 0U;
    device_ = nullptr;
}

Render::Texture2DFrameResourceResolver EditorIconResources::resolver() noexcept
{
    return {
        .userData = this,
        .resolve = &EditorIconResources::resolve,
    };
}

Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
EditorIconResources::resolve(void* userData, Core::AssetId asset,
                             Render::FrameResourceSink& sink) noexcept
{
    auto* resources = static_cast<EditorIconResources*>(userData);
    if (resources == nullptr) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor icon resolver received null user data");
    }
    if (asset != editorIconAtlasAssetId() || resources->bindingKey_ == 0U) {
        return std::optional<Render::Texture2DFrameResourceResolution>{};
    }

    ++resources->frameBorrowCount_;
    Render::FramePin pin{
        Render::FramePinKind::Custom,
        resources->bindingKey_,
        resources,
        &EditorIconResources::releaseFrameBorrow,
    };
    auto frameResource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Texture2D,
            .deviceBindingKey = resources->bindingKey_,
        },
        std::move(pin));
    if (!frameResource) {
        return Core::failure(std::move(frameResource.error()));
    }
    return std::optional<Render::Texture2DFrameResourceResolution>{
        Render::Texture2DFrameResourceResolution{
            .resource = *frameResource,
            .pixelWidth = EditorIconAtlasWidth,
            .pixelHeight = EditorIconAtlasHeight,
        }};
}

void EditorIconResources::releaseFrameBorrow(void* userData) noexcept
{
    auto* resources = static_cast<EditorIconResources*>(userData);
    if (resources == nullptr || resources->frameBorrowCount_ == 0U) {
        std::terminate();
    }
    --resources->frameBorrowCount_;
}

} // namespace Tina::EditorApp::WorkspaceInternal
