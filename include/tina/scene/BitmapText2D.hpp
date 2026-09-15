#pragma once

#include <tina/render/RenderScene.hpp>
#include <tina/render/Texture2DFrameResourceResolver.hpp>
#include <tina/text/BitmapFont.hpp>
#include <memory>

namespace Tina::Scene {

struct BitmapText2DDraw final {
    // Top-left line-box anchor in world/grid coordinates; glyphs stay upright
    // in an isometric scene and share the anchor's depth.
    float x = 0, y = 0, elevation = 0;
    float rotationRadians = 0;
    float pixelsPerMeter = 100;
    Core::u64 firstStableKey = 1;
    Core::i16 sortingLayer = 0;
    Core::i32 orderInLayer = 0;
    Core::ColorTransform colorTransform{};
    Core::BlendMode blendMode = Core::BlendMode::PremultipliedAlpha;
};

// One layout owner, not one Entity per character. Font metrics are immutable and
// shared, texture IDs are persistent weak identities. Frame pins come solely
// from the resolver. setText is transactional; extract does not allocate.
class BitmapText2D final {
  public:
    [[nodiscard]] static Core::Result<BitmapText2D> Create(
        std::shared_ptr<const Text::BitmapFont> font, std::span<const Core::AssetId> pages);
    [[nodiscard]] Core::Status setText(std::string_view utf8, Text::BitmapTextOptions options = {});
    [[nodiscard]] const Text::BitmapTextLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] Core::Status extract(Render::RenderSceneWriter& writer, Render::FrameResourceSink& resources,
        Render::Texture2DFrameResourceResolver resolver, BitmapText2DDraw draw = {}) const noexcept;
  private:
    BitmapText2D(std::shared_ptr<const Text::BitmapFont> font, std::vector<Core::AssetId> pages)
        : font_(std::move(font)), pages_(std::move(pages)) {}
    std::shared_ptr<const Text::BitmapFont> font_;
    std::vector<Core::AssetId> pages_;
    Text::BitmapTextLayout layout_;
    float layoutScale_ = 1;
};
}
