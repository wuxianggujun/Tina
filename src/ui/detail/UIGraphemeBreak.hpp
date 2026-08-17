#pragma once

#include <tina/core/base/Types.hpp>

#include <string_view>

namespace Tina::UI::Detail {

struct UIGraphemeCluster final {
    usize beginByte = 0;
    usize endByte = 0;
    u32 beginCodepoint = 0;
    u32 endCodepoint = 0;
};

// Allocation-free UAX #29 extended-grapheme subset over strict UTF-8.
// The caller-owned offsets advance monotonically and are changed only when a
// complete cluster is decoded. Invalid UTF-8 returns false.
[[nodiscard]] bool nextGraphemeCluster(
    std::string_view text, usize& byteOffset, u32& codepointOffset,
    UIGraphemeCluster& cluster) noexcept;

[[nodiscard]] bool isGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept;

// Strict movement helpers clamp offsets outside the text. An offset inside a
// cluster moves to that cluster's begin/end respectively.
[[nodiscard]] u32 previousGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept;
[[nodiscard]] u32 nextGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept;
[[nodiscard]] u32 graphemeBoundaryAtOrAfter(
    std::string_view text, u32 codepointOffset) noexcept;

// Used when authored text replaces the underlying storage. Equal scalar
// distance chooses the trailing boundary so an existing caret after a base
// scalar does not move in front of newly attached marks.
[[nodiscard]] u32 nearestGraphemeBoundary(
    std::string_view text, u32 codepointOffset) noexcept;

} // namespace Tina::UI::Detail\n
