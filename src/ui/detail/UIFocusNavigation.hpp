#pragma once

#include <tina/ui/UICommittedHit.hpp>
#include <tina/ui/UIFocus.hpp>

#include <algorithm>
#include <span>

namespace Tina::UI::Detail {

[[nodiscard]] constexpr bool isValidFocusNavigationDirection(UIFocusNavigationDirection direction) noexcept
{
    return direction == UIFocusNavigationDirection::Left || direction == UIFocusNavigationDirection::Right ||
           direction == UIFocusNavigationDirection::Up || direction == UIFocusNavigationDirection::Down;
}

struct UIFocusNavigationScore final {
    bool inDirectionalBeam = false;
    double primaryDistance = 0.0;
    double secondaryDistance = 0.0;
    double centerDistanceSquared = 0.0;
    u32 paintOrdinal = 0;
};

[[nodiscard]] inline double intervalDistance(double firstStart, double firstEnd, double secondStart,
                                             double secondEnd) noexcept
{
    if (firstEnd < secondStart)
    {
        return secondStart - firstEnd;
    }
    if (secondEnd < firstStart)
    {
        return firstStart - secondEnd;
    }
    return 0.0;
}

[[nodiscard]] inline bool focusNavigationScore(UILogicalRect origin, UILogicalRect candidate,
                                               UIFocusNavigationDirection direction, u32 paintOrdinal,
                                               UIFocusNavigationScore& output) noexcept
{
    const double originCenterX = static_cast<double>(origin.x) + static_cast<double>(origin.width) * 0.5;
    const double originCenterY = static_cast<double>(origin.y) + static_cast<double>(origin.height) * 0.5;
    const double candidateCenterX = static_cast<double>(candidate.x) + static_cast<double>(candidate.width) * 0.5;
    const double candidateCenterY = static_cast<double>(candidate.y) + static_cast<double>(candidate.height) * 0.5;
    const double deltaX = candidateCenterX - originCenterX;
    const double deltaY = candidateCenterY - originCenterY;

    double primaryDistance = 0.0;
    double secondaryDistance = 0.0;
    switch (direction)
    {
    case UIFocusNavigationDirection::Left:
        if (deltaX >= 0.0)
        {
            return false;
        }
        primaryDistance = std::max(0.0, static_cast<double>(origin.x) - static_cast<double>(candidate.right()));
        secondaryDistance = intervalDistance(origin.y, origin.bottom(), candidate.y, candidate.bottom());
        break;
    case UIFocusNavigationDirection::Right:
        if (deltaX <= 0.0)
        {
            return false;
        }
        primaryDistance = std::max(0.0, static_cast<double>(candidate.x) - static_cast<double>(origin.right()));
        secondaryDistance = intervalDistance(origin.y, origin.bottom(), candidate.y, candidate.bottom());
        break;
    case UIFocusNavigationDirection::Up:
        if (deltaY >= 0.0)
        {
            return false;
        }
        primaryDistance = std::max(0.0, static_cast<double>(origin.y) - static_cast<double>(candidate.bottom()));
        secondaryDistance = intervalDistance(origin.x, origin.right(), candidate.x, candidate.right());
        break;
    case UIFocusNavigationDirection::Down:
        if (deltaY <= 0.0)
        {
            return false;
        }
        primaryDistance = std::max(0.0, static_cast<double>(candidate.y) - static_cast<double>(origin.bottom()));
        secondaryDistance = intervalDistance(origin.x, origin.right(), candidate.x, candidate.right());
        break;
    }

    output = UIFocusNavigationScore{
        .inDirectionalBeam = secondaryDistance == 0.0,
        .primaryDistance = primaryDistance,
        .secondaryDistance = secondaryDistance,
        .centerDistanceSquared = deltaX * deltaX + deltaY * deltaY,
        .paintOrdinal = paintOrdinal,
    };
    return true;
}

[[nodiscard]] constexpr bool isBetterFocusNavigationScore(const UIFocusNavigationScore& candidate,
                                                          const UIFocusNavigationScore& current) noexcept
{
    if (candidate.inDirectionalBeam != current.inDirectionalBeam)
    {
        return candidate.inDirectionalBeam;
    }
    if (candidate.primaryDistance != current.primaryDistance)
    {
        return candidate.primaryDistance < current.primaryDistance;
    }
    if (candidate.secondaryDistance != current.secondaryDistance)
    {
        return candidate.secondaryDistance < current.secondaryDistance;
    }
    if (candidate.centerDistanceSquared != current.centerDistanceSquared)
    {
        return candidate.centerDistanceSquared < current.centerDistanceSquared;
    }
    return candidate.paintOrdinal < current.paintOrdinal;
}

template <typename CandidatePredicate>
[[nodiscard]] u32 findFocusNavigationCandidate(std::span<const UICommittedHitEntry> entries, u32 currentEntryIndex,
                                               UIFocusNavigationDirection direction,
                                               CandidatePredicate&& isCandidate) noexcept
{
    if (currentEntryIndex >= entries.size() || !isCandidate(currentEntryIndex))
    {
        return InvalidUIHitEntryIndex;
    }
    u32 bestEntryIndex = InvalidUIHitEntryIndex;
    UIFocusNavigationScore bestScore{};

    for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
    {
        if (entryIndex == currentEntryIndex || !isCandidate(entryIndex))
        {
            continue;
        }
        UIFocusNavigationScore candidateScore{};
        if (!focusNavigationScore(entries[currentEntryIndex].worldRect, entries[entryIndex].worldRect, direction,
                                  entries[entryIndex].paintOrdinal, candidateScore))
        {
            continue;
        }
        if (bestEntryIndex == InvalidUIHitEntryIndex || isBetterFocusNavigationScore(candidateScore, bestScore))
        {
            bestEntryIndex = entryIndex;
            bestScore = candidateScore;
        }
    }
    return bestEntryIndex;
}

} // namespace Tina::UI::Detail
