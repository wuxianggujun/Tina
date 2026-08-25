#include "EditorWorkspaceState.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] std::string_view layoutDebugElementTypeName(
    UI::UILayoutDebugElementType type) noexcept
{
    using Type = UI::UILayoutDebugElementType;
    switch (type) {
    case Type::Root: return "Root";
    case Type::Panel: return "Panel";
    case Type::Label: return "Label";
    case Type::Button: return "Button";
    case Type::Checkbox: return "Checkbox";
    case Type::Slider: return "Slider";
    case Type::TextEdit: return "TextEdit";
    case Type::ProgressBar: return "ProgressBar";
    case Type::RadioButton: return "RadioButton";
    case Type::Modal: return "Modal";
    case Type::ScrollView: return "ScrollView";
    case Type::Dropdown: return "Dropdown";
    case Type::Popup: return "Popup";
    case Type::DropdownItem: return "DropdownItem";
    case Type::ListView: return "ListView";
    case Type::ListViewItem: return "ListViewItem";
    case Type::TreeView: return "TreeView";
    case Type::TreeViewItem: return "TreeViewItem";
    case Type::VirtualGridView: return "VirtualGridView";
    case Type::VirtualGridViewItem: return "VirtualGridViewItem";
    case Type::DataGrid: return "DataGrid";
    case Type::DataGridRow: return "DataGridRow";
    case Type::DataGridCell: return "DataGridCell";
    case Type::DataGridColumnHeader: return "DataGridColumnHeader";
    case Type::Tooltip: return "Tooltip";
    case Type::SplitView: return "SplitView";
    case Type::Splitter: return "Splitter";
    case Type::TabView: return "TabView";
    case Type::Tab: return "Tab";
    case Type::Menu: return "Menu";
    case Type::MenuItem: return "MenuItem";
    case Type::Unknown: break;
    }
    return "Unknown";
}

void appendText(std::string& output, std::string_view text)
{
    output.append(text.data(), text.size());
}

void appendFloat(std::string& output, float value)
{
    char buffer[32]{};
    const auto result = std::to_chars(
        std::begin(buffer), std::end(buffer), value,
        std::chars_format::fixed, 1);
    if (result.ec == std::errc{}) {
        output.append(buffer, result.ptr);
    } else {
        appendText(output, "?");
    }
}

void appendRect(std::string& output, const UI::UILogicalRect& rect)
{
    output.push_back('(');
    appendFloat(output, rect.x);
    output.append(", ");
    appendFloat(output, rect.y);
    output.append(") ");
    appendFloat(output, rect.width);
    output.push_back('x');
    appendFloat(output, rect.height);
}

void appendSize(std::string& output, const UI::UILogicalSize& size)
{
    appendFloat(output, size.width);
    output.push_back('x');
    appendFloat(output, size.height);
}

void appendLength(std::string& output, const UI::UILayoutLength& length)
{
    switch (length.unit) {
    case UI::UILayoutLengthUnit::Px:
        appendFloat(output, length.value);
        appendText(output, "px");
        break;
    case UI::UILayoutLengthUnit::Percent:
        appendFloat(output, length.value);
        appendText(output, "%");
        break;
    case UI::UILayoutLengthUnit::MinContent:
        appendText(output, "min-content");
        break;
    case UI::UILayoutLengthUnit::MaxContent:
        appendText(output, "max-content");
        break;
    case UI::UILayoutLengthUnit::Auto:
        appendText(output, "auto");
        break;
    }
}

[[nodiscard]] std::string layoutDebugNodeLabel(
    const UI::UILayoutDebugEntry& entry)
{
    std::string result;
    result.reserve(72U);
    appendText(result, layoutDebugElementTypeName(entry.elementType));
    appendText(result, "  #");
    result += std::to_string(entry.preorder);
    appendText(result, "  ");
    appendRect(result, entry.worldRect);
    return result;
}

[[nodiscard]] constexpr std::string_view axisAlignmentName(
    UI::UIAxisAlignment alignment) noexcept
{
    switch (alignment) {
    case UI::UIAxisAlignment::Start: return "start";
    case UI::UIAxisAlignment::Center: return "center";
    case UI::UIAxisAlignment::End: return "end";
    case UI::UIAxisAlignment::Stretch: return "stretch";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view alignSelfName(
    UI::UIAlignSelf alignment) noexcept
{
    switch (alignment) {
    case UI::UIAlignSelf::Auto: return "auto";
    case UI::UIAlignSelf::Start: return "start";
    case UI::UIAlignSelf::Center: return "center";
    case UI::UIAlignSelf::End: return "end";
    case UI::UIAlignSelf::Stretch: return "stretch";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view visibilityName(
    UI::UIVisibility visibility) noexcept
{
    switch (visibility) {
    case UI::UIVisibility::Visible: return "visible";
    case UI::UIVisibility::Hidden: return "hidden";
    case UI::UIVisibility::Collapsed: return "collapsed";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view justifyContentName(
    UI::UIJustifyContent alignment) noexcept
{
    switch (alignment) {
    case UI::UIJustifyContent::Start: return "start";
    case UI::UIJustifyContent::Center: return "center";
    case UI::UIJustifyContent::End: return "end";
    case UI::UIJustifyContent::SpaceBetween: return "space-between";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view alignContentName(
    UI::UIAlignContent alignment) noexcept
{
    switch (alignment) {
    case UI::UIAlignContent::Start: return "start";
    case UI::UIAlignContent::Center: return "center";
    case UI::UIAlignContent::End: return "end";
    case UI::UIAlignContent::SpaceBetween: return "space-between";
    case UI::UIAlignContent::Stretch: return "stretch";
    }
    return "?";
}

void appendEdgeSpacing(std::string& output, const UI::UIEdgeSpacing& spacing)
{
    appendFloat(output, spacing.left);
    output.push_back('/');
    appendFloat(output, spacing.top);
    output.push_back('/');
    appendFloat(output, spacing.right);
    output.push_back('/');
    appendFloat(output, spacing.bottom);
}

void appendGridTrack(std::string& output, const UI::UIGridTrack& track)
{
    switch (track.unit) {
    case UI::UIGridTrackUnit::Px:
        appendFloat(output, track.value);
        appendText(output, "px");
        break;
    case UI::UIGridTrackUnit::Auto:
        appendText(output, "auto");
        break;
    case UI::UIGridTrackUnit::Fraction:
        appendFloat(output, track.value);
        appendText(output, "fr");
        break;
    }
}

void appendGridTracks(std::string& output, const UI::UIGridTrackList& tracks)
{
    if (tracks.count == 0U) {
        appendText(output, "implicit-auto");
        return;
    }
    if (tracks.count == UI::UIGridAutoIndex) {
        appendText(output, "invalid");
        return;
    }
    for (u32 index = 0U; index < tracks.count; ++index) {
        if (index != 0U) {
            output.push_back(' ');
        }
        appendGridTrack(output, tracks.tracks[index]);
    }
}

[[nodiscard]] std::string layoutDebugStyleSizing(
    const UI::UILayoutStyle& style)
{
    std::string text{"width="};
    appendLength(text, style.size.width);
    appendText(text, ", height=");
    appendLength(text, style.size.height);
    appendText(text, ", min=(");
    appendLength(text, style.minMax.minWidth);
    appendText(text, ", ");
    appendLength(text, style.minMax.minHeight);
    appendText(text, "), max=(");
    appendLength(text, style.minMax.maxWidth);
    appendText(text, ", ");
    appendLength(text, style.minMax.maxHeight);
    text.push_back(')');
    appendText(text, ", aspect=");
    if (style.aspectRatio.has_value()) {
        appendFloat(text, *style.aspectRatio);
    } else {
        appendText(text, "auto");
    }
    appendText(text, ", responsive=");
    text += std::to_string(style.responsiveRules.count);
    return text;
}

[[nodiscard]] std::string layoutDebugStyleBox(const UI::UILayoutStyle& style)
{
    std::string text{"container="};
    appendText(text, style.containerLayout == UI::UIContainerLayout::Grid
                         ? "grid" : "flex");
    appendText(text, ", margin=");
    appendEdgeSpacing(text, style.margin);
    appendText(text, ", padding=");
    appendEdgeSpacing(text, style.padding);
    appendText(text, ", clip=");
    appendText(text, style.clipDescendants ? "true" : "false");
    return text;
}

[[nodiscard]] std::string layoutDebugFlexContainer(
    const UI::UILayoutStyle& style)
{
    std::string text{"direction="};
    appendText(text, style.flexContainer.direction == UI::UIFlexDirection::Row
                        ? "row" : "column");
    appendText(text, ", wrap=");
    appendText(text, style.flexContainer.wrap == UI::UIFlexWrap::Wrap
                        ? "wrap" : "nowrap");
    appendText(text, ", justify=");
    appendText(text, justifyContentName(style.flexContainer.justifyContent));
    appendText(text, ", align-content=");
    appendText(text, alignContentName(style.flexContainer.alignContent));
    appendText(text, ", align-items=");
    appendText(text, axisAlignmentName(style.flexContainer.alignItems));
    appendText(text, ", gap(col/row)=");
    appendFloat(text, style.flexContainer.gap.column);
    text.push_back('/');
    appendFloat(text, style.flexContainer.gap.row);
    return text;
}

[[nodiscard]] std::string layoutDebugFlexItem(const UI::UILayoutStyle& style)
{
    std::string text{"grow="};
    appendFloat(text, style.flexItem.grow);
    appendText(text, ", shrink=");
    appendFloat(text, style.flexItem.shrink);
    appendText(text, ", basis=");
    appendLength(text, style.flexItem.basis);
    appendText(text, ", align-self=");
    appendText(text, alignSelfName(style.flexItem.alignSelf));
    return text;
}

[[nodiscard]] std::string layoutDebugGridContainer(
    const UI::UILayoutStyle& style)
{
    std::string text{"columns=["};
    appendGridTracks(text, style.gridContainer.columns);
    appendText(text, "], rows=[");
    appendGridTracks(text, style.gridContainer.rows);
    appendText(text, "], gap(col/row)=");
    appendFloat(text, style.gridContainer.gap.column);
    text.push_back('/');
    appendFloat(text, style.gridContainer.gap.row);
    appendText(text, ", justify-items=");
    appendText(text, axisAlignmentName(style.gridContainer.justifyItems));
    appendText(text, ", align-items=");
    appendText(text, axisAlignmentName(style.gridContainer.alignItems));
    return text;
}

void appendGridIndex(std::string& output, u8 index)
{
    if (index == UI::UIGridAutoIndex) {
        appendText(output, "auto");
    } else {
        output += std::to_string(index);
    }
}

[[nodiscard]] std::string layoutDebugGridItem(const UI::UILayoutStyle& style)
{
    std::string text{"column="};
    appendGridIndex(text, style.gridItem.column);
    appendText(text, ", row=");
    appendGridIndex(text, style.gridItem.row);
    appendText(text, ", span(col/row)=");
    text += std::to_string(style.gridItem.columnSpan);
    text.push_back('/');
    text += std::to_string(style.gridItem.rowSpan);
    appendText(text, ", justify-self=");
    appendText(text, alignSelfName(style.gridItem.justifySelf));
    appendText(text, ", align-self=");
    appendText(text, alignSelfName(style.gridItem.alignSelf));
    return text;
}

[[nodiscard]] std::string layoutDebugOverlay(const UI::UILayoutStyle& style)
{
    std::string text{"placement="};
    appendText(text, style.placement == UI::UILayoutPlacement::Overlay
                         ? "overlay" : "flow");
    appendText(text, ", horizontal=");
    appendText(text, axisAlignmentName(style.overlay.horizontal));
    appendText(text, ", vertical=");
    appendText(text, axisAlignmentName(style.overlay.vertical));
    appendText(text, ", offset=(");
    appendLength(text, style.overlay.offset.x);
    appendText(text, ", ");
    appendLength(text, style.overlay.offset.y);
    text.push_back(')');
    return text;
}

[[nodiscard]] std::string layoutDebugNodeState(
    const UI::UILayoutDebugEntry& entry)
{
    std::string text{"visibility(authored/resolved/effective)="};
    appendText(text, visibilityName(entry.authoredStyle.visibility));
    text.push_back('/');
    appendText(text, visibilityName(entry.resolvedStyle.visibility));
    text.push_back('/');
    appendText(text, visibilityName(entry.effectiveVisibility));
    appendText(text, ", enabled=");
    appendText(text, entry.enabled ? "true" : "false");
    appendText(text, ", hit=");
    appendText(text, entry.pointerHitPolicy == UI::UIPointerHitPolicy::Targetable
                        ? "targetable" : "ignore");
    appendText(text, ", behaviors=0x");
    char behaviorBuffer[16]{};
    const auto behaviorResult = std::to_chars(
        std::begin(behaviorBuffer), std::end(behaviorBuffer),
        static_cast<u32>(entry.behaviors), 16);
    if (behaviorResult.ec == std::errc{}) {
        text.append(behaviorBuffer, behaviorResult.ptr);
    }
    appendText(text, ", style-role=");
    text += std::to_string(static_cast<u32>(entry.styleRole));
    appendText(text, ", layout/paint=");
    text += std::to_string(entry.layoutOrdinal);
    text.push_back('/');
    text += std::to_string(entry.paintOrdinal);
    return text;
}

[[nodiscard]] constexpr UI::UITreeViewItemKey layoutDebugNodeKey(
    UI::UINodeId node) noexcept
{
    return (static_cast<u64>(node.generation()) << 32U) |
           (static_cast<u64>(node.index()) + 1U);
}

} // namespace

auto EditorWorkspaceState::layoutDebugTreeDataSource() noexcept
    -> UI::UITreeViewDataSource
{
    return {
        .state = this,
        .itemCount = &EditorWorkspaceState::layoutDebugTreeItemCount,
        .resolveItem = &EditorWorkspaceState::resolveLayoutDebugTreeItem,
    };
}

u64 EditorWorkspaceState::layoutDebugTreeItemCount(const void* state) noexcept
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self == nullptr ? 0U : static_cast<u64>(self->layoutDebugProjectionCount_);
}

bool EditorWorkspaceState::resolveLayoutDebugTreeItem(
    const void* state, u64 logicalIndex,
    UI::UITreeViewItemDescriptor& output) noexcept
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalIndex >= self->layoutDebugProjectionCount_) {
        return false;
    }
    const auto& row = self->layoutDebugProjection_[logicalIndex];
    output = {
        .key = row.key,
        .label = std::string_view(row.label.data(), row.labelLength),
        .level = row.depth,
        .enabled = true,
        .expandable = false,
        .expanded = false,
    };
    return true;
}

void EditorWorkspaceState::handleLayoutDebugPickPointerDown(
    UI::UIRoutedPointerEvent& event) noexcept
{
    if (!layoutDebugPickArmed_ || bottomPanel_ != BottomPanelKind::Layout) {
        return;
    }
    const UI::UINodeId target = event.targetNode();
    if (target == layoutDebugPanel_) {
        return;
    }
    for (Tina::Core::usize index = 0U;
         index < layoutDebugPanelSubtreeNodeCount_; ++index) {
        if (layoutDebugPanelSubtreeNodes_[index] == target) {
            return;
        }
    }
    if (!layoutDebugSnapshotInitialized_) {
        return;
    }
    pendingLayoutDebugPickPoint_ = event.input().position;
    event.consumeInputTransition();
    event.preventDefaultAction();
    event.stopPropagation();
}

auto EditorWorkspaceState::refreshLayoutDebuggerUi(
    Tina::UIUpdateContext& context,
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const auto applyLayoutDebugOptions = [&]() -> Tina::Core::Status {
        UI::UILayoutDebugOptions options{};
        options.enabled = bottomPanel_ == BottomPanelKind::Layout;
        options.showAllVisibleBounds = layoutDebugShowAllVisibleBounds_;
        options.selectedNode = layoutDebugSelectedNode_;
        options.excludedSubtreeRoot = layoutDebugPanel_;
        return context.setPrimaryWindowUILayoutDebugOptions(options);
    };
    if (bottomPanel_ != BottomPanelKind::Layout) {
        return applyLayoutDebugOptions();
    }

    if (layoutDebugSelectedNode_.hasValue()) {
        auto selectedNodeAlive = tree.isAlive(layoutDebugSelectedNode_);
        if (!selectedNodeAlive) {
            return Tina::Core::failure(std::move(selectedNodeAlive.error()));
        }
        if (!*selectedNodeAlive) {
            if (auto status = tree.clearTreeViewSelection(layoutDebugTree_);
                !status) {
                return status;
            }
            layoutDebugSelectedNode_ = {};
            layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
            layoutDebugSelectedEntry_.reset();
            layoutDebugPickFeedback_ = "Selected node was destroyed";
            layoutDebugDetailsRefreshPending_ = true;
        }
    }

    auto snapshot = context.committedLayoutDebugSnapshot();
    if (!snapshot) {
        return Tina::Core::failure(std::move(snapshot.error()));
    }
    const bool snapshotChanged =
        !layoutDebugSnapshotInitialized_ ||
        layoutDebugStructureRevision_ != snapshot->structureRevision() ||
        layoutDebugLayoutRevision_ != snapshot->layoutRevision();
    bool pickedThisFrame = false;
    bool projectionChanged = false;
    bool hadSelectionToRestore = false;
    std::optional<Tina::Core::usize> restoredSelectionIndex{};
    if (snapshotChanged) {
        auto selectionBeforeRebuild = tree.treeViewSelection(layoutDebugTree_);
        if (!selectionBeforeRebuild) {
            return Tina::Core::failure(std::move(selectionBeforeRebuild.error()));
        }
        UI::UITreeViewItemKey selectionKeyToRestore = layoutDebugSelectionKey_;
        if (selectionBeforeRebuild->hasValue()) {
            selectionKeyToRestore = selectionBeforeRebuild->key;
        }
        hadSelectionToRestore =
            selectionKeyToRestore != UI::InvalidUITreeViewItemKey ||
            layoutDebugSelectedNode_.hasValue();

        const auto entries = snapshot->entries();
        const Tina::Core::usize count = (std::min)(
            entries.size(), static_cast<Tina::Core::usize>(LayoutDebugProjectionCapacity));
        const Tina::Core::usize previousProjectionCount =
            layoutDebugProjectionCount_;
        std::array<Tina::Core::usize, LayoutDebugProjectionCapacity> order{};
        for (Tina::Core::usize index = 0U; index < count; ++index) {
            order[index] = index;
        }
        std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(count),
                  [&entries](Tina::Core::usize left, Tina::Core::usize right) {
                      return entries[left].preorder < entries[right].preorder;
                  });

        layoutDebugSnapshotEntryCount_ = entries.size();
        layoutDebugProjectionCount_ = 0U;
        layoutDebugPanelSubtreeNodeCount_ = 0U;
        layoutDebugPanelProjectionRangeValid_ = false;
        layoutDebugStructureRevision_ = snapshot->structureRevision();
        layoutDebugLayoutRevision_ = snapshot->layoutRevision();

        Tina::Core::usize panelOrderIndex = count;
        u32 panelDepth = 0U;
        for (Tina::Core::usize orderIndex = 0U; orderIndex < count;
             ++orderIndex) {
            const auto& entry = entries[order[orderIndex]];
            if (entry.node == layoutDebugPanel_) {
                panelOrderIndex = orderIndex;
                panelDepth = entry.depth;
                layoutDebugPanelPreorderBegin_ = entry.preorder;
                break;
            }
        }
        if (panelOrderIndex < count) {
            Tina::Core::usize panelOrderEnd = panelOrderIndex + 1U;
            while (panelOrderEnd < count &&
                   entries[order[panelOrderEnd]].depth > panelDepth) {
                ++panelOrderEnd;
            }
            layoutDebugPanelPreorderEnd_ =
                panelOrderEnd < count
                    ? entries[order[panelOrderEnd]].preorder
                    : entries[order[count - 1U]].preorder + 1U;
            layoutDebugPanelProjectionRangeValid_ = true;
            for (Tina::Core::usize orderIndex = panelOrderIndex;
                 orderIndex < panelOrderEnd; ++orderIndex) {
                layoutDebugPanelSubtreeNodes_[layoutDebugPanelSubtreeNodeCount_++] =
                    entries[order[orderIndex]].node;
            }
        }

        for (Tina::Core::usize orderIndex = 0U; orderIndex < count;
             ++orderIndex) {
            const auto& entry = entries[order[orderIndex]];
            if (layoutDebugPanelProjectionRangeValid_ &&
                entry.preorder >= layoutDebugPanelPreorderBegin_ &&
                entry.preorder < layoutDebugPanelPreorderEnd_) {
                continue;
            }
            LayoutDebugProjectionRow row{};
            row.node = entry.node;
            row.key = layoutDebugNodeKey(entry.node);
            row.depth = entry.depth;
            row.preorder = entry.preorder;
            const std::string label = layoutDebugNodeLabel(entry);
            const u32 labelLength = static_cast<u32>((std::min)(
                label.size(), row.label.size() - 1U));
            std::memcpy(row.label.data(), label.data(), labelLength);
            row.label[labelLength] = '\0';
            row.labelLength = labelLength;
            if (layoutDebugProjectionCount_ >= previousProjectionCount ||
                layoutDebugProjection_[layoutDebugProjectionCount_] != row) {
                projectionChanged = true;
            }
            layoutDebugProjection_[layoutDebugProjectionCount_] = row;
            if (row.key == selectionKeyToRestore ||
                (selectionKeyToRestore == UI::InvalidUITreeViewItemKey &&
                 row.node == layoutDebugSelectedNode_)) {
                restoredSelectionIndex = layoutDebugProjectionCount_;
            }
            ++layoutDebugProjectionCount_;
        }
        projectionChanged = projectionChanged ||
                            layoutDebugProjectionCount_ != previousProjectionCount;
        layoutDebugSnapshotInitialized_ = true;
        layoutDebugDetailsRefreshPending_ = true;
    }
    if (projectionChanged) {
        if (auto status = tree.invalidateTreeViewItems(layoutDebugTree_); !status) {
            return status;
        }
        if (restoredSelectionIndex.has_value()) {
            const auto& restored = layoutDebugProjection_[*restoredSelectionIndex];
            if (auto status = tree.setTreeViewSelectedIndex(
                    layoutDebugTree_, *restoredSelectionIndex); !status) {
                return status;
            }
            layoutDebugSelectedNode_ = restored.node;
            layoutDebugSelectionKey_ = restored.key;
        } else if (hadSelectionToRestore) {
            if (auto status = tree.clearTreeViewSelection(layoutDebugTree_);
                !status) {
                return status;
            }
            layoutDebugSelectedNode_ = {};
            layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
            layoutDebugPickFeedback_ = "Selected node is no longer inspectable";
        }
    }

    if (pendingLayoutDebugPickPoint_.has_value()) {
        auto hit = context.queryCommittedPrimaryWindowUIPointerHit(
            *pendingLayoutDebugPickPoint_);
        pendingLayoutDebugPickPoint_.reset();
        if (!hit) {
            return Tina::Core::failure(std::move(hit.error()));
        }
        if (hit->hasTarget()) {
            bool found = false;
            for (Tina::Core::usize index = 0U;
                 index < layoutDebugProjectionCount_; ++index) {
                if (layoutDebugProjection_[index].node == hit->target.node) {
                    layoutDebugSelectedNode_ = hit->target.node;
                    layoutDebugSelectionKey_ = layoutDebugProjection_[index].key;
                    if (auto status = tree.setTreeViewSelectedIndex(
                            layoutDebugTree_, index); !status) {
                        return status;
                    }
                    layoutDebugPickFeedback_ = "Picked committed target";
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (auto status = tree.clearTreeViewSelection(layoutDebugTree_);
                    !status) {
                    return status;
                }
                layoutDebugSelectedNode_ = {};
                layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
                layoutDebugPickFeedback_ =
                    "Picked target is outside the inspectable snapshot";
            }
        } else {
            if (auto status = tree.clearTreeViewSelection(layoutDebugTree_);
                !status) {
                return status;
            }
            layoutDebugSelectedNode_ = {};
            layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
            layoutDebugPickFeedback_ = "No target at pointer location";
        }
        layoutDebugDetailsRefreshPending_ = true;
        pickedThisFrame = true;
    }

    auto selection = tree.treeViewSelection(layoutDebugTree_);
    if (!selection) {
        return Tina::Core::failure(std::move(selection.error()));
    }
    if (!pickedThisFrame && selection->hasValue() &&
        selection->logicalIndex < layoutDebugProjectionCount_) {
        const auto& row = layoutDebugProjection_[selection->logicalIndex];
        if (row.node != layoutDebugSelectedNode_) {
            layoutDebugSelectedNode_ = row.node;
            layoutDebugSelectionKey_ = selection->key;
            layoutDebugDetailsRefreshPending_ = true;
        }
    } else if (!pickedThisFrame && selection->hasValue()) {
        layoutDebugSelectedNode_ = {};
        layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
        layoutDebugDetailsRefreshPending_ = true;
    }

    bool selectedNodeStillPresent = false;
    for (Tina::Core::usize index = 0U;
         index < layoutDebugProjectionCount_; ++index) {
        if (layoutDebugProjection_[index].node == layoutDebugSelectedNode_) {
            selectedNodeStillPresent = true;
            break;
        }
    }
    if (!selectedNodeStillPresent && layoutDebugSelectedNode_.hasValue()) {
        if (auto status = tree.clearTreeViewSelection(layoutDebugTree_);
            !status) {
            return status;
        }
        layoutDebugSelectedNode_ = {};
        layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
        layoutDebugDetailsRefreshPending_ = true;
    }
    if (layoutDebugSelectedNode_.hasValue()) {
        auto selectedNodeAlive = tree.isAlive(layoutDebugSelectedNode_);
        if (!selectedNodeAlive) {
            return Tina::Core::failure(std::move(selectedNodeAlive.error()));
        }
        if (!*selectedNodeAlive) {
            if (auto status = tree.clearTreeViewSelection(layoutDebugTree_);
                !status) {
                return status;
            }
            layoutDebugSelectedNode_ = {};
            layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
            layoutDebugPickFeedback_ = "Selected node was destroyed";
            layoutDebugDetailsRefreshPending_ = true;
        }
    }

    if (!layoutDebugDetailsRefreshPending_) {
        return applyLayoutDebugOptions();
    }
    layoutDebugDetailsRefreshPending_ = false;
    layoutDebugSelectedEntry_.reset();
    for (const auto& entry : snapshot->entries()) {
        if (entry.node == layoutDebugSelectedNode_) {
            layoutDebugSelectedEntry_ = entry;
            break;
        }
    }

    std::array<std::string, LayoutDebugDetailRowCount> details{};
    if (!layoutDebugSelectedEntry_.has_value()) {
        details[0] = layoutDebugPickFeedback_.empty()
                         ? "Select a committed node"
                         : layoutDebugPickFeedback_;
        for (Tina::Core::usize index = 1U; index < details.size(); ++index) {
            details[index] = "-";
        }
        details.back() = "State: no selection";
    } else {
        const auto& entry = *layoutDebugSelectedEntry_;
        details[0] = layoutDebugElementTypeName(entry.elementType);
        details[0] += "  #" + std::to_string(entry.preorder);
        details[0] += "  node=" + std::to_string(entry.node.index());
        details[0] += ":" + std::to_string(entry.node.generation());
        details[0] += " parent=";
        if (entry.parent.hasValue()) {
            details[0] += std::to_string(entry.parent.index());
            details[0] += ":" + std::to_string(entry.parent.generation());
        } else {
            details[0] += "none";
        }
        details[1] = "Local: "; appendRect(details[1], entry.localRect);
        details[2] = "World: "; appendRect(details[2], entry.worldRect);
        details[3] = "Clip: "; appendRect(details[3], entry.effectiveClip);
        details[4] = "Content box: "; appendRect(details[4], entry.contentPlacement.contentBox);
        details[5] = "Content: origin="; appendText(details[5], "(");
        appendFloat(details[5], entry.contentPlacement.origin.x);
        appendText(details[5], ", ");
        appendFloat(details[5], entry.contentPlacement.origin.y);
        appendText(details[5], "), intrinsic=");
        appendSize(details[5], entry.contentPlacement.intrinsicSize);
        appendText(details[5], ", present=");
        appendText(details[5], entry.contentPlacement.hasIntrinsicContent ? "true" : "false");
        details[6] = "Measured: "; appendSize(details[6], entry.measuredSize);
        details[7] = "Min-content: "; appendSize(details[7], entry.minContentSize);
        details[7] += " | max-content: "; appendSize(details[7], entry.maxContentSize);
        details[8] = "Parent basis: width=";
        appendFloat(details[8], entry.basis.parentContentWidth);
        appendText(details[8], entry.basis.parentContentWidthDefinite
                                   ? " definite, height="
                                   : " indefinite, height=");
        appendFloat(details[8], entry.basis.parentContentHeight);
        appendText(details[8], entry.basis.parentContentHeightDefinite
                                   ? " definite" : " indefinite");
        details[9] = "Content basis: width=";
        appendFloat(details[9], entry.basis.contentWidth);
        appendText(details[9], entry.basis.contentWidthDefinite
                                   ? " definite, height="
                                   : " indefinite, height=");
        appendFloat(details[9], entry.basis.contentHeight);
        appendText(details[9], entry.basis.contentHeightDefinite
                                   ? " definite" : " indefinite");
        details[10] = "Authored: "; details[10] += layoutDebugStyleSizing(entry.authoredStyle);
        details[11] = "Authored: "; details[11] += layoutDebugStyleBox(entry.authoredStyle);
        details[12] = "Authored: "; details[12] += layoutDebugFlexContainer(entry.authoredStyle);
        details[13] = "Authored: "; details[13] += layoutDebugFlexItem(entry.authoredStyle);
        details[14] = "Authored: "; details[14] += layoutDebugGridContainer(entry.authoredStyle);
        details[15] = "Authored: "; details[15] += layoutDebugGridItem(entry.authoredStyle);
        details[16] = "Authored: "; details[16] += layoutDebugOverlay(entry.authoredStyle);
        details[17] = "Resolved: "; details[17] += layoutDebugStyleSizing(entry.resolvedStyle);
        details[18] = "Resolved: "; details[18] += layoutDebugStyleBox(entry.resolvedStyle);
        details[19] = "Resolved: "; details[19] += layoutDebugFlexContainer(entry.resolvedStyle);
        details[20] = "Resolved: "; details[20] += layoutDebugFlexItem(entry.resolvedStyle);
        details[21] = "Resolved: "; details[21] += layoutDebugGridContainer(entry.resolvedStyle);
        details[22] = "Resolved: "; details[22] += layoutDebugGridItem(entry.resolvedStyle);
        details[23] = "Resolved: "; details[23] += layoutDebugOverlay(entry.resolvedStyle);
        details[24] = "State: "; details[24] += layoutDebugNodeState(entry);
    }
    for (u32 index = 0U; index < layoutDebugDetailLabels_.size(); ++index) {
        if (auto status = tree.setText(layoutDebugDetailLabels_[index], details[index]);
            !status) {
            return status;
        }
    }
    std::string summary = "Inspectable " +
                          std::to_string(layoutDebugProjectionCount_) +
                          " / snapshot " +
                          std::to_string(layoutDebugSnapshotEntryCount_);
    summary += " | viewport ";
    appendSize(summary, snapshot->viewportSize());
    if (!layoutDebugPickFeedback_.empty()) {
        summary += " | " + layoutDebugPickFeedback_;
    }
    if (auto status = tree.setText(layoutDebugSummary_, summary); !status) {
        return status;
    }
    if (auto status = tree.setText(
            layoutDebugShowAllButton_,
            layoutDebugShowAllVisibleBounds_ ? "All Bounds" : "Selected");
        !status) {
        return status;
    }
    if (auto status = tree.setStyleRole(
            layoutDebugShowAllButton_,
            layoutDebugShowAllVisibleBounds_
                ? UI::UIStyleRoleId::ButtonPrimary
                : UI::UIStyleRoleId::ButtonOutlined);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            layoutDebugPickButton_, layoutDebugPickArmed_ ? "Picking" : "Pick");
        !status) {
        return status;
    }
    if (auto status = tree.setStyleRole(
            layoutDebugPickButton_,
            layoutDebugPickArmed_ ? UI::UIStyleRoleId::ButtonPrimary
                                  : UI::UIStyleRoleId::ButtonOutlined);
        !status) {
        return status;
    }
    return applyLayoutDebugOptions();
}

} // namespace Tina::EditorApp::WorkspaceInternal
