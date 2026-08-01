#pragma once

#include <tina/ui/UIPaint.hpp>

#include <array>

namespace Tina::UI::Detail {

struct UINineSlicePatch final {
    UILogicalRect worldRect{};
    UILogicalPoint worldEnd{};
    UIImagePixelRect sourcePixels{};
};

struct UINineSlicePatchBatch final {
    std::array<UINineSlicePatch, 9> patches{};
    usize count = 0;
};

// Builds the exact row-major patch set without allocation. The command is
// assumed to have passed retained Canvas validation.
[[nodiscard]] UINineSlicePatchBatch makeNineSlicePatches(
    const UILogicalRect& elementWorldRect, const UICanvasCommand& command) noexcept;

} // namespace Tina::UI::Detail
