#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <span>

namespace Tina::UI {

struct UICommittedPaintEntry final {
    UINodeId node{};
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    u32 paintOrdinal = 0;
    UIPremultipliedRgba8Color solidFill{};
};

// Owner-thread borrowed paint/composite snapshot. It is invalidated by the
// next successful paint publication through commitLayout(), or by destruction
// of its UIContext. A successful no-op commit does not invalidate this view.
class UICommittedPaintView final {
public:
    constexpr UICommittedPaintView() noexcept = default;

    constexpr UICommittedPaintView(
        std::span<const UICommittedPaintEntry> entries,
        UILogicalSize viewportSize,
        u64 structureRevision,
        u64 layoutRevision,
        u64 paintOrderRevision,
        u64 paintRevision) noexcept
        : m_entries(entries),
          m_viewportSize(viewportSize),
          m_structureRevision(structureRevision),
          m_layoutRevision(layoutRevision),
          m_paintOrderRevision(paintOrderRevision),
          m_paintRevision(paintRevision)
    {
    }

    [[nodiscard]] constexpr std::span<const UICommittedPaintEntry> entries() const noexcept
    {
        return m_entries;
    }

    [[nodiscard]] constexpr UILogicalSize viewportSize() const noexcept
    {
        return m_viewportSize;
    }

    [[nodiscard]] constexpr u64 structureRevision() const noexcept
    {
        return m_structureRevision;
    }

    [[nodiscard]] constexpr u64 layoutRevision() const noexcept
    {
        return m_layoutRevision;
    }

    [[nodiscard]] constexpr u64 paintOrderRevision() const noexcept
    {
        return m_paintOrderRevision;
    }

    [[nodiscard]] constexpr u64 paintRevision() const noexcept
    {
        return m_paintRevision;
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
    std::span<const UICommittedPaintEntry> m_entries{};
    UILogicalSize m_viewportSize{};
    u64 m_structureRevision = 0;
    u64 m_layoutRevision = 0;
    u64 m_paintOrderRevision = 0;
    u64 m_paintRevision = 0;
};

} // namespace Tina::UI
