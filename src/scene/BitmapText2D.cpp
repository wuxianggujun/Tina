#include <tina/scene/BitmapText2D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Tina::Scene {
Core::Result<BitmapText2D> BitmapText2D::Create(std::shared_ptr<const Text::BitmapFont> font,
    std::span<const Core::AssetId> pages)
try {
    if (!font || pages.size() != font->descriptor().pages.size() ||
        std::any_of(pages.begin(), pages.end(), [](auto id) { return !id; }))
        return Core::failure(SceneErrorCode::InvalidComponent, "BitmapText2D requires font and complete page IDs");
    return BitmapText2D{std::move(font), {pages.begin(), pages.end()}};
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "BitmapText2D allocation failed"); }

Core::Status BitmapText2D::setText(std::string_view utf8, Text::BitmapTextOptions options) {
    auto candidate = Text::layoutBitmapText(*font_, utf8, options);
    if (!candidate) return Core::failure(candidate.error());
    layout_ = std::move(*candidate);
    layoutScale_ = options.scale;
    return Core::success();
}

Core::Status BitmapText2D::extract(Render::RenderSceneWriter& writer, Render::FrameResourceSink& resources,
    Render::Texture2DFrameResourceResolver resolver, BitmapText2DDraw draw) const noexcept
{
    if (!std::isfinite(draw.x) || !std::isfinite(draw.y) || !std::isfinite(draw.elevation) ||
        !std::isfinite(draw.rotationRadians) || !std::isfinite(draw.pixelsPerMeter) || draw.pixelsPerMeter <= 0 ||
        !Core::isValidColorTransform(draw.colorTransform) || !Core::isSupportedBlendMode(draw.blendMode) ||
        !draw.firstStableKey ||
        (!layout_.glyphs.empty() && layout_.glyphs.size() - 1 > (std::numeric_limits<Core::u64>::max)() - draw.firstStableKey))
        return Core::failure(SceneErrorCode::InvalidComponent, "Invalid BitmapText2D draw parameters");
    if (layout_.glyphs.empty()) return Core::success();
    if (!resolver.hasValue()) return Core::failure(SceneErrorCode::UnresolvedSprite, "BitmapText2D requires page resolver");
    auto camera = writer.camera2D();
    if (!camera) return Core::failure(camera.error());
    const Render::Sprite2DProjection projection{camera->isometricProjection};
    const auto anchor = projection.projectPoint({draw.x, draw.y, draw.elevation});
    const auto depth = projection.sortDepth({draw.x, draw.y, draw.elevation});
    if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y) || !std::isfinite(depth))
        return Core::failure(SceneErrorCode::InvalidComponent, "BitmapText2D projection overflow");
    std::array<Render::FrameResourceRef, Text::BitmapFont::MaxPages> textures{};
    const auto& descriptor = font_->descriptor();
    for (Core::usize index = 0; index < pages_.size(); ++index) {
        auto texture = resolver.resolve(resolver.userData, pages_[index], resources);
        if (!texture) return Core::failure(texture.error());
        if (!*texture || !(**texture).resource || (**texture).pixelWidth != descriptor.pages[index].width ||
            (**texture).pixelHeight != descriptor.pages[index].height)
            return Core::failure(SceneErrorCode::UnresolvedSprite, "BitmapText2D page binding missing or dimensions mismatch");
        textures[index] = (**texture).resource;
    }
    const float cosine = std::cos(draw.rotationRadians), sine = std::sin(draw.rotationRadians);
    const float unit = 1 / draw.pixelsPerMeter;
    const float glyphUnit = unit * layoutScale_;
    const auto makeQuad = [&](const Text::BitmapTextGlyph& placed, const Text::BitmapGlyph& glyph) {
        const float halfWidth = static_cast<float>(glyph.width) * glyphUnit * 0.5F;
        const float halfHeight = static_cast<float>(glyph.height) * glyphUnit * 0.5F;
        const float x = placed.originX * unit + glyph.bearingX * glyphUnit + halfWidth;
        const float y = -placed.baselineY * unit + glyph.bearingY * glyphUnit - halfHeight;
        return Render::Sprite2DQuad{anchor.x + cosine * x - sine * y, anchor.y + sine * x + cosine * y,
            cosine * halfWidth, sine * halfWidth, -sine * halfHeight, cosine * halfHeight};
    };
    // Reject overflow before publishing the first glyph; a writer-capacity error
    // still follows the existing frame extraction transaction contract.
    for (const auto& placed : layout_.glyphs) {
        const auto& glyph = descriptor.glyphs[placed.glyphIndex];
        if (glyph.width && !makeQuad(placed, glyph).isValid())
            return Core::failure(SceneErrorCode::InvalidComponent, "BitmapText2D geometry overflow");
    }
    for (Core::usize index = 0; index < layout_.glyphs.size(); ++index) {
        const auto& placed = layout_.glyphs[index];
        const auto& glyph = descriptor.glyphs[placed.glyphIndex];
        if (!glyph.width) continue;
        const auto& page = descriptor.pages[glyph.page];
        auto color = draw.colorTransform;
        if (page.imageKind == Text::BitmapFontImageKind::Coverage) {
            color.add.red += color.multiply.red; color.add.green += color.multiply.green; color.add.blue += color.multiply.blue;
            color.multiply.red = color.multiply.green = color.multiply.blue = 0;
        }
        if (auto status = writer.addSprite2D({.texture = textures[glyph.page], .stableEntityKey = draw.firstStableKey + index,
            .quad = makeQuad(placed, glyph), .u0 = static_cast<float>(glyph.x) / static_cast<float>(page.width), .v0 = static_cast<float>(glyph.y) / static_cast<float>(page.height),
            .u1 = static_cast<float>(glyph.x + glyph.width) / static_cast<float>(page.width), .v1 = static_cast<float>(glyph.y + glyph.height) / static_cast<float>(page.height),
            .sortingLayer = draw.sortingLayer, .sortDepth = depth, .orderInLayer = draw.orderInLayer, .colorTransform = color,
            .blendMode = draw.blendMode}); !status) return status;
    }
    return Core::success();
}
}
