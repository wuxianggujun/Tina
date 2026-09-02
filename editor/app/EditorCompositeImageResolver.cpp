#include "EditorCompositeImageResolver.hpp"

#include "EditorIconAtlas.hpp"

#include <utility>

namespace Tina::EditorApp::WorkspaceInternal {

Render::Texture2DFrameResourceResolver
EditorCompositeImageResolver::resolver() noexcept
{
    return {
        .userData = this,
        .resolve = &EditorCompositeImageResolver::resolve,
    };
}

Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
EditorCompositeImageResolver::resolve(void* userData, Core::AssetId asset,
                                      Render::FrameResourceSink& sink) noexcept
{
    auto* resolver = static_cast<EditorCompositeImageResolver*>(userData);
    if (resolver == nullptr) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor composite image resolver received null user data");
    }

    // Keep the private atlas namespace exact: an icon id must never fall
    // through to a Catalog asset that happens to reuse the same id.
    if (asset == editorIconAtlasAssetId()) {
        if (!resolver->iconResolver_.hasValue()) {
            return std::optional<Render::Texture2DFrameResourceResolution>{};
        }
        auto icon = resolver->iconResolver_.resolve(
            resolver->iconResolver_.userData, asset, sink);
        if (!icon) {
            return Core::failure(std::move(icon.error()));
        }
        return icon;
    }

    if (!resolver->catalogTextureResolver_.hasValue()) {
        return std::optional<Render::Texture2DFrameResourceResolution>{};
    }
    auto texture = resolver->catalogTextureResolver_.resolve(
        resolver->catalogTextureResolver_.userData, asset, sink);
    if (!texture) {
        return Core::failure(std::move(texture.error()));
    }
    return texture;
}

} // namespace Tina::EditorApp::WorkspaceInternal
