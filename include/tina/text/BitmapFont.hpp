#pragma once

#include <tina/core/error/Result.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace Tina::Text {

namespace TextErrorCode {
inline constexpr Core::ErrorCode InvalidFont{Core::ErrorDomain::Text, 1};
inline constexpr Core::ErrorCode InvalidText{Core::ErrorDomain::Text, 2};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Text, 3};
}

enum class BitmapFontImageKind : Core::u8 { Coverage, Color };
struct BitmapFontPage final {
    Core::u32 width = 0;
    Core::u32 height = 0;
    // Coverage uses source alpha; Color uses straight sRGBA and permits tint.
    BitmapFontImageKind imageKind = BitmapFontImageKind::Coverage;
};
struct BitmapGlyph final {
    Core::u32 codepoint = 0;
    Core::u32 page = 0;
    Core::u32 x = 0, y = 0, width = 0, height = 0;
    float advance = 0;
    float bearingX = 0;
    float bearingY = 0; // positive upwards from the baseline
};
struct BitmapKerning final {
    Core::u32 left = 0, right = 0;
    float adjustment = 0;
};
struct BitmapFontDesc final {
    float nominalSize = 16;
    float lineHeight = 16;
    float baseline = 12;
    Core::u32 fallbackCodepoint = 0xFFFD;
    std::vector<BitmapFontPage> pages;
    std::vector<BitmapGlyph> glyphs;
    std::vector<BitmapKerning> kerning;
};

// Immutable, owning metrics. No texture, AssetSystem, FreeType or UI dependency.
// Glyph indices are local to this font and stable for its entire lifetime.
class BitmapFont final {
  public:
    static constexpr Core::u32 MaxPages = 16;
    static constexpr Core::u32 MaxGlyphs = 65536;
    static constexpr Core::u32 MaxKerningPairs = 262144;
    static constexpr Core::u32 MaxPageDimension = 16384;
    [[nodiscard]] static Core::Result<BitmapFont> Create(BitmapFontDesc descriptor);
    [[nodiscard]] const BitmapFontDesc& descriptor() const noexcept { return descriptor_; }
    [[nodiscard]] Core::u32 glyphIndex(Core::u32 codepoint) const noexcept;
    [[nodiscard]] bool contains(Core::u32 codepoint) const noexcept;
    [[nodiscard]] float kerning(Core::u32 left, Core::u32 right) const noexcept;
  private:
    explicit BitmapFont(BitmapFontDesc descriptor) : descriptor_(std::move(descriptor)) {}
    BitmapFontDesc descriptor_;
};

class BitmapFontAtlas final {
  public:
    static constexpr Core::u64 MaxPixelBytes = 64ULL * 1024ULL * 1024ULL;
    // Tightly packed straight sRGBA8, one unscaled image per metrics page.
    [[nodiscard]] static Core::Result<BitmapFontAtlas> Create(BitmapFont font, std::vector<std::vector<Core::u8>> pages);
    [[nodiscard]] const BitmapFont& font() const noexcept { return font_; }
    [[nodiscard]] const auto& pages() const noexcept { return pages_; }
  private:
    BitmapFontAtlas(BitmapFont font, std::vector<std::vector<Core::u8>> pages)
        : font_(std::move(font)), pages_(std::move(pages)) {}
    BitmapFont font_;
    std::vector<std::vector<Core::u8>> pages_;
};

struct BitmapTextOptions final {
    float scale = 1;
    float extraLineSpacing = 0;
    // Zero preserves explicit lines only. Positive width hard-wraps scalars;
    // word/grapheme-aware UI wrapping remains owned by the existing UI system.
    float wrapWidth = 0;
    Core::u32 maxScalars = 65536;
    Core::u32 maxTextBytes = 256 * 1024;
};
struct BitmapTextGlyph final {
    Core::u32 glyphIndex = 0;
    Core::u32 scalarIndex = 0;
    Core::u32 byteBegin = 0, byteEnd = 0;
    Core::u32 line = 0;
    float originX = 0, baselineY = 0;
    float advance = 0;
    bool missing = false;
};
struct BitmapTextMetrics {
    float width = 0, height = 0;
    float baseline = 0;
    Core::u32 scalarCount = 0, lineCount = 0, missingGlyphCount = 0;
};
struct BitmapTextLayout final : BitmapTextMetrics {
    std::vector<BitmapTextGlyph> glyphs; // one per non-LF scalar, including spaces
};
struct BitmapTextLayoutView final : BitmapTextMetrics {
    std::span<const BitmapTextGlyph> glyphs;
};

// Allocation-free scratch form. Storage may be overwritten on failure; no view
// is returned until the entire layout is valid.
[[nodiscard]] Core::Result<BitmapTextLayoutView> layoutBitmapTextInto(
    const BitmapFont& font, std::string_view utf8, BitmapTextOptions options, std::span<BitmapTextGlyph> storage);

// Strict UTF-8, explicit LF, kerning and fallback. This is a pixel-font scalar
// layout, not a replacement for HarfBuzz/BiDi shaping of outline fonts.
[[nodiscard]] Core::Result<BitmapTextLayout> layoutBitmapText(
    const BitmapFont& font, std::string_view utf8, BitmapTextOptions options = {});

} // namespace Tina::Text
