#include "UINineSlicePaintEmitter.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] constexpr float normalizeFloat(float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] std::array<float, 4> makeDestinationCuts(
    float origin, float extent, float leadingInset, float trailingInset) noexcept
{
    const float end = normalizeFloat(origin + extent);
    const double fixedExtent = static_cast<double>(leadingInset) + static_cast<double>(trailingInset);
    if (fixedExtent > 0.0 && static_cast<double>(extent) <= fixedExtent)
    {
        const float compressedLeading = normalizeFloat(static_cast<float>(
            static_cast<double>(extent) * static_cast<double>(leadingInset) / fixedExtent));
        const float sharedMiddle = std::clamp(normalizeFloat(origin + compressedLeading), origin, end);
        return {origin, sharedMiddle, sharedMiddle, end};
    }

    const float leading = std::clamp(normalizeFloat(origin + leadingInset), origin, end);
    const float trailing = std::clamp(normalizeFloat(end - trailingInset), leading, end);
    return {origin, leading, trailing, end};
}

void appendPatch(UINineSlicePatchBatch& output, UILogicalRect worldRect, UILogicalPoint worldEnd,
                 UIImagePixelRect sourcePixels) noexcept
{
    if (output.count < output.patches.size())
    {
        output.patches[output.count++] = UINineSlicePatch{
            .worldRect = worldRect,
            .worldEnd = worldEnd,
            .sourcePixels = sourcePixels,
        };
    }
}

} // namespace

UINineSlicePatchBatch makeNineSlicePatches(
    const UILogicalRect& elementWorldRect, const UICanvasCommand& command) noexcept
{
    UINineSlicePatchBatch output;
    if (command.color.alpha == 0 || command.bounds.width <= 0.0F || command.bounds.height <= 0.0F)
    {
        return output;
    }

    const UIImagePixelRect source = command.imageSource.sourcePixels;
    const UIImagePixelInsets sourceInsets = command.imageSourceInsets;
    const std::array<u32, 4> sourceX{
        source.x,
        source.x + sourceInsets.left,
        source.x + source.width - sourceInsets.right,
        source.x + source.width,
    };
    const std::array<u32, 4> sourceY{
        source.y,
        source.y + sourceInsets.top,
        source.y + source.height - sourceInsets.bottom,
        source.y + source.height,
    };

    const float destinationX = normalizeFloat(elementWorldRect.x + command.bounds.x);
    const float destinationY = normalizeFloat(elementWorldRect.y + command.bounds.y);
    if (!std::isfinite(destinationX) || !std::isfinite(destinationY) ||
        !std::isfinite(destinationX + command.bounds.width) ||
        !std::isfinite(destinationY + command.bounds.height))
    {
        return output;
    }
    const std::array<float, 4> destinationXCut = makeDestinationCuts(
        destinationX, command.bounds.width, command.imageDestinationInsets.left,
        command.imageDestinationInsets.right);
    const std::array<float, 4> destinationYCut = makeDestinationCuts(
        destinationY, command.bounds.height, command.imageDestinationInsets.top,
        command.imageDestinationInsets.bottom);

    for (usize row = 0; row < 3; ++row)
    {
        const u32 sourceHeight = sourceY[row + 1] - sourceY[row];
        const float destinationHeight = normalizeFloat(destinationYCut[row + 1] - destinationYCut[row]);
        if (sourceHeight == 0 || destinationHeight <= 0.0F)
        {
            continue;
        }
        for (usize column = 0; column < 3; ++column)
        {
            const u32 sourceWidth = sourceX[column + 1] - sourceX[column];
            const float destinationWidth = normalizeFloat(destinationXCut[column + 1] - destinationXCut[column]);
            if (sourceWidth == 0 || destinationWidth <= 0.0F)
            {
                continue;
            }
            appendPatch(
                output,
                UILogicalRect{
                    .x = destinationXCut[column],
                    .y = destinationYCut[row],
                    .width = destinationWidth,
                    .height = destinationHeight,
                },
                UILogicalPoint{
                    .x = destinationXCut[column + 1],
                    .y = destinationYCut[row + 1],
                },
                UIImagePixelRect{
                    .x = sourceX[column],
                    .y = sourceY[row],
                    .width = sourceWidth,
                    .height = sourceHeight,
                });
        }
    }
    return output;
}

} // namespace Tina::UI::Detail
