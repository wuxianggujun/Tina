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

struct UITextRasterizerCapacity final {
    static constexpr u32 DefaultFaceCapacity = 4;
    static constexpr u32 DefaultMaxGlyphsPerRaster = 4096;
    static constexpr u32 DefaultCoverageByteCapacity = 256U * 1024U;
    static constexpr u32 MaxFaceCapacity = 64;
    static constexpr u32 MaxGlyphsPerRaster = 1'048'576;
    static constexpr u32 MaxCoverageByteCapacity = 64U * 1024U * 1024U;

    u32 faceCapacity = DefaultFaceCapacity;
    // Fixed per-call scratch for one raster() invocation. Not a global glyph
    // atlas; atlas/upload is a later slice.
    u32 maxGlyphsPerRaster = DefaultMaxGlyphsPerRaster;
    u32 coverageByteCapacity = DefaultCoverageByteCapacity;
};

// One drawable codepoint after measure/raster. Coverage is R8, row-major,
// pitch == width. Empty coverage (width/height 0) is valid when only advance is
// known (glyph not yet uploaded / missing face).
struct UITextGlyphRaster final {
    u32 codepoint = 0;
    float advance = 0.0F;
    float bearingX = 0.0F;
    float bearingY = 0.0F;
    u32 width = 0;
    u32 height = 0;
    u32 coverageOffset = 0;
    u32 coveragePitch = 0;
};

// Borrowed view into storage supplied to raster(). Invalidated by the next
// raster() on the same rasterizer instance, or by destroying the rasterizer.
struct UITextRasterBatch final {
    UITextMetrics metrics{};
    std::span<const UITextGlyphRaster> glyphs{};
    std::span<const u8> coverage{};
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

    // May update face internal state (pixel size). Not const because FreeType
    // faces mutate on measure/load.
    [[nodiscard]] virtual Core::Result<UITextMetrics> measure(
        UIFontFaceId face,
        std::string_view utf8,
        UITextStyle style) = 0;

    // Emits one glyph record per drawable codepoint; '\n' advances metrics line
    // count only. Coverage is packed into a fixed Create-time buffer owned by
    // the rasterizer (not the caller's PMR).
    [[nodiscard]] virtual Core::Result<UITextRasterBatch> raster(
        UIFontFaceId face,
        std::string_view utf8,
        UITextStyle style) = 0;

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
