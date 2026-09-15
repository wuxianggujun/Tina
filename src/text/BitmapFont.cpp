#include <tina/text/BitmapFont.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace Tina::Text {
namespace {
bool scalar(Core::u32 value) noexcept {
    return value != 0 && value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF);
}
bool metric(float value) noexcept { return std::isfinite(value) && std::abs(value) <= 1'000'000; }
}

Core::Result<BitmapFont> BitmapFont::Create(BitmapFontDesc descriptor)
try {
    if (!metric(descriptor.nominalSize) || descriptor.nominalSize <= 0 ||
        !metric(descriptor.lineHeight) || descriptor.lineHeight <= 0 ||
        !metric(descriptor.baseline) || descriptor.baseline < 0 || descriptor.baseline > descriptor.lineHeight ||
        descriptor.pages.empty() || descriptor.pages.size() > MaxPages || descriptor.glyphs.empty() ||
        descriptor.glyphs.size() > MaxGlyphs || descriptor.kerning.size() > MaxKerningPairs ||
        !scalar(descriptor.fallbackCodepoint))
        return Core::failure(TextErrorCode::InvalidFont, "Invalid bitmap font metrics or capacity");
    for (const auto& page : descriptor.pages) {
        if (!page.width || !page.height || page.width > MaxPageDimension || page.height > MaxPageDimension ||
            page.imageKind > BitmapFontImageKind::Color)
            return Core::failure(TextErrorCode::InvalidFont, "Invalid bitmap font page");
    }
    std::sort(descriptor.glyphs.begin(), descriptor.glyphs.end(), [](const auto& left, const auto& right) {
        return left.codepoint < right.codepoint;
    });
    Core::u32 previous = 0;
    bool hasFallback = false;
    for (const auto& glyph : descriptor.glyphs) {
        if (!scalar(glyph.codepoint) || glyph.codepoint == previous || glyph.page >= descriptor.pages.size() ||
            !metric(glyph.advance) || glyph.advance < 0 || !metric(glyph.bearingX) || !metric(glyph.bearingY))
            return Core::failure(TextErrorCode::InvalidFont, "Invalid or duplicate bitmap glyph");
        const auto& page = descriptor.pages[glyph.page];
        if (glyph.x > page.width || glyph.y > page.height || glyph.width > page.width - glyph.x ||
            glyph.height > page.height - glyph.y || ((glyph.width == 0) != (glyph.height == 0)))
            return Core::failure(TextErrorCode::InvalidFont, "Bitmap glyph is outside its page");
        previous = glyph.codepoint;
        hasFallback |= glyph.codepoint == descriptor.fallbackCodepoint;
    }
    if (!hasFallback) return Core::failure(TextErrorCode::InvalidFont, "Bitmap font requires an explicit fallback glyph");
    std::sort(descriptor.kerning.begin(), descriptor.kerning.end(), [](const auto& left, const auto& right) {
        return std::tie(left.left, left.right) < std::tie(right.left, right.right);
    });
    BitmapFont font{std::move(descriptor)};
    std::pair<Core::u32, Core::u32> previousPair{};
    for (const auto& pair : font.descriptor_.kerning) {
        if (!font.contains(pair.left) || !font.contains(pair.right) || !metric(pair.adjustment) ||
            previousPair == std::pair{pair.left, pair.right})
            return Core::failure(TextErrorCode::InvalidFont, "Invalid or duplicate bitmap kerning pair");
        // A scalar's caret interval must not run backwards.
        if (font.descriptor_.glyphs[font.glyphIndex(pair.right)].advance + pair.adjustment < 0)
            return Core::failure(TextErrorCode::InvalidFont, "Kerning exceeds glyph advance");
        previousPair = {pair.left, pair.right};
    }
    return font;
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap font allocation failed"); }

bool BitmapFont::contains(Core::u32 codepoint) const noexcept {
    const auto found = std::lower_bound(descriptor_.glyphs.begin(), descriptor_.glyphs.end(), codepoint,
        [](const BitmapGlyph& glyph, Core::u32 value) { return glyph.codepoint < value; });
    return found != descriptor_.glyphs.end() && found->codepoint == codepoint;
}
Core::u32 BitmapFont::glyphIndex(Core::u32 codepoint) const noexcept {
    auto found = std::lower_bound(descriptor_.glyphs.begin(), descriptor_.glyphs.end(), codepoint,
        [](const BitmapGlyph& glyph, Core::u32 value) { return glyph.codepoint < value; });
    if (found == descriptor_.glyphs.end() || found->codepoint != codepoint)
        found = std::lower_bound(descriptor_.glyphs.begin(), descriptor_.glyphs.end(), descriptor_.fallbackCodepoint,
            [](const BitmapGlyph& glyph, Core::u32 value) { return glyph.codepoint < value; });
    return static_cast<Core::u32>(found - descriptor_.glyphs.begin());
}
float BitmapFont::kerning(Core::u32 left, Core::u32 right) const noexcept {
    auto found = std::lower_bound(descriptor_.kerning.begin(), descriptor_.kerning.end(), std::pair{left, right},
        [](const BitmapKerning& pair, const auto& value) { return std::pair{pair.left, pair.right} < value; });
    return found != descriptor_.kerning.end() && found->left == left && found->right == right ? found->adjustment : 0;
}

Core::Result<BitmapFontAtlas> BitmapFontAtlas::Create(BitmapFont font, std::vector<std::vector<Core::u8>> pages) {
    if (pages.size() != font.descriptor().pages.size())
        return Core::failure(TextErrorCode::InvalidFont, "Bitmap atlas page count mismatch");
    Core::u64 bytes = 0;
    for (Core::usize index = 0; index < pages.size(); ++index) {
        const auto& page = font.descriptor().pages[index];
        const Core::u64 expected = static_cast<Core::u64>(page.width) * page.height * 4;
        if (pages[index].size() != expected || expected > MaxPixelBytes - bytes)
            return Core::failure(TextErrorCode::InvalidFont, "Bitmap atlas pixel length or capacity mismatch");
        bytes += expected;
    }
    return BitmapFontAtlas{std::move(font), std::move(pages)};
}

Core::Result<BitmapTextLayoutView> layoutBitmapTextInto(const BitmapFont& font, std::string_view utf8,
    BitmapTextOptions options, std::span<BitmapTextGlyph> storage)
try {
    if (utf8.size() > options.maxTextBytes)
        return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text byte limit exceeded");
    auto scalarCount = Core::countStrictUtf8CodepointsWithoutNul(utf8);
    if (!scalarCount) return Core::failure(TextErrorCode::InvalidText, "Bitmap text must be strict UTF-8 without NUL");
    if (*scalarCount > options.maxScalars || *scalarCount > storage.size())
        return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text scalar limit exceeded");
    if (!std::isfinite(options.scale) || options.scale <= 0 || !std::isfinite(options.extraLineSpacing) ||
        !std::isfinite(options.wrapWidth) || options.wrapWidth < 0)
        return Core::failure(TextErrorCode::InvalidText, "Invalid bitmap text layout options");
    BitmapTextLayoutView result;
    result.scalarCount = *scalarCount;
    result.baseline = font.descriptor().baseline * options.scale;
    const float lineHeight = font.descriptor().lineHeight * options.scale + options.extraLineSpacing;
    if (!std::isfinite(result.baseline) || !std::isfinite(lineHeight) || lineHeight <= 0)
        return Core::failure(TextErrorCode::InvalidText, "Invalid bitmap text line spacing");
    if (utf8.empty()) return result;
    result.lineCount = 1;
    Core::usize glyphCount = 0;
    Core::u32 previous = 0, scalarIndex = 0;
    float pen = 0;
    for (Core::u32 offset = 0; offset < utf8.size(); ++scalarIndex) {
        const Core::u32 begin = offset;
        const auto first = static_cast<unsigned char>(utf8[offset++]);
        Core::u32 codepoint = first;
        Core::u32 extra = 0;
        if (first >= 0xF0) { extra = 3; codepoint = first & 7U; }
        else if (first >= 0xE0) { extra = 2; codepoint = first & 15U; }
        else if (first >= 0xC0) { extra = 1; codepoint = first & 31U; }
        while (extra--) codepoint = (codepoint << 6U) | (static_cast<unsigned char>(utf8[offset++]) & 63U);
        if (codepoint == '\n') {
            if (result.lineCount == (std::numeric_limits<Core::u32>::max)())
                return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text line count overflow");
            result.width = (std::max)(result.width, pen); pen = 0; previous = 0; ++result.lineCount; continue;
        }
        const auto glyphIndex = font.glyphIndex(codepoint);
        const auto& glyph = font.descriptor().glyphs[glyphIndex];
        const float nominalAdvance = glyph.advance * options.scale;
        const float kerning = font.kerning(previous, glyph.codepoint);
        float adjustment = kerning * options.scale;
        float advance = (glyph.advance + kerning) * options.scale;
        if (!std::isfinite(nominalAdvance) || !std::isfinite(adjustment) || !std::isfinite(advance))
            return Core::failure(TextErrorCode::InvalidText, "Bitmap glyph advance overflow");
        if (options.wrapWidth > 0 && pen > 0 && pen + advance > options.wrapWidth) {
            if (result.lineCount == (std::numeric_limits<Core::u32>::max)())
                return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text line count overflow");
            result.width = (std::max)(result.width, pen); pen = 0; previous = 0; adjustment = 0; ++result.lineCount;
            advance = nominalAdvance;
        }
        const float originX = pen + adjustment;
        const float baselineY = static_cast<float>(result.lineCount - 1) * lineHeight + result.baseline;
        const float inkLeft = originX + glyph.bearingX * options.scale;
        const float inkTop = baselineY - glyph.bearingY * options.scale;
        const float inkRight = inkLeft + static_cast<float>(glyph.width) * options.scale;
        const float inkBottom = inkTop + static_cast<float>(glyph.height) * options.scale;
        if (!std::isfinite(originX) || !std::isfinite(baselineY) || !std::isfinite(inkLeft) ||
            !std::isfinite(inkTop) || !std::isfinite(inkRight) || !std::isfinite(inkBottom) || !std::isfinite(pen + advance))
            return Core::failure(TextErrorCode::InvalidText, "Bitmap glyph geometry overflow");
        const bool missing = glyph.codepoint != codepoint;
        storage[glyphCount++] = {glyphIndex, scalarIndex, begin, offset, result.lineCount - 1,
            originX, baselineY, advance, missing};
        result.missingGlyphCount += missing ? 1U : 0U;
        pen += advance;
        previous = glyph.codepoint;
    }
    result.width = (std::max)(result.width, pen);
    result.height = static_cast<float>(result.lineCount) * lineHeight - options.extraLineSpacing;
    if (!std::isfinite(result.width) || !std::isfinite(result.height) || !std::isfinite(result.baseline))
        return Core::failure(TextErrorCode::InvalidText, "Bitmap text layout overflow");
    result.glyphs = storage.first(glyphCount);
    return result;
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap text layout allocation failed"); }
  catch (const std::length_error&) { return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text storage exceeds addressable size"); }

Core::Result<BitmapTextLayout> layoutBitmapText(const BitmapFont& font, std::string_view utf8, BitmapTextOptions options)
try {
    if (utf8.size() > options.maxTextBytes) return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text byte limit exceeded");
    auto count = Core::countStrictUtf8CodepointsWithoutNul(utf8);
    if (!count) return Core::failure(TextErrorCode::InvalidText, "Bitmap text must be strict UTF-8 without NUL");
    if (*count > options.maxScalars) return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text scalar limit exceeded");
    BitmapTextLayout result;
    result.glyphs.resize(*count);
    auto view = layoutBitmapTextInto(font, utf8, options, result.glyphs);
    if (!view) return Core::failure(view.error());
    static_cast<BitmapTextMetrics&>(result) = *view;
    result.glyphs.resize(view->glyphs.size());
    return result;
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap text layout allocation failed"); }
  catch (const std::length_error&) { return Core::failure(TextErrorCode::CapacityExceeded, "Bitmap text storage exceeds addressable size"); }

} // namespace Tina::Text
