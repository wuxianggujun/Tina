#include "UIPaintPrimitives.hpp"

#include <algorithm>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] float normalizeFloat(float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] bool hasDrawableShadow(const UIBoxPaint& paint,
                                     const UILogicalRect& worldRect) noexcept
{
    return paint.shadow.alpha != 0 &&
           (paint.shadowOffsetX != 0.0F || paint.shadowOffsetY != 0.0F) &&
           worldRect.width > 0.0F && worldRect.height > 0.0F;
}

[[nodiscard]] bool hasDrawableBorder(const UIBoxPaint& paint,
                                     const UILogicalRect& worldRect) noexcept
{
    return paint.borderWidth > 0.0F &&
           (paint.borderLight.alpha != 0 || paint.borderDark.alpha != 0) &&
           worldRect.width > paint.borderWidth * 2.0F &&
           worldRect.height > paint.borderWidth * 2.0F;
}

[[nodiscard]] UIStraightSrgba8Color roundedBorderColor(const UIBoxPaint& paint) noexcept
{
    // Rounded chrome uses one outer ring. Prefer the bottom/right tone so
    // swapping light/dark on press still produces a visible depth response.
    return paint.borderDark.alpha != 0 ? paint.borderDark : paint.borderLight;
}

} // namespace

usize countBoxChromePaintEntries(const UIBoxPaint& paint,
                                 const UILogicalRect& worldRect,
                                 bool hasResolvedFill) noexcept
{
    usize count = hasDrawableShadow(paint, worldRect) ? 1U : 0U;
    const bool hasBorder = hasDrawableBorder(paint, worldRect);
    if (paint.cornerRadius > 0.0F && hasResolvedFill && hasBorder)
    {
        return count + 2U;
    }
    if (hasResolvedFill)
    {
        ++count;
    }
    if (hasBorder)
    {
        if (paint.borderLight.alpha != 0)
        {
            count += 2;
        }
        if (paint.borderDark.alpha != 0)
        {
            count += 2;
        }
    }
    return count;
}

void appendBoxChromePaints(std::pmr::vector<UICommittedPaintEntry>& output,
                           UINodeId node, const UILogicalRect& worldRect,
                           const UILogicalRect& effectiveClip,
                           u32& nextPaintOrdinal, const UIBoxPaint& paint,
                           UIPremultipliedRgba8Color resolvedFill) noexcept
{
    if (hasDrawableShadow(paint, worldRect))
    {
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = normalizeFloat(worldRect.x + paint.shadowOffsetX),
                    .y = normalizeFloat(worldRect.y + paint.shadowOffsetY),
                    .width = worldRect.width,
                    .height = worldRect.height,
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = premultiply(paint.shadow),
            .cornerRadius = paint.cornerRadius,
        });
        ++nextPaintOrdinal;
    }

    const bool hasBorder = hasDrawableBorder(paint, worldRect);
    if (paint.cornerRadius > 0.0F && !resolvedFill.isTransparent() && hasBorder)
    {
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect = worldRect,
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = premultiply(roundedBorderColor(paint)),
            .cornerRadius = paint.cornerRadius,
        });
        ++nextPaintOrdinal;

        const float inset = paint.borderWidth;
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = normalizeFloat(worldRect.x + inset),
                    .y = normalizeFloat(worldRect.y + inset),
                    .width = normalizeFloat(worldRect.width - inset * 2.0F),
                    .height = normalizeFloat(worldRect.height - inset * 2.0F),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = resolvedFill,
            .cornerRadius = (std::max)(0.0F, paint.cornerRadius - inset),
        });
        ++nextPaintOrdinal;
        return;
    }

    if (!resolvedFill.isTransparent())
    {
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect = worldRect,
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = resolvedFill,
            .cornerRadius = paint.cornerRadius,
        });
        ++nextPaintOrdinal;
    }
    if (!hasBorder)
    {
        return;
    }
    const float borderWidth = paint.borderWidth;
    if (paint.borderLight.alpha != 0)
    {
        const UIPremultipliedRgba8Color light = premultiply(paint.borderLight);
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = worldRect.x,
                    .y = worldRect.y,
                    .width = worldRect.width,
                    .height = normalizeFloat(borderWidth),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = light,
        });
        ++nextPaintOrdinal;
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = worldRect.x,
                    .y = normalizeFloat(worldRect.y + borderWidth),
                    .width = normalizeFloat(borderWidth),
                    .height = normalizeFloat(worldRect.height - borderWidth),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = light,
        });
        ++nextPaintOrdinal;
    }
    if (paint.borderDark.alpha != 0)
    {
        const UIPremultipliedRgba8Color dark = premultiply(paint.borderDark);
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = worldRect.x,
                    .y = normalizeFloat(worldRect.y + worldRect.height -
                                        borderWidth),
                    .width = worldRect.width,
                    .height = normalizeFloat(borderWidth),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = dark,
        });
        ++nextPaintOrdinal;
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = normalizeFloat(worldRect.x + worldRect.width -
                                        borderWidth),
                    .y = worldRect.y,
                    .width = normalizeFloat(borderWidth),
                    .height = normalizeFloat(worldRect.height - borderWidth),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = dark,
        });
        ++nextPaintOrdinal;
    }
}

usize countDrawableTextCodepoints(std::string_view utf8) noexcept
{
    usize count = 0;
    usize index = 0;
    while (index < utf8.size())
    {
        const auto first = static_cast<unsigned char>(utf8[index]);
        usize unitLength = 1;
        if (first <= 0x7FU)
        {
            unitLength = 1;
        } else if ((first & 0xE0U) == 0xC0U)
        {
            unitLength = 2;
        } else if ((first & 0xF0U) == 0xE0U)
        {
            unitLength = 3;
        } else
        {
            unitLength = 4;
        }
        if (unitLength > utf8.size() - index)
        {
            break;
        }
        if (!(unitLength == 1 && first == '\n'))
        {
            ++count;
        }
        index += unitLength;
    }
    return count;
}

UIPremultipliedRgba8Color applyOpacity(UIPremultipliedRgba8Color color,
                                       u8 opacity) noexcept
{
    const auto scale = [opacity](u8 channel) noexcept -> u8 {
        return static_cast<u8>(
            (static_cast<u16>(channel) * static_cast<u16>(opacity) + u16{127}) /
            u16{255});
    };
    return UIPremultipliedRgba8Color{
        .red = scale(color.red),
        .green = scale(color.green),
        .blue = scale(color.blue),
        .alpha = scale(color.alpha),
    };
}

} // namespace Tina::UI::Detail
