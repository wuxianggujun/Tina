#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIBehavior.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIStyle.hpp>

#include <compare>
#include <span>

namespace Tina::UI {

// Stable public diagnostic identity. This deliberately does not expose the
// private retained-dispatch enum used by UIContext.
enum class UILayoutDebugElementType : u8 {
    Unknown,
    Root,
    Panel,
    Label,
    Button,
    Checkbox,
    Slider,
    TextEdit,
    ProgressBar,
    RadioButton,
    Modal,
    ScrollView,
    Dropdown,
    Popup,
    DropdownItem,
    ListView,
    ListViewItem,
    TreeView,
    TreeViewItem,
    VirtualGridView,
    VirtualGridViewItem,
    DataGrid,
    DataGridRow,
    DataGridCell,
    DataGridColumnHeader,
    Tooltip,
    SplitView,
    Splitter,
    TabView,
    Tab,
    Menu,
    MenuItem,
};

struct UILayoutDebugBasis final {
    bool parentContentWidthDefinite = false;
    bool parentContentHeightDefinite = false;
    float parentContentWidth = 0.0F;
    float parentContentHeight = 0.0F;
    bool contentWidthDefinite = false;
    bool contentHeightDefinite = false;
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;

    auto operator<=>(const UILayoutDebugBasis&) const = default;
};

struct UILayoutDebugEntry final {
    UINodeId node{};
    UINodeId parent{};
    UILayoutDebugElementType elementType = UILayoutDebugElementType::Unknown;
    u32 depth = 0;
    u32 preorder = 0;
    u32 layoutOrdinal = 0;
    u32 paintOrdinal = 0;

    UILayoutStyle authoredStyle{};
    UILayoutStyle resolvedStyle{};
    UILayoutDebugBasis basis{};
    UILogicalSize measuredSize{};
    UILogicalSize minContentSize{};
    UILogicalSize maxContentSize{};
    UILogicalRect localRect{};
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    UICommittedContentPlacement contentPlacement{};
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    UIPointerHitPolicy pointerHitPolicy = UIPointerHitPolicy::Ignore;
    UIElementBehavior behaviors = UIElementBehavior::None;
    UIStyleRoleId styleRole = UIStyleRoleId::None;
    bool enabled = true;

    auto operator<=>(const UILayoutDebugEntry&) const = default;
};

// Owner-thread borrowed view. Entries remain valid until the next successful
// layout publication or UIContext destruction. A failed layout keeps the prior
// published view intact. The view is empty when the configured debug capacity is
// zero; the capability is compiled and callable in every build configuration.
class UILayoutDebugSnapshotView final {
  public:
    constexpr UILayoutDebugSnapshotView() noexcept = default;

    constexpr UILayoutDebugSnapshotView(std::span<const UILayoutDebugEntry> entries,
                                        u64 structureRevision, u64 layoutRevision,
                                        UILogicalSize viewportSize) noexcept
        : m_entries(entries),
          m_structureRevision(structureRevision),
          m_layoutRevision(layoutRevision),
          m_viewportSize(viewportSize)
    {
    }

    [[nodiscard]] constexpr std::span<const UILayoutDebugEntry> entries() const noexcept
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
    [[nodiscard]] constexpr UILogicalSize viewportSize() const noexcept
    {
        return m_viewportSize;
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
    std::span<const UILayoutDebugEntry> m_entries{};
    u64 m_structureRevision = 0;
    u64 m_layoutRevision = 0;
    UILogicalSize m_viewportSize{};
};

// Overlay interaction state is independent from layout publication. Updating
// it never runs layout and does not enter the retained mutation transaction.
struct UILayoutDebugOptions final {
    bool enabled = false;
    bool showAllVisibleBounds = false;
    UINodeId selectedNode{};
    UINodeId excludedSubtreeRoot{};
    // Frame-local transform for an actively dragged diagnostic subtree. This
    // never enters retained layout; the Render bridge applies it to the
    // committed paint snapshot for the current frame only.
    UINodeId transientTransformRoot{};
    UILogicalPoint transientTransformOffset{};

    auto operator<=>(const UILayoutDebugOptions&) const = default;
};

class UIContext;

class UILayoutDebugger final {
  public:
    [[nodiscard]] UILayoutDebugSnapshotView committedSnapshot() const noexcept;
    [[nodiscard]] UILayoutDebugOptions options() const noexcept;
    [[nodiscard]] Core::Status setOptions(UILayoutDebugOptions options);

  private:
    explicit UILayoutDebugger(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;

    friend class UIContext;
};

} // namespace Tina::UI
