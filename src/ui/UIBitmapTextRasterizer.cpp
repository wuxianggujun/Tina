#include <tina/ui/text/UIBitmapTextRasterizer.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/core/text/Utf8.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Tina::UI {
namespace {
class BitmapTextRasterizer final : public IUITextRasterizer {
  public:
    BitmapTextRasterizer(std::shared_ptr<const Text::BitmapFontAtlas> source, UITextRasterizerCapacity capacity,
        std::pmr::memory_resource& resource)
        : source_(std::move(source)), capacity_(capacity), faces_(&resource), layout_(&resource), scalars_(&resource),
          glyphs_(&resource), pixels_(&resource), offsets_(&resource)
    {
        faces_.reserve(capacity.initialFaceCapacity);
        layout_.reserve(capacity.maxGlyphsPerRaster);
        scalars_.reserve(capacity.maxGlyphsPerRaster);
        glyphs_.reserve(capacity.maxGlyphsPerRaster);
        pixels_.resize(capacity.coverageByteCapacity);
        offsets_.resize(source_->font().descriptor().glyphs.size());
    }
    Core::Result<UIFontFaceId> openFace(std::span<const std::byte> bytes, i32 index) override
    try {
        if (!bytes.empty() || index != 0) return Core::failure(UIErrorCode::InvalidFont, "Bitmap rasterizer opens its injected atlas with empty bytes and face 0");
        for (u32 slot = 0; slot < faces_.size(); ++slot) {
            auto& face = faces_[slot];
            if (!face.active && face.generation != (std::numeric_limits<u32>::max)()) {
                ++face.generation; face.active = true; return UIFontFaceId{slot, face.generation};
            }
        }
        if (faces_.size() == (std::numeric_limits<u32>::max)()) return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap face identity exhausted");
        faces_.push_back({1, true});
        return UIFontFaceId{static_cast<u32>(faces_.size() - 1), 1};
    } catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap face allocation failed"); }
      catch (const std::length_error&) { return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap face storage exceeds addressable size"); }

    Core::Status closeFace(UIFontFaceId face) noexcept override {
        if (!valid(face)) return Core::failure(UIErrorCode::InvalidFont, "Invalid bitmap face generation");
        faces_[face.index].active = false;
        return Core::success();
    }
    Core::Status setFallbackChain(std::span<const UIFontFaceId> faces) override {
        if (!faces.empty()) return Core::failure(Core::CoreErrorCode::Unsupported, "Bitmap fonts use their explicit fallback glyph, not an outline face chain");
        return Core::success();
    }
    Core::Status primeGlyphCache(UIFontFaceId, std::span<const std::byte>) override {
        return Core::failure(Core::CoreErrorCode::Unsupported, "Bitmap fonts are not outline cache seeds");
    }
    Core::Result<UITextMetrics> measure(UIFontFaceId face, std::string_view text, UITextStyle style, UITextRasterScale scale) override {
        if (auto status = validateUITextRasterScale(scale); !status) return Core::failure(status.error());
        auto shaped = shape(face, text, style);
        if (!shaped) return Core::failure(shaped.error());
        return shaped->metrics;
    }
    Core::Result<UITextShapeView> shape(UIFontFaceId face, std::string_view text, UITextStyle style) override {
        auto result = shapeLayout(face, text, style);
        if (!result) return Core::failure(result.error());
        return UITextShapeView{metrics(*result), result->baseline, scalars_, result->missingGlyphCount};
    }
    Core::Result<UITextRasterBatch> raster(UIFontFaceId face, std::string_view text, UITextStyle style, UITextRasterScale scale) override {
        if (auto status = validateUITextRasterScale(scale); !status) return Core::failure(status.error());
        auto layout = shapeLayout(face, text, style);
        if (!layout) return Core::failure(layout.error());
        glyphs_.clear();
        std::fill(offsets_.begin(), offsets_.end(), (std::numeric_limits<u32>::max)());
        u32 usedBytes = 0, imageCount = 0;
        const auto& font = source_->font().descriptor();
        const float factor = style.logicalSize / font.nominalSize;
        const auto nominalPixels = static_cast<u32>((std::max)(1.0F, std::ceil(font.nominalSize)));
        for (const auto& placed : layout->glyphs) {
            const auto& glyph = font.glyphs[placed.glyphIndex];
            const auto& page = font.pages[glyph.page];
            if (!glyph.width) continue;
            if (glyphs_.size() >= capacity_.maxGlyphsPerRaster)
                return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap raster glyph budget exhausted");
            auto& offset = offsets_[placed.glyphIndex];
            if (offset == (std::numeric_limits<u32>::max)()) {
                const u64 bytes = static_cast<u64>(glyph.width) * glyph.height * 4;
                if (imageCount >= capacity_.glyphImageCapacity || bytes > pixels_.size() - usedBytes)
                    return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap glyph raster scratch exhausted");
                ++imageCount;
                offset = usedBytes;
                for (u32 y = 0; y < glyph.height; ++y) for (u32 x = 0; x < glyph.width; ++x) {
                    const usize input = (static_cast<usize>(glyph.y + y) * page.width + glyph.x + x) * 4;
                    const auto& source = source_->pages()[glyph.page];
                    const u32 alpha = source[input + 3];
                    for (u32 channel = 0; channel < 3; ++channel)
                        pixels_[usedBytes++] = page.imageKind == Text::BitmapFontImageKind::Coverage ? static_cast<u8>(alpha)
                            : static_cast<u8>((source[input + channel] * alpha + 127) / 255);
                    pixels_[usedBytes++] = static_cast<u8>(alpha);
                }
            }
            glyphs_.push_back({.face = face, .glyphIndex = placed.glyphIndex, .clusterByteBegin = placed.byteBegin,
                .clusterByteEnd = placed.byteEnd, .line = placed.line, .originX = placed.originX,
                .originY = placed.baselineY - layout->baseline, .advance = placed.advance,
                .bearingX = glyph.bearingX * factor, .bearingY = glyph.bearingY * factor,
                .width = glyph.width, .height = glyph.height, .coverageOffset = offset, .coveragePitch = glyph.width * 4,
                .logicalWidth = static_cast<float>(glyph.width) * factor, .logicalHeight = static_cast<float>(glyph.height) * factor,
                .rasterSize = {nominalPixels, nominalPixels},
                .imageKind = page.imageKind == Text::BitmapFontImageKind::Coverage ? UIGlyphImageKind::BitmapCoverage : UIGlyphImageKind::BitmapColor,
                .nominalAdvance = glyph.advance * factor});
        }
        return UITextRasterBatch{metrics(*layout), layout->baseline, glyphs_, scalars_,
            std::span<const u8>{pixels_}.first(usedBytes), layout->missingGlyphCount};
    }
    UITextRasterizerCapacity capacity() const noexcept override { return capacity_; }
  private:
    struct Face { u32 generation; bool active; };
    bool valid(UIFontFaceId face) const noexcept {
        return face.index < faces_.size() && faces_[face.index].active && faces_[face.index].generation == face.generation;
    }
    static UITextMetrics metrics(const Text::BitmapTextMetrics& layout) noexcept {
        return {{layout.width, layout.height}, layout.scalarCount, layout.lineCount};
    }
    Core::Result<Text::BitmapTextLayoutView> shapeLayout(UIFontFaceId face, std::string_view text, UITextStyle style)
    try {
        if (!valid(face)) return Core::failure(UIErrorCode::InvalidFont, "Invalid bitmap face generation");
        if (!std::isfinite(style.logicalSize) || style.logicalSize <= 0 || !std::isfinite(style.lineHeightScale) || style.lineHeightScale <= 0 ||
            !std::isfinite(style.advanceScale) || style.advanceScale <= 0 ||
            style.direction > UITextDirection::RightToLeft || style.pixelSnap > UITextPixelSnap::RunOrigin)
            return Core::failure(UIErrorCode::InvalidText, "Invalid bitmap text style");
        if (style.direction != UITextDirection::Auto && style.direction != UITextDirection::LeftToRight)
            return Core::failure(Core::CoreErrorCode::Unsupported, "Bitmap scalar fonts require LTR layout; use the outline shaper for BiDi");
        const auto& font = source_->font().descriptor();
        const float factor = style.logicalSize / font.nominalSize;
        const float lineHeight = style.logicalSize * style.lineHeightScale;
        const float naturalHeight = font.lineHeight * factor;
        if (!std::isfinite(lineHeight) || !std::isfinite(naturalHeight))
            return Core::failure(UIErrorCode::InvalidText, "Bitmap text line height overflow");
        if (text.size() > capacity_.maxTextBytes)
            return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap text byte budget exhausted");
        const auto scalarCount = Core::countStrictUtf8CodepointsWithoutNul(text);
        if (!scalarCount) return Core::failure(UIErrorCode::InvalidText, "Bitmap text must be strict UTF-8 without NUL");
        // Measurement/shaping follows maxTextBytes, independently of drawable
        // raster/image budgets. Storage grows only when a longer run needs it.
        layout_.resize(*scalarCount);
        scalars_.reserve(*scalarCount);
        auto layout = Text::layoutBitmapTextInto(source_->font(), text, {.scale = factor,
            .extraLineSpacing = lineHeight - naturalHeight,
            .maxScalars = capacity_.maxTextBytes, .maxTextBytes = capacity_.maxTextBytes}, layout_);
        if (!layout) return Core::failure(layout.error());
        // World text measures its natural last line; UI always measures complete
        // style line boxes and centers the font's natural ascent/descent in them.
        const float baseline = std::clamp(layout->baseline + (lineHeight - naturalHeight) * 0.5F, 0.0F, lineHeight);
        layout->baseline = baseline;
        layout->height = static_cast<float>(layout->lineCount) * lineHeight;
        if (!std::isfinite(layout->height)) return Core::failure(UIErrorCode::InvalidText, "Bitmap text height overflow");
        for (usize index = 0; index < layout->glyphs.size(); ++index) {
            layout_[index].baselineY = static_cast<float>(layout_[index].line) * lineHeight + baseline;
        }
        scalars_.clear();
        for (const auto& glyph : layout->glyphs) {
            const float end = glyph.originX + font.glyphs[glyph.glyphIndex].advance * factor;
            scalars_.push_back({.advance = glyph.advance, .visualStartX = end - glyph.advance, .visualEndX = end,
                .clusterByteBegin = glyph.byteBegin, .clusterByteEnd = glyph.byteEnd, .line = glyph.line, .hasVisualPosition = true});
        }
        return layout;
    } catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap shape allocation failed"); }
      catch (const std::length_error&) { return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap shape storage exceeds addressable size"); }
    std::shared_ptr<const Text::BitmapFontAtlas> source_;
    UITextRasterizerCapacity capacity_;
    std::pmr::vector<Face> faces_;
    std::pmr::vector<Text::BitmapTextGlyph> layout_;
    std::pmr::vector<UITextScalarMetrics> scalars_;
    std::pmr::vector<UITextGlyphRaster> glyphs_;
    std::pmr::vector<u8> pixels_;
    std::pmr::vector<u32> offsets_;
};
}
Core::Result<std::unique_ptr<IUITextRasterizer>> createBitmapTextRasterizer(
    std::shared_ptr<const Text::BitmapFontAtlas> source, UITextRasterizerCapacity capacity, std::pmr::memory_resource& resource)
try {
    if (!source) return Core::failure(UIErrorCode::InvalidFont, "Bitmap rasterizer requires a cooked CPU atlas");
    if (auto status = validateUITextRasterizerCapacity(capacity); !status) return Core::failure(status.error());
    const auto& descriptor = source->font().descriptor();
    Core::u64 fontBytes = sizeof(Text::BitmapFontDesc) + descriptor.pages.size() * sizeof(Text::BitmapFontPage) +
        descriptor.glyphs.size() * sizeof(Text::BitmapGlyph) + descriptor.kerning.size() * sizeof(Text::BitmapKerning);
    for (const auto& page : source->pages()) fontBytes += page.size();
    if (fontBytes > capacity.maxFontBytes)
        return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap font exceeds maxFontBytes");
    return std::unique_ptr<IUITextRasterizer>{std::make_unique<BitmapTextRasterizer>(std::move(source), capacity, resource)};
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap rasterizer allocation failed"); }
  catch (const std::length_error&) { return Core::failure(UIErrorCode::CapacityExceeded, "Bitmap rasterizer storage exceeds addressable size"); }
}
