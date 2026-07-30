#pragma once

#include <tina/ui/UICommittedHit.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIFocus.hpp>

#include "UILayoutPrimitives.hpp"
#include "UIPointerRoutePath.hpp"
#include <cmath>
#include <span>

namespace Tina::UI::Detail {

[[nodiscard]] constexpr bool isValidPointerHitPolicy(
    UIPointerHitPolicy policy) noexcept
{
    switch (policy)
    {
    case UIPointerHitPolicy::Ignore:
    case UIPointerHitPolicy::Targetable:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isValidFocusScopeMode(UIFocusScopeMode mode) noexcept
{
    return mode == UIFocusScopeMode::None || mode == UIFocusScopeMode::Contain;
}

[[nodiscard]] constexpr bool isValidRoutedPointerEventKind(
    UIRoutedPointerEventKind kind) noexcept
{
    switch (kind)
    {
    case UIRoutedPointerEventKind::Move:
    case UIRoutedPointerEventKind::ButtonDown:
    case UIRoutedPointerEventKind::ButtonUp:
    case UIRoutedPointerEventKind::Wheel:
    case UIRoutedPointerEventKind::PointerCancel:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isValidEventPhaseMask(UIEventPhaseMask phases) noexcept
{
    const auto bits = static_cast<u8>(phases);
    const auto allBits = static_cast<u8>(UIEventPhaseMask::All);
    return bits != 0 && (bits & static_cast<u8>(~allBits)) == 0;
}

[[nodiscard]] constexpr UIEventPhaseMask phaseMaskFor(UIEventPhase phase) noexcept
{
    switch (phase)
    {
    case UIEventPhase::Capture:
        return UIEventPhaseMask::Capture;
    case UIEventPhase::Target:
        return UIEventPhaseMask::Target;
    case UIEventPhase::Bubble:
        return UIEventPhaseMask::Bubble;
    }
    return UIEventPhaseMask::None;
}

[[nodiscard]] constexpr u32 findHitEntryIndex(
    UINodeId node, std::span<const UICommittedHitEntry> entries) noexcept
{
    for (u32 index = 0; index < entries.size(); ++index)
    {
        if (entries[index].node == node)
        {
            return index;
        }
    }
    return InvalidUIHitEntryIndex;
}

[[nodiscard]] constexpr bool hitEntryIsWithinScope(
    u32 entryIndex, u32 scopeEntryIndex,
    std::span<const UICommittedHitEntry> entries) noexcept
{
    if (scopeEntryIndex == InvalidUIHitEntryIndex)
    {
        return true;
    }
    usize visited = 0;
    while (entryIndex != InvalidUIHitEntryIndex && entryIndex < entries.size())
    {
        if (entryIndex == scopeEntryIndex)
        {
            return true;
        }
        entryIndex = entries[entryIndex].parentEntryIndex;
        if (++visited > entries.size())
        {
            return false;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool hitEntryAllowedByModal(
    const UICommittedHitEntry& entry, u32 activeModalEntryIndex) noexcept
{
    return activeModalEntryIndex == InvalidUIHitEntryIndex ||
           entry.modalScopeEntryIndex == activeModalEntryIndex;
}

[[nodiscard]] constexpr bool hitEntryAllowsPointerInteraction(
    u32 entryIndex, std::span<const UICommittedHitEntry> entries,
    u32 activeModalEntryIndex) noexcept
{
    return entryIndex < entries.size() &&
           entries[entryIndex].policy == UIPointerHitPolicy::Targetable &&
           hitEntryAllowedByModal(entries[entryIndex], activeModalEntryIndex);
}

[[nodiscard]] constexpr bool hitEntryAllowsPointerCapture(
    u32 entryIndex, std::span<const UICommittedHitEntry> entries,
    u32 activeModalEntryIndex) noexcept
{
    return entryIndex < entries.size() &&
           hitEntryAllowedByModal(entries[entryIndex], activeModalEntryIndex);
}

[[nodiscard]] constexpr bool hitEntryAllowsKeyboardFocus(
    u32 entryIndex, std::span<const UICommittedHitEntry> entries,
    u32 activeModalEntryIndex) noexcept
{
    return hitEntryAllowsPointerInteraction(
               entryIndex, entries, activeModalEntryIndex) &&
           hasBehavior(entries[entryIndex].behaviors,
                       UIElementBehavior::Focusable);
}

[[nodiscard]] inline UIPointerHitQueryResult queryCommittedPointerHit(
    UICommittedHitView hit, UILogicalPoint point) noexcept
{
    const std::span<const UICommittedHitEntry> entries = hit.entries();
    UIPointerHitQueryResult result{
        .structureRevision = hit.structureRevision(),
        .layoutRevision = hit.layoutRevision(),
        .paintOrderRevision = hit.paintOrderRevision(),
        .hitRevision = hit.hitRevision(),
        .modalBarrierActive = hit.activeModalEntryIndex() < entries.size(),
    };
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return result;
    }

    for (usize reverseIndex = entries.size(); reverseIndex > 0; --reverseIndex)
    {
        ++result.visitedEntryCount;
        const usize entryIndex = reverseIndex - 1;
        const UICommittedHitEntry& entry = entries[entryIndex];
        if ((result.modalBarrierActive &&
             !hitEntryAllowedByModal(entry, hit.activeModalEntryIndex())) ||
            entry.policy != UIPointerHitPolicy::Targetable ||
            !containsPointHalfOpen(entry.worldRect, point) ||
            !containsPointHalfOpen(entry.effectiveClip, point) ||
            entry.rootEntryIndex >= entries.size())
        {
            continue;
        }

        result.target = pointerHitTargetForEntry(
            entries, static_cast<u32>(entryIndex));
        return result;
    }
    return result;
}

} // namespace Tina::UI::Detail
