#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIWidgetKind.hpp>

#include <span>

namespace Tina::UI {

struct UICommittedNodeEntry final {
    UINodeId node{};
    UINodeId parent{};
    u32 depth = 0;
    u32 preorder = 0;
    u32 paintOrdinal = 0;
    UIWidgetKind kind = UIWidgetKind::Panel;
};

// Owner-thread borrowed view. It is invalidated by the next commitStructure()
// call or by destruction of its UIContext and is not a cross-thread snapshot.
class UICommittedStructureView final {
public:
    constexpr UICommittedStructureView() noexcept = default;

    constexpr UICommittedStructureView(
        std::span<const UICommittedNodeEntry> entries,
        u64 revision) noexcept
        : m_entries(entries), m_revision(revision)
    {
    }

    [[nodiscard]] constexpr std::span<const UICommittedNodeEntry> entries() const noexcept
    {
        return m_entries;
    }

    [[nodiscard]] constexpr u64 revision() const noexcept
    {
        return m_revision;
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
    std::span<const UICommittedNodeEntry> m_entries{};
    u64 m_revision = 0;
};

} // namespace Tina::UI
