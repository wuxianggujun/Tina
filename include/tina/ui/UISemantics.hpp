#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UITreeView.hpp>

#include <optional>
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
    ScrollView,
    ComboBox,
    Tree,
    TreeItem,
    Image,
    TabList,
    Tab,
    TabPanel,
};

enum class UISemanticsMode : u8 {
    // The Element itself is omitted while eligible descendants remain visible.
    Automatic = 0,
    Publish,
    // Publish this Element and fold eligible descendant names into it. Merged
    // descendants do not appear as separate committed semantics entries.
    MergeDescendants,
    // Exclude this Element and its complete subtree.
    Exclude,
};

enum class UISemanticsAction : u8 {
    None = 0,
    Focus = 1U << 0U,
    Activate = 1U << 1U,
    Toggle = 1U << 2U,
    SetRangeValue = 1U << 3U,
    SetTextValue = 1U << 4U,
};

[[nodiscard]] constexpr UISemanticsAction operator|(UISemanticsAction left, UISemanticsAction right) noexcept
{
    return static_cast<UISemanticsAction>(static_cast<u8>(left) | static_cast<u8>(right));
}

[[nodiscard]] constexpr UISemanticsAction operator&(UISemanticsAction left, UISemanticsAction right) noexcept
{
    return static_cast<UISemanticsAction>(static_cast<u8>(left) & static_cast<u8>(right));
}

constexpr UISemanticsAction& operator|=(UISemanticsAction& left, UISemanticsAction right) noexcept
{
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool hasSemanticsAction(UISemanticsAction set, UISemanticsAction action) noexcept
{
    return (set & action) == action;
}

struct UISemanticsDescriptor final {
    UISemanticsMode mode = UISemanticsMode::Automatic;
    UISemanticsRole role = UISemanticsRole::Group;
    std::optional<std::string_view> name{};
    std::optional<std::string_view> description{};
    UISemanticsAction actions = UISemanticsAction::None;
    // Uses intrinsic Element text when an explicit name is absent. A merged
    // node additionally appends eligible descendant names in tree order.
    bool useContentAsName = false;
    bool readOnly = false;
};

// Owner-thread snapshot entry. Text fields point into the committed snapshot's
// private, double-buffered text storage and are invalidated with the view.
struct UISemanticsEntry final {
    UINodeId node{};
    UINodeId parent{};
    UISemanticsRole role = UISemanticsRole::Group;
    UISemanticsAction actions = UISemanticsAction::None;
    UILogicalRect worldRect{};
    std::string_view name{};
    std::string_view description{};
    std::string_view valueText{};
    float value = 0.0F;
    float minValue = 0.0F;
    float maxValue = 0.0F;
    bool hasRange = false;
    bool checked = false;
    bool selected = false;
    bool enabled = true;
    bool focused = false;
    bool readOnly = false;
    UIListViewItemKey virtualItemKey = InvalidUIListViewItemKey;
    u64 virtualItemIndex = 0;
    u32 level = 0;
    bool expandable = false;
    bool expanded = false;
};

// Owner-thread borrowed semantics snapshot. Invalidated by the next successful
// commitLayout semantics publication, or by UIContext destruction. It is not a
// cross-thread snapshot.
class UICommittedSemanticsView final {
  public:
    constexpr UICommittedSemanticsView() noexcept = default;

    constexpr UICommittedSemanticsView(std::span<const UISemanticsEntry> entries, UILogicalSize viewportSize,
                                       u64 structureRevision, u64 layoutRevision, u64 semanticsRevision) noexcept
        : m_entries(entries), m_viewportSize(viewportSize), m_structureRevision(structureRevision),
          m_layoutRevision(layoutRevision), m_semanticsRevision(semanticsRevision)
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

} // namespace Tina::UI
