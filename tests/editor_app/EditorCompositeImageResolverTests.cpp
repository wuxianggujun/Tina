#include <gtest/gtest.h>

#include "EditorCompositeImageResolver.hpp"
#include "EditorIconAtlas.hpp"

#include <tina/render/FramePin.hpp>

#include <optional>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] Core::AssetId assetId(u8 discriminator) noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(discriminator);
    return *Core::AssetId::fromBytes(bytes);
}

class UnusedFrameResourceSink final : public Render::FrameResourceSink {
  public:
    [[nodiscard]] Core::Result<Render::FrameResourceRef> intern(
        Render::FrameResourceDescriptor,
        Render::FramePin&&) noexcept override
    {
        ++internCount;
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Unexpected frame resource intern");
    }

    [[nodiscard]] u32 resourceCount() const noexcept override
    {
        return 0;
    }

    u32 internCount = 0;
};

struct ResolverState final {
    u32 callCount = 0;
    Core::AssetId lastAsset{};
    u32 pixelWidth = 0;
    u32 pixelHeight = 0;
    bool available = true;
    bool fail = false;

    [[nodiscard]] Render::Texture2DFrameResourceResolver resolver() noexcept
    {
        return {
            .userData = this,
            .resolve = &ResolverState::resolve,
        };
    }

    [[nodiscard]] static Core::Result<std::optional<
        Render::Texture2DFrameResourceResolution>>
    resolve(void* userData, Core::AssetId asset,
            Render::FrameResourceSink&) noexcept
    {
        auto& state = *static_cast<ResolverState*>(userData);
        ++state.callCount;
        state.lastAsset = asset;
        if (state.fail) {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "Delegated image resolver failure");
        }
        if (!state.available) {
            return std::optional<
                Render::Texture2DFrameResourceResolution>{};
        }
        return std::optional<Render::Texture2DFrameResourceResolution>{
            Render::Texture2DFrameResourceResolution{
                .pixelWidth = state.pixelWidth,
                .pixelHeight = state.pixelHeight,
            }};
    }
};

TEST(EditorCompositeImageResolverTest,
     RoutesAtlasExactlyAndDelegatesOtherAssetsToCatalog)
{
    EditorCompositeImageResolver composite;
    ResolverState catalog{
        .pixelWidth = 128,
        .pixelHeight = 64,
    };
    composite.setCatalogTextureResolver(catalog.resolver());
    const Render::Texture2DFrameResourceResolver resolver =
        composite.resolver();
    UnusedFrameResourceSink sink;

    auto missingIcon = resolver.resolve(
        resolver.userData, editorIconAtlasAssetId(), sink);
    ASSERT_TRUE(missingIcon.has_value())
        << (missingIcon ? "" : missingIcon.error().message);
    EXPECT_FALSE(missingIcon->has_value());
    EXPECT_EQ(catalog.callCount, 0U);

    ResolverState icon{
        .pixelWidth = EditorIconAtlasWidth,
        .pixelHeight = EditorIconAtlasHeight,
    };
    composite.setIconResolver(icon.resolver());
    auto resolvedIcon = resolver.resolve(
        resolver.userData, editorIconAtlasAssetId(), sink);
    ASSERT_TRUE(resolvedIcon.has_value())
        << (resolvedIcon ? "" : resolvedIcon.error().message);
    ASSERT_TRUE(resolvedIcon->has_value());
    EXPECT_EQ((*resolvedIcon)->pixelWidth, EditorIconAtlasWidth);
    EXPECT_EQ((*resolvedIcon)->pixelHeight, EditorIconAtlasHeight);
    EXPECT_EQ(icon.callCount, 1U);
    EXPECT_EQ(icon.lastAsset, editorIconAtlasAssetId());
    EXPECT_EQ(catalog.callCount, 0U);

    const Core::AssetId textureAsset = assetId(0x42);
    auto resolvedTexture = resolver.resolve(
        resolver.userData, textureAsset, sink);
    ASSERT_TRUE(resolvedTexture.has_value())
        << (resolvedTexture ? "" : resolvedTexture.error().message);
    ASSERT_TRUE(resolvedTexture->has_value());
    EXPECT_EQ((*resolvedTexture)->pixelWidth, 128U);
    EXPECT_EQ((*resolvedTexture)->pixelHeight, 64U);
    EXPECT_EQ(icon.callCount, 1U);
    EXPECT_EQ(catalog.callCount, 1U);
    EXPECT_EQ(catalog.lastAsset, textureAsset);
    EXPECT_EQ(sink.internCount, 0U);
}

TEST(EditorCompositeImageResolverTest,
     PreservesMissingAndFailureResultsWithoutCrossFallback)
{
    EditorCompositeImageResolver composite;
    ResolverState icon;
    ResolverState catalog;
    composite.setIconResolver(icon.resolver());
    composite.setCatalogTextureResolver(catalog.resolver());
    Render::Texture2DFrameResourceResolver resolver = composite.resolver();
    UnusedFrameResourceSink sink;

    icon.fail = true;
    auto iconFailure = resolver.resolve(
        resolver.userData, editorIconAtlasAssetId(), sink);
    ASSERT_FALSE(iconFailure.has_value());
    EXPECT_EQ(iconFailure.error().code, Core::CoreErrorCode::Internal);
    EXPECT_EQ(icon.callCount, 1U);
    EXPECT_EQ(catalog.callCount, 0U);

    icon.fail = false;
    icon.available = false;
    auto missingIcon = resolver.resolve(
        resolver.userData, editorIconAtlasAssetId(), sink);
    ASSERT_TRUE(missingIcon.has_value())
        << (missingIcon ? "" : missingIcon.error().message);
    EXPECT_FALSE(missingIcon->has_value());
    EXPECT_EQ(icon.callCount, 2U);
    EXPECT_EQ(catalog.callCount, 0U);

    catalog.fail = true;
    auto catalogFailure = resolver.resolve(
        resolver.userData, assetId(0x51), sink);
    ASSERT_FALSE(catalogFailure.has_value());
    EXPECT_EQ(catalogFailure.error().code, Core::CoreErrorCode::Internal);
    EXPECT_EQ(icon.callCount, 2U);
    EXPECT_EQ(catalog.callCount, 1U);

    composite.clearCatalogTextureResolver();
    auto missingCatalog = resolver.resolve(
        resolver.userData, assetId(0x52), sink);
    ASSERT_TRUE(missingCatalog.has_value())
        << (missingCatalog ? "" : missingCatalog.error().message);
    EXPECT_FALSE(missingCatalog->has_value());
    EXPECT_EQ(catalog.callCount, 1U);

    resolver.userData = nullptr;
    auto invalidUserData = resolver.resolve(
        resolver.userData, editorIconAtlasAssetId(), sink);
    ASSERT_FALSE(invalidUserData.has_value());
    EXPECT_EQ(invalidUserData.error().code,
              Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(sink.internCount, 0U);
}

} // namespace
} // namespace Tina::EditorApp::WorkspaceInternal
