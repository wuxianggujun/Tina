#include "UIPaintPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

[[nodiscard]] bool hasRoundedCorners(const UILogicalCornerRadii& radii) noexcept
{
    return radii.topLeft > 0.0F || radii.topRight > 0.0F ||
           radii.bottomRight > 0.0F || radii.bottomLeft > 0.0F;
}

[[nodiscard]] UILogicalCornerRadii insetCornerRadii(
    const UILogicalCornerRadii& radii, float inset) noexcept
{
    return {
        .topLeft = (std::max)(0.0F, radii.topLeft - inset),
        .topRight = (std::max)(0.0F, radii.topRight - inset),
        .bottomRight = (std::max)(0.0F, radii.bottomRight - inset),
        .bottomLeft = (std::max)(0.0F, radii.bottomLeft - inset),
    };
}

[[nodiscard]] UIStraightSrgba8Color roundedBorderColor(const UIBoxPaint& paint) noexcept
{
    // Rounded chrome uses one outer ring. Prefer the bottom/right tone so
    // swapping light/dark on press still produces a visible depth response.
    return paint.borderDark.alpha != 0 ? paint.borderDark : paint.borderLight;
}

[[nodiscard]] bool validLineGeometry(const UIBoxPaint& paint) noexcept
{
    const float length = std::hypot(
        paint.line.end.x - paint.line.start.x,
        paint.line.end.y - paint.line.start.y);
    return paint.primitive == UIBoxPrimitiveKind::Line &&
           std::isfinite(paint.line.start.x) && std::isfinite(paint.line.start.y) &&
           std::isfinite(paint.line.end.x) && std::isfinite(paint.line.end.y) &&
           std::isfinite(paint.line.thickness) && paint.line.thickness > 0.0F &&
           std::isfinite(length) && length > 0.0F;
}

[[nodiscard]] bool validEllipseGeometry(const UIBoxPaint& paint,
                                        const UILogicalRect& worldRect) noexcept
{
    return paint.primitive == UIBoxPrimitiveKind::Ellipse &&
           std::isfinite(worldRect.width) && worldRect.width > 0.0F &&
           std::isfinite(worldRect.height) && worldRect.height > 0.0F &&
           std::isfinite(paint.ellipseStrokeWidth) &&
           paint.ellipseStrokeWidth >= 0.0F &&
           paint.ellipseStrokeWidth <=
               (std::min)(worldRect.width, worldRect.height) * 0.5F;
}

} // namespace

std::optional<UICommittedLineGeometry> resolveCommittedLineGeometry(
    const UILineGeometry& line, UILogicalPoint worldOrigin) noexcept
{
    const double localDeltaX = static_cast<double>(line.end.x) - line.start.x;
    const double localDeltaY = static_cast<double>(line.end.y) - line.start.y;
    const double localLength = std::hypot(localDeltaX, localDeltaY);
    if (!std::isfinite(line.start.x) || !std::isfinite(line.start.y) ||
        !std::isfinite(line.end.x) || !std::isfinite(line.end.y) ||
        !std::isfinite(line.thickness) || line.thickness <= 0.0F ||
        !std::isfinite(worldOrigin.x) || !std::isfinite(worldOrigin.y) ||
        !std::isfinite(localLength) || localLength <= 0.0)
    {
        return std::nullopt;
    }

    static constexpr double MaximumFloat =
        static_cast<double>((std::numeric_limits<float>::max)());
    const auto finiteFloat = [](double value) noexcept -> std::optional<float> {
        if (!std::isfinite(value) || value < -MaximumFloat || value > MaximumFloat)
        {
            return std::nullopt;
        }
        const float converted = static_cast<float>(value);
        return std::isfinite(converted)
                   ? std::optional<float>{normalizeFloat(converted)}
                   : std::nullopt;
    };
    const auto finiteFloatAtOrBelow = [&](double value) noexcept
        -> std::optional<float> {
        auto converted = finiteFloat(value);
        if (!converted)
        {
            return std::nullopt;
        }
        if (static_cast<double>(*converted) > value)
        {
            *converted = std::nextafter(
                *converted, -(std::numeric_limits<float>::infinity)());
        }
        return std::isfinite(*converted)
                   ? std::optional<float>{normalizeFloat(*converted)}
                   : std::nullopt;
    };
    const auto finiteFloatAtOrAbove = [&](double value) noexcept
        -> std::optional<float> {
        auto converted = finiteFloat(value);
        if (!converted)
        {
            return std::nullopt;
        }
        if (static_cast<double>(*converted) < value)
        {
            *converted = std::nextafter(
                *converted, (std::numeric_limits<float>::infinity)());
        }
        return std::isfinite(*converted)
                   ? std::optional<float>{normalizeFloat(*converted)}
                   : std::nullopt;
    };

    const auto startX = finiteFloat(static_cast<double>(worldOrigin.x) + line.start.x);
    const auto startY = finiteFloat(static_cast<double>(worldOrigin.y) + line.start.y);
    const auto endX = finiteFloat(static_cast<double>(worldOrigin.x) + line.end.x);
    const auto endY = finiteFloat(static_cast<double>(worldOrigin.y) + line.end.y);
    if (!startX || !startY || !endX || !endY ||
        (*startX == *endX && *startY == *endY))
    {
        return std::nullopt;
    }

    const double deltaX = static_cast<double>(*endX) - *startX;
    const double deltaY = static_cast<double>(*endY) - *startY;
    const double length = std::hypot(deltaX, deltaY);
    const double halfThickness = static_cast<double>(line.thickness) * 0.5;
    if (!std::isfinite(length) || length <= 0.0 || !std::isfinite(halfThickness))
    {
        return std::nullopt;
    }
    const double extentX = std::abs(deltaY / length) * halfThickness;
    const double extentY = std::abs(deltaX / length) * halfThickness;
    const double left = (std::min)(static_cast<double>(*startX),
                                   static_cast<double>(*endX)) - extentX;
    const double top = (std::min)(static_cast<double>(*startY),
                                  static_cast<double>(*endY)) - extentY;
    const double right = (std::max)(static_cast<double>(*startX),
                                    static_cast<double>(*endX)) + extentX;
    const double bottom = (std::max)(static_cast<double>(*startY),
                                     static_cast<double>(*endY)) + extentY;
    const auto envelopeX = finiteFloatAtOrBelow(left);
    const auto envelopeY = finiteFloatAtOrBelow(top);
    if (!envelopeX || !envelopeY)
    {
        return std::nullopt;
    }
    const auto envelopeWidth = finiteFloatAtOrAbove(
        right - static_cast<double>(*envelopeX));
    const auto envelopeHeight = finiteFloatAtOrAbove(
        bottom - static_cast<double>(*envelopeY));
    if (!envelopeWidth || !envelopeHeight ||
        *envelopeWidth <= 0.0F || *envelopeHeight <= 0.0F)
    {
        return std::nullopt;
    }

    return UICommittedLineGeometry{
        .worldEnvelope = {
            .x = *envelopeX,
            .y = *envelopeY,
            .width = *envelopeWidth,
            .height = *envelopeHeight,
        },
        .worldStart = {.x = *startX, .y = *startY},
        .worldEnd = {.x = *endX, .y = *endY},
    };
}

usize countBoxChromePaintEntries(const UIBoxPaint& paint,
                                 const UILogicalRect& worldRect,
                                 bool hasResolvedFill) noexcept
{
    if (paint.primitive == UIBoxPrimitiveKind::Ellipse)
    {
        return validEllipseGeometry(paint, worldRect) && hasResolvedFill
                   ? 1U
                   : 0U;
    }
    if (paint.primitive == UIBoxPrimitiveKind::Line)
    {
        return validLineGeometry(paint) && hasResolvedFill &&
                       resolveCommittedLineGeometry(
                           paint.line, {.x = worldRect.x, .y = worldRect.y})
                           .has_value()
                   ? 1U
                   : 0U;
    }
    if (paint.primitive != UIBoxPrimitiveKind::Rectangle)
    {
        return 0U;
    }
    usize count = hasDrawableShadow(paint, worldRect) ? 1U : 0U;
    const bool hasBorder = hasDrawableBorder(paint, worldRect);
    if (hasRoundedCorners(paint.cornerRadii) && hasResolvedFill && hasBorder)
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
    if (paint.primitive == UIBoxPrimitiveKind::Ellipse)
    {
        if (!resolvedFill.isTransparent() && validEllipseGeometry(paint, worldRect))
        {
            output.push_back(UICommittedPaintEntry{
                .node = node,
                .worldRect = worldRect,
                .effectiveClip = effectiveClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = resolvedFill,
                .kind = UICommittedPaintKind::SolidEllipse,
                .ellipseStrokeWidth = paint.ellipseStrokeWidth,
            });
            ++nextPaintOrdinal;
        }
        return;
    }
    if (paint.primitive == UIBoxPrimitiveKind::Line)
    {
        const auto geometry = resolveCommittedLineGeometry(
            paint.line, {.x = worldRect.x, .y = worldRect.y});
        if (validLineGeometry(paint) && geometry.has_value() &&
            !resolvedFill.isTransparent())
        {
            output.push_back(UICommittedPaintEntry{
                .node = node,
                .worldRect = geometry->worldEnvelope,
                .effectiveClip = effectiveClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = resolvedFill,
                .kind = UICommittedPaintKind::SolidLine,
                .lineStart = geometry->worldStart,
                .lineEnd = geometry->worldEnd,
                .lineThickness = paint.line.thickness,
            });
            ++nextPaintOrdinal;
        }
        return;
    }
    if (paint.primitive != UIBoxPrimitiveKind::Rectangle)
    {
        return;
    }
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
            .cornerRadii = paint.cornerRadii,
        });
        ++nextPaintOrdinal;
    }

    const bool hasBorder = hasDrawableBorder(paint, worldRect);
    if (hasRoundedCorners(paint.cornerRadii) && !resolvedFill.isTransparent() && hasBorder)
    {
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect = worldRect,
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = premultiply(roundedBorderColor(paint)),
            .cornerRadii = paint.cornerRadii,
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
            .cornerRadii = insetCornerRadii(paint.cornerRadii, inset),
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
            .cornerRadii = paint.cornerRadii,
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
