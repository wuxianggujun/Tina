#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <span>

namespace Tina::UI {

struct UICommittedLayoutEntry final {
    UINodeId node{};
    UILogicalRect localRect{};
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    UICommittedContentPlacement contentPlacement{};
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    u32 layoutOrdinal = 0;
    u32 paintOrdinal = 0;
};

// Owner-thread borrowed layout view. It is invalidated by the next successful
// layout publication through commitLayout(), or by destruction of its
// UIContext, and is not a cross-thread snapshot.
class UICommittedLayoutView final {
public:
    constexpr UICommittedLayoutView() noexcept = default;

    constexpr UICommittedLayoutView(
        std::span<const UICommittedLayoutEntry> entries,
        u64 structureRevision,
        u64 layoutRevision) noexcept
        : m_entries(entries),
          m_structureRevision(structureRevision),
          m_layoutRevision(layoutRevision)
    {
    }

    [[nodiscard]] constexpr std::span<const UICommittedLayoutEntry> entries() const noexcept
    {
        return m_entries;
    }

    [[nodiscard]] constexpr u64 structureRevision() const noexcept
    {
        return m_structureRevision;
    }

    [[nodiscard]] constexpr u64 layoutRevision() const noexcept
    {
        return m_layoutRevision;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return m_entries.empty();
    }

    [[nodiscard]] constexpr usize size() const noexcept
    {
        return m_entries.size();
    }

    [[nodiscard]] constexpr auto begin() const noexcept
    {
        return m_entries.begin();
    }

    [[nodiscard]] constexpr auto end() const noexcept
    {
        return m_entries.end();
    }

private:
    std::span<const UICommittedLayoutEntry> m_entries{};
    u64 m_structureRevision = 0;
    u64 m_layoutRevision = 0;
};

} // namespace Tina::UI
