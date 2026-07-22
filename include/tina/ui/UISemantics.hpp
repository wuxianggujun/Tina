#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIWidgetKind.hpp>

#include <span>
#include <string_view>

namespace Tina::UI {

// First-wave accessible roles (docs/ui.md). Decorative nodes are omitted from
// the committed semantics tree.
enum class UISemanticsRole : u8 {
    Group = 0,
    Label,
    Button,
    Checkbox,
    Slider,
    TextEdit,
    Dialog,
    List,
    ListItem,
    ProgressBar,
    RadioButton,
};

// Owner-thread snapshot entry. Text fields point into the committed snapshot's
// private, double-buffered text storage and are invalidated with the view.
struct UISemanticsEntry final {
    UINodeId node{};
    UINodeId parent{};
    UISemanticsRole role = UISemanticsRole::Group;
    UIWidgetKind kind = UIWidgetKind::Panel;
    UILogicalRect worldRect{};
    std::string_view name{};
    std::string_view description{};
    std::string_view valueText{};
    float value = 0.0F;
    float minValue = 0.0F;
    float maxValue = 0.0F;
    bool hasRange = false;
    bool checked = false;
    bool enabled = true;
    bool focused = false;
};

// Owner-thread borrowed semantics snapshot. Invalidated by the next successful
// commitLayout semantics publication, or by UIContext destruction. It is not a
// cross-thread snapshot.
class UICommittedSemanticsView final {
  public:
    constexpr UICommittedSemanticsView() noexcept = default;

    constexpr UICommittedSemanticsView(std::span<const UISemanticsEntry> entries, UILogicalSize viewportSize,
                                       u64 structureRevision, u64 layoutRevision, u64 semanticsRevision) noexcept
        : m_entries(entries),
          m_viewportSize(viewportSize),
          m_structureRevision(structureRevision),
          m_layoutRevision(layoutRevision),
          m_semanticsRevision(semanticsRevision)
    {
    }

    [[nodiscard]] constexpr std::span<const UISemanticsEntry> entries() const noexcept
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

    [[nodiscard]] constexpr u64 semanticsRevision() const noexcept
    {
        return m_semanticsRevision;
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
    std::span<const UISemanticsEntry> m_entries{};
    UILogicalSize m_viewportSize{};
    u64 m_structureRevision = 0;
    u64 m_layoutRevision = 0;
    u64 m_semanticsRevision = 0;
};

[[nodiscard]] constexpr UISemanticsRole semanticsRoleForWidgetKind(UIWidgetKind kind) noexcept
{
    switch (kind)
    {
    case UIWidgetKind::Root:
    case UIWidgetKind::Panel:
        return UISemanticsRole::Group;
    case UIWidgetKind::Label:
        return UISemanticsRole::Label;
    case UIWidgetKind::Button:
        return UISemanticsRole::Button;
    case UIWidgetKind::Checkbox:
        return UISemanticsRole::Checkbox;
    case UIWidgetKind::Slider:
        return UISemanticsRole::Slider;
    case UIWidgetKind::TextEdit:
        return UISemanticsRole::TextEdit;
    case UIWidgetKind::ProgressBar:
        return UISemanticsRole::ProgressBar;
    case UIWidgetKind::RadioButton:
        return UISemanticsRole::RadioButton;
    }
    return UISemanticsRole::Group;
}

// Interactive roles enter the semantics tree; Root/Panel groups are omitted
// unless they later gain explicit accessible names (not in this slice).
[[nodiscard]] constexpr bool isSemanticsPublishedKind(UIWidgetKind kind) noexcept
{
    return kind == UIWidgetKind::Label || kind == UIWidgetKind::Button || kind == UIWidgetKind::Checkbox ||
           kind == UIWidgetKind::Slider || kind == UIWidgetKind::TextEdit ||
           kind == UIWidgetKind::ProgressBar || kind == UIWidgetKind::RadioButton;
}

} // namespace Tina::UI
