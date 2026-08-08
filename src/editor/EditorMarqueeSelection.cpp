#include <tina/editor/EditorMarqueeSelection.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Editor {
namespace {

[[nodiscard]] bool isValidMode(EditorMarqueeSelectionMode mode) noexcept
{
    return mode == EditorMarqueeSelectionMode::Replace ||
           mode == EditorMarqueeSelectionMode::Add ||
           mode == EditorMarqueeSelectionMode::Toggle;
}

[[nodiscard]] bool isFinite(EditorMarqueeScreenRect rect) noexcept
{
    return std::isfinite(rect.x0) && std::isfinite(rect.y0) &&
           std::isfinite(rect.x1) && std::isfinite(rect.y1);
}

[[nodiscard]] EditorMarqueeScreenRect normalized(
    EditorMarqueeScreenRect rect) noexcept
{
    if (rect.x0 > rect.x1) {
        std::swap(rect.x0, rect.x1);
    }
    if (rect.y0 > rect.y1) {
        std::swap(rect.y0, rect.y1);
    }
    return rect;
}

[[nodiscard]] bool overlaps(EditorMarqueeScreenRect lhs,
                            EditorMarqueeScreenRect rhs) noexcept
{
    lhs = normalized(lhs);
    rhs = normalized(rhs);
    return lhs.x0 <= rhs.x1 && lhs.x1 >= rhs.x0 && lhs.y0 <= rhs.y1 &&
           lhs.y1 >= rhs.y0;
}

template <typename Value>
[[nodiscard]] bool hasDuplicates(std::span<const Value> values) noexcept
{
    for (Core::usize index = 0; index < values.size(); ++index) {
        if (std::find(values.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                      values.end(), values[index]) != values.end()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasDuplicateCandidateIds(
    std::span<const EditorMarqueeCandidate> candidates) noexcept
{
    for (Core::usize index = 0; index < candidates.size(); ++index) {
        const Core::u64 stableId = candidates[index].stableId;
        const auto tail = candidates.subspan(index + 1U);
        if (std::ranges::any_of(tail, [stableId](const auto& candidate) {
                return candidate.stableId == stableId;
            })) {
            return true;
        }
    }
    return false;
}

template <typename Output>
[[nodiscard]] bool appendBounded(Output& output, Core::usize& count,
                                 Core::u64 stableId) noexcept
{
    if (count >= output.size()) {
        return false;
    }
    output[count++] = stableId;
    return true;
}

template <typename Output>
[[nodiscard]] bool mergeUnion(std::span<const Core::u64> lhs,
                              std::span<const Core::u64> rhs, Output& output,
                              Core::usize& count) noexcept
{
    Core::usize left = 0;
    Core::usize right = 0;
    while (left < lhs.size() || right < rhs.size()) {
        Core::u64 value = 0;
        if (right == rhs.size() ||
            (left < lhs.size() && lhs[left] < rhs[right])) {
            value = lhs[left++];
        } else if (left == lhs.size() || rhs[right] < lhs[left]) {
            value = rhs[right++];
        } else {
            value = lhs[left];
            ++left;
            ++right;
        }
        if (!appendBounded(output, count, value)) {
            return false;
        }
    }
    return true;
}

template <typename Output>
[[nodiscard]] bool mergeSymmetricDifference(
    std::span<const Core::u64> lhs, std::span<const Core::u64> rhs,
    Output& output, Core::usize& count) noexcept
{
    Core::usize left = 0;
    Core::usize right = 0;
    while (left < lhs.size() || right < rhs.size()) {
        if (right == rhs.size() ||
            (left < lhs.size() && lhs[left] < rhs[right])) {
            if (!appendBounded(output, count, lhs[left++])) {
                return false;
            }
        } else if (left == lhs.size() || rhs[right] < lhs[left]) {
            if (!appendBounded(output, count, rhs[right++])) {
                return false;
            }
        } else {
            ++left;
            ++right;
        }
    }
    return true;
}

template <typename Output>
void setDifference(std::span<const Core::u64> lhs,
                   std::span<const Core::u64> rhs, Output& output,
                   Core::usize& count) noexcept
{
    Core::usize left = 0;
    Core::usize right = 0;
    while (left < lhs.size()) {
        while (right < rhs.size() && rhs[right] < lhs[left]) {
            ++right;
        }
        if (right == rhs.size() || lhs[left] < rhs[right]) {
            output[count++] = lhs[left];
        }
        ++left;
    }
}

} // namespace

Core::Result<EditorMarqueeSelection> EditorMarqueeSelection::Evaluate(
    EditorMarqueeScreenRect marquee,
    std::span<const EditorMarqueeCandidate> candidates,
    std::span<const Core::u64> currentSelection,
    EditorMarqueeSelectionMode mode)
{
    if (!isValidMode(mode) || !isFinite(marquee)) {
        return Core::failure(
            EditorErrorCode::InvalidConfiguration,
            "Editor marquee selection requires a finite rectangle and valid mode");
    }
    if (candidates.size() > EditorMarqueeSelectionCapacity ||
        currentSelection.size() > EditorMarqueeSelectionCapacity) {
        return Core::failure(
            EditorErrorCode::DocumentCapacityExceeded,
            "Editor marquee selection input exceeds its fixed capacity");
    }
    if (std::ranges::any_of(currentSelection,
                            [](Core::u64 stableId) { return stableId == 0U; }) ||
        std::ranges::any_of(candidates, [](const auto& candidate) {
            return candidate.stableId == 0U;
        }) ||
        hasDuplicates(currentSelection) ||
        hasDuplicateCandidateIds(candidates)) {
        return Core::failure(
            EditorErrorCode::InvalidConfiguration,
            "Editor marquee selection requires unique non-zero stable ids");
    }
    if (std::ranges::any_of(candidates, [](const auto& candidate) {
            return !isFinite(candidate.screenBounds);
        })) {
        return Core::failure(
            EditorErrorCode::InvalidConfiguration,
            "Editor marquee candidate bounds must be finite");
    }

    std::array<Core::u64, EditorMarqueeSelectionCapacity> current{};
    std::copy(currentSelection.begin(), currentSelection.end(), current.begin());
    std::sort(current.begin(), current.begin() + currentSelection.size());
    const std::span<const Core::u64> orderedCurrent(current.data(),
                                                    currentSelection.size());

    std::array<Core::u64, EditorMarqueeSelectionCapacity> hits{};
    Core::usize hitCount = 0;
    marquee = normalized(marquee);
    for (const EditorMarqueeCandidate& candidate : candidates) {
        if (overlaps(marquee, candidate.screenBounds)) {
            hits[hitCount++] = candidate.stableId;
        }
    }
    std::sort(hits.begin(), hits.begin() + hitCount);
    const std::span<const Core::u64> orderedHits(hits.data(), hitCount);

    EditorMarqueeSelection result;
    bool withinCapacity = true;
    if (mode == EditorMarqueeSelectionMode::Replace) {
        std::copy(orderedHits.begin(), orderedHits.end(), result.m_selection.begin());
        result.m_selectionCount = orderedHits.size();
    } else if (mode == EditorMarqueeSelectionMode::Add) {
        withinCapacity = mergeUnion(orderedCurrent, orderedHits,
                                    result.m_selection,
                                    result.m_selectionCount);
    } else {
        withinCapacity = mergeSymmetricDifference(
            orderedCurrent, orderedHits, result.m_selection,
            result.m_selectionCount);
    }
    if (!withinCapacity) {
        return Core::failure(
            EditorErrorCode::DocumentCapacityExceeded,
            "Editor marquee selection result exceeds its fixed capacity");
    }

    const std::span<const Core::u64> orderedSelection(
        result.m_selection.data(), result.m_selectionCount);
    setDifference(orderedSelection, orderedCurrent, result.m_added,
                  result.m_addedCount);
    setDifference(orderedCurrent, orderedSelection, result.m_removed,
                  result.m_removedCount);
    return result;
}

} // namespace Tina::Editor
