#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIText.hpp>

#include <compare>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>

namespace Tina::UI {

// Strong id for a face owned by one IUITextRasterizer instance. hasValue() only
// means the bits are non-zero; the owning rasterizer must re-resolve generation.
struct UIFontFaceId final {
    u32 index = 0;
    u32 generation = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return generation != 0;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const UIFontFaceId&) const = default;
};

// Device pixels per logical unit. Outline MSDFs are scale independent; color
// bitmap strikes and the explicit Null placeholder use this raster scale.
struct UITextRasterScale final {
    float x = 1.0F;
    float y = 1.0F;

    auto operator<=>(const UITextRasterScale&) const = default;
};

[[nodiscard]] Core::Status validateUITextRasterScale(UITextRasterScale scale);

// Integer device ppem a style rasterizes at under a given scale. Both the
// rasterizer and the paint emitters key glyph caches from this single
// quantization so an atlas lookup can never disagree with the bitmap it holds.
struct UIGlyphDevicePixelSize final {
    u32 x = 0;
    u32 y = 0;

    auto operator<=>(const UIGlyphDevicePixelSize&) const = default;
};

[[nodiscard]] UIGlyphDevicePixelSize resolveGlyphDevicePixelSize(
    const UITextStyle& style,
    UITextRasterScale scale) noexcept;

struct UITextRasterizerCapacity final {
    static constexpr u32 DefaultFaceCapacity = 8;
    static constexpr u32 DefaultMaxGlyphsPerRaster = 4096;
    static constexpr u32 DefaultCoverageByteCapacity = 16U * 1024U * 1024U;
    static constexpr u32 MaxFaceCapacity = 64;
    static constexpr u32 MaxGlyphsPerRaster = 1'048'576;
    static constexpr u32 MaxCoverageByteCapacity = 64U * 1024U * 1024U;

    u32 faceCapacity = DefaultFaceCapacity;
    // Fixed per-call scratch for one raster() invocation. Not a global glyph
    // atlas; the atlas lives in UIGlyphAtlas.
    u32 maxGlyphsPerRaster = DefaultMaxGlyphsPerRaster;
    u32 coverageByteCapacity = DefaultCoverageByteCapacity;
    // Unique glyph images, not Unicode coverage. No font charmap is eagerly
    // rasterized. These bounds also limit cold-cache work and memory.
    u32 glyphImageCapacity = 4096;
    u32 maxTextBytes = 64U * 1024U;
    u32 maxFontBytes = 64U * 1024U * 1024U;
};

enum class UIGlyphImageKind : u8 { Coverage, Msdf, Color };
inline constexpr u32 UITextMsdfPixelsPerEm = 48;
inline constexpr float UITextMsdfDistanceRange = 4.0F;

// Logical order, one record per non-LF Unicode scalar. This is a caret/line
// breaking map, NOT a drawable glyph array. Several scalars can share one
// shaping cluster; glyph count is independent of scalar count.
struct UITextScalarMetrics final {
    float advance = 0.0F;
    float visualStartX = 0.0F;
    float visualEndX = 0.0F;
    u32 clusterByteBegin = 0;
    u32 clusterByteEnd = 0;
    u32 line = 0;
    bool rightToLeft = false;
    // Resolved UAX #9 paragraph base, independent of this scalar's embedding.
    bool paragraphRightToLeft = false;
    // False for an advance-only measurement; true for a positioned shaping run.
    bool hasVisualPosition = false;
};

// One positioned glyph in visual order. Pixel data is tightly packed RGBA8:
// linear RGB distances for Msdf, premultiplied sRGBA for Color, replicated
// coverage for the explicit placeholder. Bearings and draw extents are logical.
struct UITextGlyphRaster final {
    UIFontFaceId face{};
    u32 glyphIndex = 0;
    u32 clusterByteBegin = 0;
    u32 clusterByteEnd = 0;
    u32 line = 0;
    float originX = 0.0F;
    float originY = 0.0F;
    float advance = 0.0F;
    float bearingX = 0.0F;
    float bearingY = 0.0F;
    u32 width = 0;
    u32 height = 0;
    u32 coverageOffset = 0;
    u32 coveragePitch = 0;
    float logicalWidth = 0.0F;
    float logicalHeight = 0.0F;
    UIGlyphDevicePixelSize rasterSize{};
    UIGlyphImageKind imageKind = UIGlyphImageKind::Coverage;
    float distanceRange = 0.0F;
    float nominalAdvance = 0.0F;
};

// Borrowed owner storage. Invalidated by the next measure/raster/font mutation
// on the same instance, or by destroying the rasterizer.
struct UITextRasterBatch final {
    UITextMetrics metrics{};
    // Logical distance to the unhinted font baseline. The Render bridge may
    // snap this shared origin according to UITextPixelSnap, never glyph edges.
    float baselineFromLineTop = 0.0F;
    std::span<const UITextGlyphRaster> glyphs{};
    std::span<const UITextScalarMetrics> scalars{};
    std::span<const u8> coverage{};
    u32 missingGlyphCount = 0;
};

// Backend-neutral text measure/raster SPI. FreeType types must not appear in
// this header. Implementations live in tina_ui (placeholder) or optional
// tina_ui_freetype.
class IUITextRasterizer {
  public:
    virtual ~IUITextRasterizer() = default;

    IUITextRasterizer(const IUITextRasterizer&) = delete;
    IUITextRasterizer& operator=(const IUITextRasterizer&) = delete;
    IUITextRasterizer(IUITextRasterizer&&) = delete;
    IUITextRasterizer& operator=(IUITextRasterizer&&) = delete;

    // Placeholder: empty fontBytes opens the built-in monospaced face.
    // FreeType: fontBytes must be a complete face blob (TTF/OTF/etc.).
    [[nodiscard]] virtual Core::Result<UIFontFaceId> openFace(
        std::span<const std::byte> fontBytes,
        i32 faceIndex = 0) = 0;

    [[nodiscard]] virtual Core::Status closeFace(UIFontFaceId face) noexcept = 0;

    // Ordered explicit fallback chain. The primary face passed to measure /
    // raster always wins unless an Emoji presentation needs a color face.
    [[nodiscard]] virtual Core::Status setFallbackChain(
        std::span<const UIFontFaceId> faces) = 0;

    // Validated versioned output of tina_msdfgen. An optional startup seed for
    // the same dynamic cache; a mismatch fails closed, never tries old formats.
    [[nodiscard]] virtual Core::Status primeGlyphCache(
        UIFontFaceId face, std::span<const std::byte> cooked) = 0;

    // Logical unhinted shaping metrics; changing device DPI does not change
    // advances. Scale affects color strikes / explicit placeholder pixels.
    [[nodiscard]] virtual Core::Result<UITextMetrics> measure(
        UIFontFaceId face,
        std::string_view utf8,
        UITextStyle style,
        UITextRasterScale scale = {}) = 0;

    // Emits positioned glyphs plus a separate logical scalar map. No caller
    // may infer glyph indices from UTF-8 byte/scalar indices. Borrowed spans
    // are invalidated by the next measure/raster/font mutation on this owner.
    [[nodiscard]] virtual Core::Result<UITextRasterBatch> raster(
        UIFontFaceId face,
        std::string_view utf8,
        UITextStyle style,
        UITextRasterScale scale = {}) = 0;

    [[nodiscard]] virtual UITextRasterizerCapacity capacity() const noexcept = 0;

  protected:
    IUITextRasterizer() = default;
};

// Always available; no FreeType, no file IO. openFace requires empty bytes.
[[nodiscard]] Core::Result<std::unique_ptr<IUITextRasterizer>> createPlaceholderTextRasterizer(
    UITextRasterizerCapacity capacity = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

[[nodiscard]] Core::Status validateUITextRasterizerCapacity(
    const UITextRasterizerCapacity& capacity);

} // namespace Tina::UI
