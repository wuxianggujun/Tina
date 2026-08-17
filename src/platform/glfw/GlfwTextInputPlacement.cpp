#include "GlfwTextInputPlacement.hpp"

#include <tina/platform/PlatformErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace Tina::Platform::Detail {
namespace {

[[nodiscard]] Core::Error invalidPlacement(std::string_view message) noexcept
{
    return Core::Error{Core::CoreErrorCode::InvalidArgument, message};
}

[[nodiscard]] bool finite(double value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] double clampLogical(double value, double extent) noexcept
{
    return (std::clamp)(value, 0.0, extent);
}

[[nodiscard]] Core::Result<i32> toPixel(double logical, float scale) noexcept
{
    const double scaled = std::round(logical * static_cast<double>(scale));
    if (!finite(scaled) || scaled < static_cast<double>((std::numeric_limits<i32>::min)()) ||
        scaled > static_cast<double>((std::numeric_limits<i32>::max)()))
    {
        return Core::failure(invalidPlacement("Text input placement exceeds the native client-pixel range"));
    }
    return static_cast<i32>(scaled);
}

} // namespace

Core::Result<GlfwTextInputPlacementPixels>
resolveGlfwTextInputPlacement(const TextInputPlacement& placement,
                              const WindowMetricsSnapshot& metrics) noexcept
{
    if (!placement.window.hasValue() || placement.window != metrics.window)
    {
        return Core::failure(invalidPlacement("Text input placement belongs to another window"));
    }
    const TextInputCaretRect& caret = placement.caret;
    if (!finite(caret.x) || !finite(caret.y) || !finite(caret.width) || !finite(caret.height) ||
        caret.width < 0.0 || caret.height <= 0.0 ||
        !finite(caret.x + caret.width) || !finite(caret.y + caret.height))
    {
        return Core::failure(invalidPlacement("Text input caret geometry is not finite and positive"));
    }
    if (metrics.logicalExtent.width == 0 || metrics.logicalExtent.height == 0 ||
        !finite(metrics.contentScale.x) || !finite(metrics.contentScale.y) ||
        metrics.contentScale.x <= 0.0F || metrics.contentScale.y <= 0.0F)
    {
        return Core::failure(Core::Error{
            PlatformErrorCode::BackendOperationFailed,
            "Window metrics cannot convert text input placement"});
    }

    const double logicalWidth = static_cast<double>(metrics.logicalExtent.width);
    const double logicalHeight = static_cast<double>(metrics.logicalExtent.height);
    const double left = clampLogical(caret.x, logicalWidth);
    const double top = clampLogical(caret.y, logicalHeight);
    const double right = clampLogical(caret.x + caret.width, logicalWidth);
    const double bottom = clampLogical(caret.y + caret.height, logicalHeight);
    auto leftPx = toPixel(left, metrics.contentScale.x);
    auto topPx = toPixel(top, metrics.contentScale.y);
    auto rightPx = toPixel((std::max)(left, right), metrics.contentScale.x);
    auto bottomPx = toPixel((std::max)(top, bottom), metrics.contentScale.y);
    if (!leftPx || !topPx || !rightPx || !bottomPx)
    {
        if (!leftPx) return std::unexpected(std::move(leftPx.error()));
        if (!topPx) return std::unexpected(std::move(topPx.error()));
        if (!rightPx) return std::unexpected(std::move(rightPx.error()));
        return std::unexpected(std::move(bottomPx.error()));
    }

    return GlfwTextInputPlacementPixels{
        .caretLeft = *leftPx,
        .caretTop = *topPx,
        .caretRight = *rightPx,
        .caretBottom = *bottomPx,
        .candidateX = *leftPx,
        .candidateY = *bottomPx,
    };
}

} // namespace Tina::Platform::Detail
