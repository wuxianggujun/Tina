#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#include <tina/ui/text/UIBakedFont.hpp>
#include <tina/ui/UIErrors.hpp>
#include "TextShaperImpl.hpp"

#include FT_OUTLINE_H
#include <msdfgen.h>
#include <msdfgen/core/ShapeDistanceFinder.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace Tina::UI::Detail {
namespace {
constexpr u32 MaximumGlyphExtent = 512;
constexpr short MaximumOutlinePoints = 8192;
constexpr u32 EmptyImage = (std::numeric_limits<u32>::max)();

struct OutlineBuilder final {
    msdfgen::Shape shape;
    msdfgen::Point2 current{};
    msdfgen::Point2 start{};
    double units = 1;
    bool failed = false;

    msdfgen::Point2 point(const FT_Vector* value) const noexcept
    {
        return {static_cast<double>(value->x) / units, static_cast<double>(value->y) / units};
    }
    void close()
    {
        if (!shape.contours.empty() && current != start)
        {
            shape.contours.back().addEdge(msdfgen::EdgeHolder(current, start));
        }
    }
    template<class Function> int callback(Function&& function) noexcept
    {
        try { function(); return 0; }
        catch (const std::bad_alloc&) { failed = true; return 1; }
    }
    static int move(const FT_Vector* to, void* context) noexcept
    {
        auto& self = *static_cast<OutlineBuilder*>(context);
        return self.callback([&] {
            self.close();
            self.shape.addContour();
            self.current = self.start = self.point(to);
        });
    }
    static int line(const FT_Vector* to, void* context) noexcept
    {
        auto& self = *static_cast<OutlineBuilder*>(context);
        return self.callback([&] {
            const auto end = self.point(to);
            if (self.current != end) { self.shape.contours.back().addEdge(msdfgen::EdgeHolder(self.current, end)); }
            self.current = end;
        });
    }
    static int quadratic(const FT_Vector* control, const FT_Vector* to, void* context) noexcept
    {
        auto& self = *static_cast<OutlineBuilder*>(context);
        return self.callback([&] {
            const auto end = self.point(to);
            const auto controlPoint = self.point(control);
            if (self.current != end || self.current != controlPoint)
            { self.shape.contours.back().addEdge(msdfgen::EdgeHolder(self.current, controlPoint, end)); }
            self.current = end;
        });
    }
    static int cubic(const FT_Vector* first, const FT_Vector* second, const FT_Vector* to, void* context) noexcept
    {
        auto& self = *static_cast<OutlineBuilder*>(context);
        return self.callback([&] {
            const auto end = self.point(to);
            const auto firstPoint = self.point(first);
            const auto secondPoint = self.point(second);
            if (self.current != end || self.current != firstPoint || self.current != secondPoint)
            { self.shape.contours.back().addEdge(msdfgen::EdgeHolder(self.current, firstPoint, secondPoint, end)); }
            self.current = end;
        });
    }
};

usize imageHash(UIFontFaceId face, u32 glyph, UIGlyphDevicePixelSize size) noexcept
{
    u64 hash = (static_cast<u64>(face.generation) << 32U) | face.index;
    for (u32 value : {glyph, size.x, size.y})
    {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    }
    return static_cast<usize>(hash);
}
}

class FreeTypeTextRasterizer final : public IUITextRasterizer {
  public:
    FreeTypeTextRasterizer(UITextRasterizerCapacity capacity, std::unique_ptr<TextShaper> shaper,
                           std::pmr::memory_resource& resource)
        : m_capacity(capacity), m_shaper(std::move(shaper)), m_images(&resource),
          m_lookup(&resource), m_pixels(&resource), m_glyphs(&resource),
          m_distanceScratch(&resource), m_errorScratch(&resource)
    {
        m_images.reserve(capacity.glyphImageCapacity);
        m_lookup.resize(std::bit_ceil(static_cast<usize>(capacity.glyphImageCapacity) * 2U), EmptyImage);
        m_pixels.resize(capacity.coverageByteCapacity);
        m_glyphs.reserve(capacity.maxGlyphsPerRaster);
        m_distanceScratch.resize(static_cast<usize>(MaximumGlyphExtent) * MaximumGlyphExtent * 3U);
        m_errorScratch.resize(static_cast<usize>(MaximumGlyphExtent) * MaximumGlyphExtent);
    }

    Core::Result<UIFontFaceId> openFace(std::span<const std::byte> bytes, i32 faceIndex) override
    {
        return m_shaper->openFace(bytes, faceIndex);
    }
    Core::Status closeFace(UIFontFaceId face) noexcept override
    {
        auto status = m_shaper->closeFace(face);
        if (status)
        {
            m_images.clear();
            std::fill(m_lookup.begin(), m_lookup.end(), EmptyImage);
            m_pixelBytes = 0;
        }
        return status;
    }
    Core::Status setFallbackChain(std::span<const UIFontFaceId> faces) override
    {
        return m_shaper->setFallbackChain(faces);
    }

    Core::Status primeGlyphCache(UIFontFaceId face, std::span<const std::byte> cooked) override
    {
        const auto* font = m_shaper->m_impl->face(face);
        if (font == nullptr) { return Core::failure(UIErrorCode::InvalidFont, "Cannot seed a stale font face"); }
        auto seed = parseUIBakedFont(cooked);
        if (!seed) { return Core::failure(seed.error()); }
        if (seed->fontFingerprint != uiFontFingerprint(font->bytes) || seed->faceIndex != static_cast<u32>(font->faceIndex))
        {
            return Core::failure(UIErrorCode::InvalidFont, "Cooked glyph seed does not match the opened font bytes and face index");
        }
        u64 additionalBytes = 0;
        u32 additionalGlyphs = 0;
        for (u32 index = 0; index < seed->glyphCount; ++index)
        {
            const UIBakedGlyph glyph = seed->glyph(index);
            if (glyph.glyphIndex >= static_cast<u32>(font->ft->num_glyphs))
            {
                return Core::failure(UIErrorCode::InvalidFont, "Cooked glyph index is outside the font");
            }
            if (findImage(face, glyph.glyphIndex, glyph.rasterSize) == nullptr)
            {
                additionalBytes += glyph.pixelBytes;
                ++additionalGlyphs;
            }
        }
        if (additionalGlyphs > m_capacity.glyphImageCapacity - m_images.size() ||
            additionalBytes > m_capacity.coverageByteCapacity - m_pixelBytes)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "Cooked glyph seed exceeds the dynamic cache budget");
        }
        // Validation and capacity checks above are complete before any mutation.
        for (u32 index = 0; index < seed->glyphCount; ++index)
        {
            UIBakedGlyph glyph = seed->glyph(index);
            if (findImage(face, glyph.glyphIndex, glyph.rasterSize) != nullptr) { continue; }
            if (glyph.pixelBytes != 0)
            {
                std::memcpy(m_pixels.data() + m_pixelBytes, seed->pixels.data() + glyph.pixelOffset, glyph.pixelBytes);
            }
            glyph.pixelOffset = m_pixelBytes;
            m_pixelBytes += glyph.pixelBytes;
            publishImage(face, glyph);
        }
        return Core::success();
    }

    Core::Result<UITextMetrics> measure(UIFontFaceId face, std::string_view text,
                                       UITextStyle style, UITextRasterScale scale) override
    {
        if (auto status = validateUITextRasterScale(scale); !status) { return Core::failure(status.error()); }
        auto run = shape(face, text, style);
        if (!run) { return Core::failure(run.error()); }
        return run->metrics;
    }

    Core::Result<UITextShapeView> shape(UIFontFaceId face, std::string_view text,
                                      UITextStyle style) override
    {
        auto run = m_shaper->shape(face, text, style);
        if (!run) { return Core::failure(run.error()); }
        return UITextShapeView{run->metrics, run->baselineFromLineTop,
                               run->scalars, run->missingGlyphCount};
    }

    Core::Result<UITextRasterBatch> raster(UIFontFaceId face, std::string_view text,
                                         UITextStyle style, UITextRasterScale scale) override
    {
        if (auto status = validateUITextRasterScale(scale); !status) { return Core::failure(status.error()); }
        auto run = m_shaper->shape(face, text, style);
        if (!run) { return Core::failure(run.error()); }
        try
        {
            if (run->glyphs.size() > m_capacity.maxGlyphsPerRaster)
            {
                return Core::failure(UIErrorCode::CapacityExceeded, "Raster glyph budget exhausted");
            }
            m_glyphs.clear();
            for (const ShapedGlyph& shaped : run->glyphs)
            {
                auto image = ensureImage(shaped.face, shaped.glyphIndex, style, scale);
                if (!image) { return Core::failure(image.error()); }
                const UIBakedGlyph& cached = **image;
                m_glyphs.push_back(UITextGlyphRaster{
                    .face = shaped.face, .glyphIndex = shaped.glyphIndex,
                    .clusterByteBegin = shaped.clusterByteBegin, .clusterByteEnd = shaped.clusterByteEnd,
                    .line = shaped.line, .originX = shaped.x, .originY = shaped.y,
                    .advance = shaped.advanceX,
                    .bearingX = cached.bearingXEm * style.logicalSize,
                    .bearingY = cached.bearingYEm * style.logicalSize,
                    .width = cached.width, .height = cached.height,
                    .coverageOffset = cached.pixelOffset, .coveragePitch = cached.width * 4U,
                    .logicalWidth = static_cast<float>(cached.width) * style.logicalSize / cached.rasterSize.x,
                    .logicalHeight = static_cast<float>(cached.height) * style.logicalSize / cached.rasterSize.y,
                    .rasterSize = cached.rasterSize, .imageKind = cached.imageKind,
                    .distanceRange = cached.distanceRange,
                    .nominalAdvance = static_cast<float>(hb_font_get_glyph_h_advance(
                        m_shaper->m_impl->face(shaped.face)->hb, shaped.glyphIndex)) * style.logicalSize /
                        m_shaper->m_impl->face(shaped.face)->unitsPerEm,
                });
            }
            return UITextRasterBatch{run->metrics, run->baselineFromLineTop, m_glyphs,
                                      run->scalars, {m_pixels.data(), m_pixelBytes}, run->missingGlyphCount};
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "MSDF glyph generation allocation failed");
        }
    }

    UITextRasterizerCapacity capacity() const noexcept override { return m_capacity; }

  private:
    struct CachedImage final { UIFontFaceId face{}; UIBakedGlyph glyph{}; };

    const UIBakedGlyph* findImage(UIFontFaceId face, u32 glyph, UIGlyphDevicePixelSize size) const noexcept
    {
        usize bucket = imageHash(face, glyph, size) & (m_lookup.size() - 1U);
        for (usize probe = 0; probe < m_lookup.size(); ++probe)
        {
            const u32 index = m_lookup[bucket];
            if (index == EmptyImage) { return nullptr; }
            const CachedImage& image = m_images[index];
            if (image.face == face && image.glyph.glyphIndex == glyph && image.glyph.rasterSize == size)
            {
                return &image.glyph;
            }
            bucket = (bucket + 1U) & (m_lookup.size() - 1U);
        }
        return nullptr;
    }

    const UIBakedGlyph* publishImage(UIFontFaceId face, const UIBakedGlyph& glyph)
    {
        usize bucket = imageHash(face, glyph.glyphIndex, glyph.rasterSize) & (m_lookup.size() - 1U);
        while (m_lookup[bucket] != EmptyImage) { bucket = (bucket + 1U) & (m_lookup.size() - 1U); }
        m_lookup[bucket] = static_cast<u32>(m_images.size());
        m_images.push_back({face, glyph});
        return &m_images.back().glyph;
    }

    Core::Status checkPixelBudget(u32 width, u32 height) const
    {
        if (width > MaximumGlyphExtent || height > MaximumGlyphExtent ||
            static_cast<u64>(width) * height * 4U > m_capacity.coverageByteCapacity - m_pixelBytes)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "Glyph exceeds the bounded image/byte budget");
        }
        return Core::success();
    }

    Core::Result<const UIBakedGlyph*> ensureImage(UIFontFaceId faceId, u32 glyphIndex,
                                                 UITextStyle style, UITextRasterScale scale)
    {
        constexpr UIGlyphDevicePixelSize msdfSize{UITextMsdfPixelsPerEm, UITextMsdfPixelsPerEm};
        if (const auto* cached = findImage(faceId, glyphIndex, msdfSize); cached != nullptr &&
            cached->imageKind == UIGlyphImageKind::Msdf) { return cached; }
        auto* font = m_shaper->m_impl->face(faceId);
        if (font == nullptr) { return Core::failure(UIErrorCode::InvalidFont, "Shaped glyph references a stale face"); }
        FT_Face face = font->ft;
        if (FT_HAS_COLOR(face))
        {
            const auto requested = resolveGlyphDevicePixelSize(style, scale);
            if (requested.x > MaximumGlyphExtent || requested.y > MaximumGlyphExtent)
            {
                return Core::failure(UIErrorCode::CapacityExceeded, "Color glyph ppem exceeds the raster budget");
            }
            FT_Error sized = 0;
            if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0)
            {
                int selected = 0;
                long bestDistance = (std::numeric_limits<long>::max)();
                for (int index = 0; index < face->num_fixed_sizes; ++index)
                {
                    const long distance = std::abs(face->available_sizes[index].y_ppem / 64 - static_cast<long>(requested.y));
                    if (distance < bestDistance) { bestDistance = distance; selected = index; }
                }
                sized = FT_Select_Size(face, selected);
            }
            else { sized = FT_Set_Pixel_Sizes(face, requested.x, requested.y); }
            if (sized != 0) { return Core::failure(UIErrorCode::InvalidFont, "Unable to select a color font strike"); }
            const UIGlyphDevicePixelSize actual{face->size->metrics.x_ppem, face->size->metrics.y_ppem};
            if (const auto* cached = findImage(faceId, glyphIndex, actual); cached != nullptr) { return cached; }
            if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR | FT_LOAD_DEFAULT) == 0 &&
                (face->glyph->format == FT_GLYPH_FORMAT_BITMAP || FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) == 0) &&
                face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                if (m_images.size() == m_capacity.glyphImageCapacity)
                {
                    return Core::failure(UIErrorCode::CapacityExceeded, "Dynamic glyph image cache exhausted");
                }
                const FT_Bitmap& bitmap = face->glyph->bitmap;
                if (auto status = checkPixelBudget(bitmap.width, bitmap.rows); !status) { return Core::failure(status.error()); }
                const u32 pitch = static_cast<u32>(std::abs(static_cast<i64>(bitmap.pitch)));
                if (actual.x == 0 || actual.y == 0 || (bitmap.width != 0 && bitmap.rows != 0 &&
                    (bitmap.buffer == nullptr || pitch < bitmap.width * 4U)))
                {
                    return Core::failure(UIErrorCode::InvalidFont, "Invalid color bitmap buffer or pitch");
                }
                UIBakedGlyph image{glyphIndex, bitmap.width, bitmap.rows, actual, UIGlyphImageKind::Color,
                    m_pixelBytes, bitmap.width * bitmap.rows * 4U,
                    static_cast<float>(face->glyph->bitmap_left) / actual.x,
                    static_cast<float>(face->glyph->bitmap_top) / actual.y, 0};
                for (u32 row = 0; row < bitmap.rows; ++row)
                {
                    const u32 sourceRow = bitmap.pitch < 0 ? bitmap.rows - row - 1U : row;
                    const u8* source = bitmap.buffer + static_cast<usize>(sourceRow) * pitch;
                    u8* destination = m_pixels.data() + m_pixelBytes + static_cast<usize>(row) * bitmap.width * 4U;
                    for (u32 column = 0; column < bitmap.width; ++column)
                    {
                        // FreeType BGRA is already premultiplied; do not multiply alpha twice.
                        destination[4U * column] = source[4U * column + 2U];
                        destination[4U * column + 1U] = source[4U * column + 1U];
                        destination[4U * column + 2U] = source[4U * column];
                        destination[4U * column + 3U] = source[4U * column + 3U];
                    }
                }
                m_pixelBytes += image.pixelBytes;
                return publishImage(faceId, image);
            }
        }
        if (m_images.size() == m_capacity.glyphImageCapacity)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "Dynamic glyph image cache exhausted");
        }
        if (!FT_IS_SCALABLE(face) || FT_Load_Glyph(face, glyphIndex,
                FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM) != 0 ||
            face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        {
            return Core::failure(UIErrorCode::InvalidFont, "Font glyph has neither an outline nor a supported color bitmap");
        }
        const FT_Outline& outline = face->glyph->outline;
        UIBakedGlyph image{glyphIndex, 0, 0, msdfSize, UIGlyphImageKind::Msdf,
                           m_pixelBytes, 0, 0, 0, UITextMsdfDistanceRange};
        if (outline.n_points == 0) { return publishImage(faceId, image); }
        if (outline.n_points < 0 || outline.n_points > MaximumOutlinePoints || font->unitsPerEm == 0)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "Glyph outline complexity exceeds the generation budget");
        }
        OutlineBuilder builder;
        builder.shape.setYAxisOrientation(msdfgen::Y_UPWARD);
        builder.units = font->unitsPerEm;
        const FT_Outline_Funcs callbacks{OutlineBuilder::move, OutlineBuilder::line,
                                        OutlineBuilder::quadratic, OutlineBuilder::cubic, 0, 0};
        if (FT_Outline_Decompose(&face->glyph->outline, &callbacks, &builder) != 0 || builder.failed)
        {
            return Core::failure(UIErrorCode::InvalidFont, "Unable to decompose the glyph outline");
        }
        builder.close();
        std::erase_if(builder.shape.contours, [](const msdfgen::Contour& contour) { return contour.edges.empty(); });
        if (builder.shape.contours.empty()) { return publishImage(faceId, image); }
        builder.shape.normalize();
        if (!builder.shape.validate()) { return Core::failure(UIErrorCode::InvalidFont, "Invalid MSDF outline geometry"); }
        const auto bounds = builder.shape.getBounds();
        // TrueType and CFF outlines use opposite winding conventions. Detect
        // the exterior sign, as msdfgen's -guesswinding does, before coloring.
        const msdfgen::Point2 exterior{bounds.l - (bounds.r - bounds.l) - 1.0,
                                      bounds.b - (bounds.t - bounds.b) - 1.0};
        if (msdfgen::SimpleTrueShapeDistanceFinder::oneShotDistance(builder.shape, exterior) > 0.0)
        {
            for (auto& contour : builder.shape.contours) { contour.reverse(); }
        }
        msdfgen::edgeColoringSimple(builder.shape, 3.0, glyphIndex);
        constexpr double em = UITextMsdfPixelsPerEm;
        constexpr double padding = UITextMsdfDistanceRange * 0.5;
        const double left = std::floor(bounds.l * em - padding);
        const double bottom = std::floor(bounds.b * em - padding);
        const double right = std::ceil(bounds.r * em + padding);
        const double top = std::ceil(bounds.t * em + padding);
        if (!std::isfinite(left) || !std::isfinite(bottom) || !std::isfinite(right) || !std::isfinite(top) ||
            right <= left || top <= bottom || right - left > MaximumGlyphExtent || top - bottom > MaximumGlyphExtent)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "MSDF glyph bounds exceed the image budget");
        }
        image.width = static_cast<u32>(right - left);
        image.height = static_cast<u32>(top - bottom);
        if (auto status = checkPixelBudget(image.width, image.height); !status) { return Core::failure(status.error()); }
        image.pixelBytes = image.width * image.height * 4U;
        image.bearingXEm = static_cast<float>(left / em);
        image.bearingYEm = static_cast<float>(top / em);
        msdfgen::MSDFGeneratorConfig config;
        config.errorCorrection.buffer = m_errorScratch.data();
        const msdfgen::BitmapSection<float, 3> bitmap(m_distanceScratch.data(), image.width, image.height,
                                                     msdfgen::Y_UPWARD);
        msdfgen::generateMSDF(bitmap, builder.shape,
            msdfgen::SDFTransformation(msdfgen::Projection({em, em}, {-left / em, -bottom / em}),
                                       msdfgen::Range(UITextMsdfDistanceRange / em)), config);
        for (u32 row = 0; row < image.height; ++row)
        {
            const float* source = bitmap(0, image.height - row - 1U);
            u8* destination = m_pixels.data() + m_pixelBytes + static_cast<usize>(row) * image.width * 4U;
            for (u32 column = 0; column < image.width; ++column)
            {
                for (u32 channel = 0; channel < 3; ++channel)
                {
                    const float distance = source[column * 3U + channel];
                    // A channel with no contributing edge can retain msdfgen's
                    // +/-DBL_MAX sentinel and become infinity in float output.
                    // Its PNG writer saturates it too. NaN is not a distance.
                    if (std::isnan(distance))
                    { return Core::failure(UIErrorCode::InvalidFont, "MSDF glyph " + std::to_string(glyphIndex) + " produced NaN distance data"); }
                    destination[column * 4U + channel] = static_cast<u8>(std::clamp(distance * 255.0F + 0.5F, 0.0F, 255.0F));
                }
                destination[column * 4U + 3U] = 255;
            }
        }
        m_pixelBytes += image.pixelBytes;
        return publishImage(faceId, image);
    }

    UITextRasterizerCapacity m_capacity;
    std::unique_ptr<TextShaper> m_shaper;
    std::pmr::vector<CachedImage> m_images;
    std::pmr::vector<u32> m_lookup;
    std::pmr::vector<u8> m_pixels;
    std::pmr::vector<UITextGlyphRaster> m_glyphs;
    std::pmr::vector<float> m_distanceScratch;
    std::pmr::vector<u8> m_errorScratch;
    u32 m_pixelBytes = 0;
};
} // namespace Tina::UI::Detail

namespace Tina::UI {
Core::Result<std::unique_ptr<IUITextRasterizer>> createFreeTypeTextRasterizer(
    UITextRasterizerCapacity capacity, std::pmr::memory_resource& resource)
{
    auto shaper = TextShaper::Create(capacity, resource);
    if (!shaper) { return Core::failure(shaper.error()); }
    try
    {
        return std::unique_ptr<IUITextRasterizer>(
            new Detail::FreeTypeTextRasterizer(capacity, std::move(*shaper), resource));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "MSDF rasterizer allocation failed");
    }
}
} // namespace Tina::UI
