#include "UITextPaintEmitter.hpp"

#include "UILayoutPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] constexpr usize utf8UnitLength(unsigned char first) noexcept
{
    if (first <= 0x7FU)
    {
        return 1;
    }
    if ((first & 0xE0U) == 0xC0U)
    {
        return 2;
    }
    if ((first & 0xF0U) == 0xE0U)
    {
        return 3;
    }
    return 4;
}

} // namespace

void UITextPaintEmitter::append(std::pmr::vector<UICommittedPaintEntry>& output,
                                const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                                std::string_view utf8, const UITextStyle& style,
                                UIPremultipliedRgba8Color color, float startX, float startY,
                                const UITextPaintRasterSource& rasterSource, UITextPaintCursor* outCursor) noexcept
{
    const float lineStartX = outCursor != nullptr ? outCursor->baseX : startX;
    if (outCursor != nullptr)
    {
        *outCursor = UITextPaintCursor{
            .x = startX,
            .y = startY,
            .lineHeight = style.logicalSize * style.lineHeightScale,
            .baseX = lineStartX,
        };
    }
    if (utf8.empty() || color.isTransparent())
    {
        return;
    }

    const float fallbackAdvance = style.logicalSize * style.advanceScale;
    const float lineHeight = style.logicalSize * style.lineHeightScale;
    if (!(std::isfinite(fallbackAdvance) && fallbackAdvance > 0.0F && std::isfinite(lineHeight) &&
          lineHeight > 0.0F))
    {
        return;
    }
    const u32 pixelSize = static_cast<u32>((std::max)(1.0F, std::floor(style.logicalSize)));

    if (rasterSource.rasterizer != nullptr && rasterSource.face.hasValue() && rasterSource.atlas != nullptr)
    {
        auto batch = rasterSource.rasterizer->raster(rasterSource.face, utf8, style);
        if (batch)
        {
            const usize outputBase = output.size();
            const u32 ordinalBase = nextPaintOrdinal;
            float cursorX = startX;
            float cursorY = startY;
            usize glyphIndex = 0;
            usize index = 0;
            bool usedAtlasPath = true;
            while (index < utf8.size())
            {
                const auto first = static_cast<unsigned char>(utf8[index]);
                const usize unitLength = utf8UnitLength(first);
                if (unitLength > utf8.size() - index)
                {
                    usedAtlasPath = false;
                    break;
                }
                if (unitLength == 1 && first == '\n')
                {
                    cursorX = lineStartX;
                    cursorY = normalizeFloat(cursorY + lineHeight);
                    index += unitLength;
                    continue;
                }
                if (glyphIndex >= batch->glyphs.size())
                {
                    usedAtlasPath = false;
                    break;
                }

                const UITextGlyphRaster& glyph = batch->glyphs[glyphIndex];
                ++glyphIndex;
                float advance = glyph.advance;
                if (!(std::isfinite(advance) && advance > 0.0F))
                {
                    advance = fallbackAdvance;
                }

                if (glyph.width == 0 || glyph.height == 0)
                {
                    cursorX = normalizeFloat(cursorX + advance);
                    index += unitLength;
                    continue;
                }

                const usize coverageBytes = static_cast<usize>(glyph.width) * glyph.height;
                if (glyph.coverageOffset + coverageBytes > batch->coverage.size())
                {
                    usedAtlasPath = false;
                    break;
                }
                const std::span<const u8> coverage(batch->coverage.data() + glyph.coverageOffset, coverageBytes);
                auto placed = rasterSource.atlas->insert(
                    UIGlyphKey{
                        .face = rasterSource.face,
                        .codepoint = glyph.codepoint,
                        .pixelSize = pixelSize,
                    },
                    glyph, coverage);
                if (!placed)
                {
                    usedAtlasPath = false;
                    break;
                }

                const float drawX = normalizeFloat(cursorX + glyph.bearingX);
                const float drawY = normalizeFloat(cursorY + (lineHeight - glyph.bearingY));
                const float drawW = static_cast<float>(placed->width);
                const float drawH = static_cast<float>(placed->height);
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .worldRect =
                        UILogicalRect{
                            .x = drawX,
                            .y = drawY,
                            .width = normalizeFloat((std::max)(0.0F, drawW)),
                            .height = normalizeFloat((std::max)(0.0F, drawH)),
                        },
                    .effectiveClip = layoutEntry.effectiveClip,
                    .paintOrdinal = nextPaintOrdinal,
                    .solidFill = color,
                    .isGlyph = true,
                    .atlasX = placed->atlasX,
                    .atlasY = placed->atlasY,
                    .atlasWidth = placed->width,
                    .atlasHeight = placed->height,
                    .atlasPage = 0,
                });
                ++nextPaintOrdinal;
                cursorX = normalizeFloat(cursorX + advance);
                index += unitLength;
            }
            if (usedAtlasPath)
            {
                if (outCursor != nullptr)
                {
                    outCursor->x = cursorX;
                    outCursor->y = cursorY;
                    outCursor->lineHeight = lineHeight;
                    outCursor->baseX = lineStartX;
                }
                return;
            }

            output.resize(outputBase);
            nextPaintOrdinal = ordinalBase;
        }
    }

    float cursorX = startX;
    float cursorY = startY;
    usize index = 0;
    while (index < utf8.size())
    {
        const auto first = static_cast<unsigned char>(utf8[index]);
        const usize unitLength = utf8UnitLength(first);
        if (unitLength > utf8.size() - index)
        {
            break;
        }
        if (unitLength == 1 && first == '\n')
        {
            cursorX = lineStartX;
            cursorY = normalizeFloat(cursorY + lineHeight);
            index += unitLength;
            continue;
        }

        output.push_back(UICommittedPaintEntry{
            .node = layoutEntry.node,
            .worldRect =
                UILogicalRect{
                    .x = normalizeFloat(cursorX),
                    .y = normalizeFloat(cursorY),
                    .width = normalizeFloat(fallbackAdvance),
                    .height = normalizeFloat(lineHeight),
                },
            .effectiveClip = layoutEntry.effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = color,
            .isGlyph = false,
        });
        ++nextPaintOrdinal;
        cursorX = normalizeFloat(cursorX + fallbackAdvance);
        index += unitLength;
    }
    if (outCursor != nullptr)
    {
        outCursor->x = cursorX;
        outCursor->y = cursorY;
        outCursor->lineHeight = lineHeight;
        outCursor->baseX = lineStartX;
    }
}

} // namespace Tina::UI::Detail
