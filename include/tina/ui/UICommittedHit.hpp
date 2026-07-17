#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <limits>
#include <span>

namespace Tina::UI {

// Snapshot-local ancestry indices. They are never persistent node identities.
inline constexpr u32 InvalidUIHitEntryIndex = (std::numeric_limits<u32>::max)();

struct UICommittedHitEntry final {
    UINodeId node{};
    u32 parentEntryIndex = InvalidUIHitEntryIndex;
    u32 rootEntryIndex = InvalidUIHitEntryIndex;
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    u32 paintOrdinal = 0;
    UIPointerHitPolicy policy = UIPointerHitPolicy::Ignore;
};

// Owner-thread borrowed hit/route-ancestry view. It is invalidated by the next
// successful hit publication through commitLayout(), or by destruction of its
// UIContext. Entries are strictly ordered by ascending, unique paintOrdinal;
// reverse iteration therefore follows front-to-back hit priority. A successful
// no-op commit does not invalidate this view.
class UICommittedHitView final {
public:
    constexpr UICommittedHitView() noexcept = default;

    constexpr UICommittedHitView(
        std::span<const UICommittedHitEntry> entries,
        u64 structureRevision,
        u64 layoutRevision,
        u64 paintOrderRevision,
        u64 hitRevision) noexcept
        : m_entries(entries),
          m_structureRevision(structureRevision),
          m_layoutRevision(layoutRevision),
          m_paintOrderRevision(paintOrderRevision),
          m_hitRevision(hitRevision)
    {
    }

    [[nodiscard]] constexpr std::span<const UICommittedHitEntry> entries() const noexcept
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

    [[nodiscard]] constexpr u64 hitRevision() const noexcept
    {
        return m_hitRevision;
    }

    [[nodiscard]] constexpr u64 paintOrderRevision() const noexcept
    {
        return m_paintOrderRevision;
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
    std::span<const UICommittedHitEntry> m_entries{};
    u64 m_structureRevision = 0;
    u64 m_layoutRevision = 0;
    u64 m_paintOrderRevision = 0;
    u64 m_hitRevision = 0;
};

} // namespace Tina::UI
