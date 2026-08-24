#pragma once

#include <tina/render/Texture2DFrameResourceResolver.hpp>

#include <optional>

namespace Tina::EditorApp::WorkspaceInternal {

// Root-scoped Editor image resolver. The icon atlas is an exact, private
// Editor asset; all other texture AssetIds are delegated to the live Catalog
// binding registry without exposing either owner through the UI API.
class EditorCompositeImageResolver final {
  public:
    EditorCompositeImageResolver() noexcept = default;

    [[nodiscard]] Render::Texture2DFrameResourceResolver resolver() noexcept;

    void setIconResolver(Render::Texture2DFrameResourceResolver resolver) noexcept
    {
        iconResolver_ = resolver;
    }

    void setCatalogTextureResolver(
        Render::Texture2DFrameResourceResolver resolver) noexcept
    {
        catalogTextureResolver_ = resolver;
    }

    void clearCatalogTextureResolver() noexcept
    {
        catalogTextureResolver_ = {};
    }

  private:
    [[nodiscard]] static Core::Result<
        std::optional<Render::Texture2DFrameResourceResolution>>
    resolve(void* userData, Core::AssetId asset,
            Render::FrameResourceSink& sink) noexcept;

    Render::Texture2DFrameResourceResolver iconResolver_{};
    Render::Texture2DFrameResourceResolver catalogTextureResolver_{};
};

} // namespace Tina::EditorApp::WorkspaceInternal
