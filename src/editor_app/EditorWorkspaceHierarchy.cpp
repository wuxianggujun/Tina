#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] constexpr char hierarchyAsciiLower(char value) noexcept
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
}

[[nodiscard]] bool hierarchyLabelContains(
    std::string_view label, std::string_view filter) noexcept
{
    if (filter.empty()) {
        return true;
    }
    if (filter.size() > label.size()) {
        return false;
    }
    for (Tina::Core::usize offset = 0;
         offset + filter.size() <= label.size(); ++offset) {
        bool matches = true;
        for (Tina::Core::usize index = 0; index < filter.size(); ++index) {
            if (hierarchyAsciiLower(label[offset + index]) !=
                hierarchyAsciiLower(filter[index])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Tina::Core::Result<std::string_view> world2DHierarchyKindName(
    const Tina::AssetFormat::World2DEntityDesc& entity)
{
    const auto registry = Tina::Editor::world2DNodeTemplateRegistry();
    auto nodeKind = Tina::Editor::classifyWorld2DNodeTemplate(entity);
    if (!nodeKind) {
        return Tina::Core::failure(std::move(nodeKind.error()));
    }
    const auto index = static_cast<Tina::Core::usize>(*nodeKind);
    if (index >= registry.size()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "World2D node kind is outside the creation registry");
    }
    return registry[index].displayName;
}

[[nodiscard]] Tina::Core::Result<std::string_view> world3DHierarchyKindName(
    const Tina::AssetFormat::PrefabNodeView& node)
{
    const auto registry = Tina::Editor::world3DNodeTemplateRegistry();
    auto nodeKind = Tina::Editor::classifyWorld3DNodeTemplate(node);
    if (!nodeKind) {
        return Tina::Core::failure(std::move(nodeKind.error()));
    }
    const auto index = static_cast<Tina::Core::usize>(*nodeKind);
    if (index >= registry.size()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "World3D node kind is outside the creation registry");
    }
    return registry[index].displayName;
}

} // namespace

auto EditorWorkspaceState::hierarchyIndexForStableId(
    u64 stableId) const noexcept -> std::optional<u64>{
    for (u64 index = 0; index < hierarchyItemCount(this); ++index) {
        UI::UITreeViewItemDescriptor descriptor{};
        if (resolveHierarchyItem(this, index, descriptor) &&
            stableEntityIdForHierarchyItem(descriptor.key) == stableId) {
            return index;
        }
    }
    return std::nullopt;
}

auto EditorWorkspaceState::automaticHierarchyStableId(
    bool preferLast) const noexcept -> std::optional<u32>{
    if (!previewWorld_.has_value()) {
        return std::nullopt;
    }
    const auto transformable = [this](const EditorHierarchyRow& row) noexcept {
        if (row.stableId == 0U || !hierarchyRowVisible(row)) {
            return false;
        }
        const Tina::Scene::EntityId entity = findPreviewEntity(row.stableId);
        return entity.hasValue() && previewWorld_->worldTransform(entity) != nullptr;
    };
    if (preferLast) {
        const auto row = std::find_if(hierarchyRows_.rbegin(),
                                      hierarchyRows_.rend(), transformable);
        return row == hierarchyRows_.rend()
                   ? std::nullopt
                   : std::optional<u32>{row->stableId};
    }
    const auto row = std::find_if(hierarchyRows_.begin(), hierarchyRows_.end(),
                                  transformable);
    return row == hierarchyRows_.end()
               ? std::nullopt
               : std::optional<u32>{row->stableId};
}

auto EditorWorkspaceState::synchronizeViewportSelectionFromHierarchy() noexcept -> void{
    const Tina::Core::usize previousCount = viewportSelectedEntityCount_;
    const auto previousSelection = std::span<const u64>(
        viewportSelectedEntityIds_.data(), previousCount);
    std::array<u64, Tina::Editor::EditorMarqueeSelectionCapacity> next{};
    Tina::Core::usize nextCount = 0;
    viewportSelectedEntityCount_ = 0;
    if (assetInspectorActive_) {
        if (!previousSelection.empty()) {
            ++viewportSelectionRevision_;
        }
        return;
    }
    const u64 stableId = stableEntityIdForHierarchyItem(selectionKey_);
    if (stableId != 0U) {
        next[0] = stableId;
        nextCount = 1;
    }
    const bool changed = previousSelection.size() != nextCount ||
                         !std::equal(previousSelection.begin(),
                                     previousSelection.end(), next.begin());
    if (changed) {
        std::copy_n(next.begin(), nextCount,
                    viewportSelectedEntityIds_.begin());
        ++viewportSelectionRevision_;
    }
    viewportSelectedEntityCount_ = nextCount;
}

auto EditorWorkspaceState::hierarchyRowCollapsed(u32 stableId) const noexcept -> bool{
    return std::find(collapsedHierarchyIds_.begin(),
                     collapsedHierarchyIds_.end(), stableId) !=
           collapsedHierarchyIds_.end();
}

auto EditorWorkspaceState::hierarchyRow(u32 stableId) const noexcept -> const EditorHierarchyRow*{
    const auto found = std::find_if(
        hierarchyRows_.begin(), hierarchyRows_.end(),
        [stableId](const EditorHierarchyRow& row) {
            return row.stableId == stableId;
        });
    return found != hierarchyRows_.end() ? &*found : nullptr;
}

auto EditorWorkspaceState::hierarchyDisplayLabel(
    UI::UITreeViewItemKey key) const noexcept -> std::string_view{
    const u32 stableId = stableEntityIdForHierarchyItem(key);
    const EditorHierarchyRow* row = hierarchyRow(stableId);
    if (row != nullptr) {
        return row->label;
    }
    return stableId == 0U
               ? (workspaceMode_ == WorkspaceMode::World2D
                      ? std::string_view{"World2D Scene"}
                      : std::string_view{"World3D Scene"})
               : std::string_view{"Unavailable scene item"};
}

auto EditorWorkspaceState::hierarchyDisplayKind(
    UI::UITreeViewItemKey key) const noexcept -> std::string_view{
    const u32 stableId = stableEntityIdForHierarchyItem(key);
    if (stableId == 0U) {
        return workspaceMode_ == WorkspaceMode::World2D
                   ? std::string_view{"World2D document"}
                   : std::string_view{"Prefab v4 document"};
    }
    // Reports the node template so the Inspector header agrees with both the
    // hierarchy label and the kind the user picked at creation time.
    const EditorHierarchyRow* row = hierarchyRow(stableId);
    if (row != nullptr && !row->kindName.empty()) {
        return row->kindName;
    }
    return workspaceMode_ == WorkspaceMode::World2D
               ? std::string_view{"Node2D"}
               : std::string_view{"Node3D"};
}

auto EditorWorkspaceState::hierarchyDisplayNote(
    UI::UITreeViewItemKey key) const noexcept -> std::string_view{
    if (stableEntityIdForHierarchyItem(key) == 0U) {
        return workspaceMode_ == WorkspaceMode::World2D
                   ? std::string_view{"Canonical World2D scene root."}
                   : std::string_view{"Canonical Prefab v4 scene root."};
    }
        return workspaceMode_ == WorkspaceMode::World2D
               ? std::string_view{"Stable Node2D identity in the scene hierarchy."}
               : std::string_view{"Stable Node3D identity in the scene hierarchy."};
}

auto EditorWorkspaceState::hierarchyRowVisible(const EditorHierarchyRow& row) const noexcept -> bool{
    if (row.stableId == 0) {
        return true;
    }
    if (!hierarchyFilterUtf8_.empty()) {
        const auto rowIterator = std::find_if(
            hierarchyRows_.begin(), hierarchyRows_.end(),
            [&row](const EditorHierarchyRow& candidate) {
                return candidate.stableId == row.stableId;
            });
        if (rowIterator == hierarchyRows_.end()) {
            return false;
        }
        for (auto candidate = rowIterator; candidate != hierarchyRows_.end();
             ++candidate) {
            if (candidate != rowIterator && candidate->level <= row.level) {
                break;
            }
            if (hierarchyLabelContains(candidate->label, hierarchyFilterUtf8_)) {
                return true;
            }
        }
        return false;
    }
    if (hierarchyRowCollapsed(0)) {
        return false;
    }
    u32 parentStableId = row.parentStableId;
    for (Tina::Core::usize depth = 0;
         parentStableId != 0 && depth <= hierarchyRows_.size(); ++depth) {
        if (hierarchyRowCollapsed(parentStableId)) {
            return false;
        }
        const EditorHierarchyRow* parent = hierarchyRow(parentStableId);
        if (parent == nullptr) {
            return false;
        }
        parentStableId = parent->parentStableId;
    }
    return parentStableId == 0;
}

auto EditorWorkspaceState::visibleHierarchyIndex(u32 stableId) const noexcept -> std::optional<u64>{
    u64 index = 0;
    for (const EditorHierarchyRow& row : hierarchyRows_) {
        if (!hierarchyRowVisible(row)) {
            continue;
        }
        if (row.stableId == stableId) {
            return index;
        }
        ++index;
    }
    return std::nullopt;
}

auto EditorWorkspaceState::hierarchyEntityLabel(
    std::string_view kindName, u32 stableId) -> std::string{
    std::string label{kindName};
    label += " #";
    label += std::to_string(stableId);
    return label;
}

auto EditorWorkspaceState::hierarchyLabelForStableId(
    std::string_view kindName, u32 stableId,
    std::string_view authoredName) const -> std::string
{
    if (!authoredName.empty()) {
        return std::string{authoredName};
    }
    return hierarchyEntityLabel(kindName, stableId);
}

auto EditorWorkspaceState::rebuildHierarchyModel() -> Tina::Core::Status{
    try {
        std::vector<EditorHierarchyRow> candidate;
        candidate.reserve(AuthoringEntityCapacity + 1U);
        candidate.push_back({
            .key = HierarchyDocumentRootKey,
            .stableId = 0,
            .parentStableId = 0,
            .level = 0,
            .expandable = true,
            .label = workspaceMode_ == WorkspaceMode::World2D
                         ? "World2D Scene"
                         : "World3D Scene",
        });

        struct PendingHierarchyNode final {
            Tina::Core::usize index = 0;
            u32 level = 0;
        };

        if (workspaceMode_ == WorkspaceMode::World2D) {
            std::vector<Tina::AssetFormat::World2DEntityDesc> entities;
            auto snapshot = document_.parseCurrentSnapshot(entities);
            if (!snapshot) {
                return Tina::Core::failure(std::move(snapshot.error()));
            }

            for (Tina::Core::usize index = 0; index < entities.size(); ++index) {
                if (entities[index].stableEntityId == 0U ||
                    std::any_of(
                        entities.begin(), entities.begin() +
                            static_cast<std::ptrdiff_t>(index),
                        [&](const auto& previous) {
                            return previous.stableEntityId ==
                                   entities[index].stableEntityId;
                        })) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "World2D hierarchy contains a duplicate or zero stable ID");
                }
            }

            const Tina::Core::usize documentRootIndex = entities.size();
            std::vector<std::vector<Tina::Core::usize>> children(
                entities.size() + 1U);
            for (Tina::Core::usize index = 0; index < entities.size(); ++index) {
                Tina::Core::usize parentIndex = documentRootIndex;
                if (entities[index].parentStableEntityId != 0U) {
                    const auto parent = std::find_if(
                        entities.begin(), entities.end(), [&](const auto& candidateEntity) {
                            return candidateEntity.stableEntityId ==
                                   entities[index].parentStableEntityId;
                        });
                    if (parent == entities.end()) {
                        return Tina::Core::failure(
                            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                            "World2D hierarchy contains a missing parent");
                    }
                    parentIndex = static_cast<Tina::Core::usize>(
                        std::distance(entities.begin(), parent));
                }
                children[parentIndex].push_back(index);
            }

            std::vector<PendingHierarchyNode> pending;
            pending.reserve(entities.size());
            for (auto root = children[documentRootIndex].rbegin();
                 root != children[documentRootIndex].rend(); ++root) {
                pending.push_back({.index = *root, .level = 1U});
            }
            std::vector<u8> published(entities.size(), 0U);
            Tina::Core::usize publishedCount = 0;
            while (!pending.empty()) {
                const PendingHierarchyNode current = pending.back();
                pending.pop_back();
                if (published[current.index] != 0U) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "World2D hierarchy would publish an entity more than once");
                }
                published[current.index] = 1U;
                ++publishedCount;
                const auto& entity = entities[current.index];
                auto kindName = world2DHierarchyKindName(entity);
                if (!kindName) {
                    return Tina::Core::failure(std::move(kindName.error()));
                }
                candidate.push_back({
                    .key = entity.stableEntityId,
                    .stableId = entity.stableEntityId,
                    .parentStableId = entity.parentStableEntityId,
                    .level = current.level,
                    .expandable = !children[current.index].empty(),
                    .label = hierarchyLabelForStableId(*kindName,
                                                       entity.stableEntityId,
                                                       entity.name),
                    .kindName = *kindName,
                });
                for (auto child = children[current.index].rbegin();
                     child != children[current.index].rend(); ++child) {
                    pending.push_back({
                        .index = *child,
                        .level = current.level + 1U,
                    });
                }
            }
            if (publishedCount != entities.size()) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "World2D hierarchy contains a parent cycle");
            }
        } else {
            std::vector<Tina::AssetFormat::PrefabNodeView> nodes;
            auto prefab = document3D_.parseCurrentPrefab(nodes);
            if (!prefab) {
                return Tina::Core::failure(std::move(prefab.error()));
            }

            for (Tina::Core::usize index = 0; index < nodes.size(); ++index) {
                if (nodes[index].stableNodeId == 0U ||
                    std::any_of(
                        nodes.begin(), nodes.begin() +
                            static_cast<std::ptrdiff_t>(index),
                        [&](const auto& previous) {
                            return previous.stableNodeId == nodes[index].stableNodeId;
                        })) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "World3D hierarchy contains a duplicate or zero stable ID");
                }
                if (nodes[index].parentIndex < -1 ||
                    (nodes[index].parentIndex >= 0 &&
                     static_cast<Tina::Core::usize>(nodes[index].parentIndex) >=
                         nodes.size())) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "World3D hierarchy contains an out-of-range parent index");
                }
            }

            const Tina::Core::usize documentRootIndex = nodes.size();
            std::vector<std::vector<Tina::Core::usize>> children(nodes.size() + 1U);
            for (Tina::Core::usize index = 0; index < nodes.size(); ++index) {
                const Tina::Core::usize parentIndex = nodes[index].parentIndex < 0
                    ? documentRootIndex
                    : static_cast<Tina::Core::usize>(nodes[index].parentIndex);
                children[parentIndex].push_back(index);
            }

            std::vector<PendingHierarchyNode> pending;
            pending.reserve(nodes.size());
            for (auto root = children[documentRootIndex].rbegin();
                 root != children[documentRootIndex].rend(); ++root) {
                pending.push_back({.index = *root, .level = 1U});
            }
            std::vector<u8> published(nodes.size(), 0U);
            Tina::Core::usize publishedCount = 0;
            while (!pending.empty()) {
                const PendingHierarchyNode current = pending.back();
                pending.pop_back();
                if (published[current.index] != 0U) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "World3D hierarchy would publish a node more than once");
                }
                published[current.index] = 1U;
                ++publishedCount;
                const auto& node = nodes[current.index];
                const u32 parentStableId = node.parentIndex < 0
                    ? 0U
                    : nodes[static_cast<Tina::Core::usize>(node.parentIndex)]
                          .stableNodeId;
                auto kindName = world3DHierarchyKindName(node);
                if (!kindName) {
                    return Tina::Core::failure(std::move(kindName.error()));
                }
                candidate.push_back({
                    .key = node.stableNodeId,
                    .stableId = node.stableNodeId,
                    .parentStableId = parentStableId,
                    .level = current.level,
                    .expandable = !children[current.index].empty(),
                    .label = hierarchyLabelForStableId(*kindName, node.stableNodeId,
                                                       node.name),
                    .kindName = *kindName,
                });
                for (auto child = children[current.index].rbegin();
                     child != children[current.index].rend(); ++child) {
                    pending.push_back({
                        .index = *child,
                        .level = current.level + 1U,
                    });
                }
            }
            if (publishedCount != nodes.size()) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "World3D hierarchy contains a parent cycle");
            }
        }

        candidate.front().expandable = candidate.size() > 1U;
        collapsedHierarchyIds_.erase(
            std::remove_if(
                collapsedHierarchyIds_.begin(),
                collapsedHierarchyIds_.end(),
                [&](u32 stableId) {
                    const auto found = std::find_if(
                        candidate.begin(), candidate.end(),
                        [stableId](const EditorHierarchyRow& row) {
                            return row.stableId == stableId && row.expandable;
                        });
                    return found == candidate.end();
                }),
            collapsedHierarchyIds_.end());
        hierarchyRows_.swap(candidate);
        return Tina::Core::success();
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor hierarchy model allocation failed");
    }
}

auto EditorWorkspaceState::refreshHierarchyTree(
    Tina::PrimaryWindowUITreeUpdater& tree, u32 preferredStableId) -> Tina::Core::Status{
    if (auto status = rebuildHierarchyModel(); !status) {
        return status;
    }
    if (auto status = tree.invalidateTreeViewItems(hierarchyTree_); !status) {
        return status;
    }
    std::string countText =
        std::to_string(hierarchyRows_.empty() ? 0U : hierarchyRows_.size() - 1U);
    countText += " nodes";
    if (auto status = tree.setText(hierarchyCount_, countText); !status) {
        return status;
    }
    const u64 index = visibleHierarchyIndex(preferredStableId).value_or(0U);
    if (auto status = tree.setTreeViewSelectedIndex(hierarchyTree_, index); !status) {
        return status;
    }
    UI::UITreeViewItemDescriptor selected{};
    if (!resolveHierarchyItem(this, index, selected)) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Editor hierarchy could not resolve its selected item");
    }
    selectionKey_ = selected.key;
    synchronizeViewportSelectionFromHierarchy();
    counters_.hierarchyLogicalItems = hierarchyItemCount(this);
    return Tina::Core::success();
}

auto EditorWorkspaceState::hierarchyDataSource() noexcept -> UI::UITreeViewDataSource{
    return UI::UITreeViewDataSource{
        .state = this,
        .itemCount = &EditorWorkspaceState::hierarchyItemCount,
        .resolveItem = &EditorWorkspaceState::resolveHierarchyItem,
        .setItemExpanded = &EditorWorkspaceState::setHierarchyExpanded,
    };
}

auto EditorWorkspaceState::hierarchyItemCount(const void* state) noexcept -> u64{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr) {
        return 0;
    }
    u64 count = 0;
    for (const EditorHierarchyRow& row : self->hierarchyRows_) {
        if (self->hierarchyRowVisible(row)) {
            ++count;
        }
    }
    return count;
}

auto EditorWorkspaceState::resolveHierarchyItem(const void* state, u64 logicalIndex,
                                 UI::UITreeViewItemDescriptor& output) noexcept -> bool{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr) {
        return false;
    }
    u64 visibleIndex = 0;
    for (const EditorHierarchyRow& row : self->hierarchyRows_) {
        if (!self->hierarchyRowVisible(row)) {
            continue;
        }
        if (visibleIndex++ != logicalIndex) {
            continue;
        }
        output = UI::UITreeViewItemDescriptor{
            .key = row.key,
            .label = row.label,
            .level = row.level,
            .enabled = true,
            .expandable = row.expandable,
            .expanded = row.expandable &&
                (!self->hierarchyFilterUtf8_.empty() ||
                 !self->hierarchyRowCollapsed(row.stableId)),
        };
        return true;
    }
    return false;
}

auto EditorWorkspaceState::setHierarchyExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept -> bool{
    auto* self = static_cast<EditorWorkspaceState*>(state);
    if (self == nullptr) {
        return false;
    }
    const u32 stableId = stableEntityIdForHierarchyItem(key);
    const auto row = std::find_if(
        self->hierarchyRows_.begin(), self->hierarchyRows_.end(),
        [stableId](const EditorHierarchyRow& candidate) {
            return candidate.stableId == stableId;
        });
    if (row == self->hierarchyRows_.end() || !row->expandable) {
        return false;
    }
    const auto collapsed = std::find(self->collapsedHierarchyIds_.begin(),
                                     self->collapsedHierarchyIds_.end(),
                                     stableId);
    if (expanded) {
        if (collapsed != self->collapsedHierarchyIds_.end()) {
            self->collapsedHierarchyIds_.erase(collapsed);
        }
    } else if (collapsed == self->collapsedHierarchyIds_.end()) {
        if (self->collapsedHierarchyIds_.size() >=
            self->collapsedHierarchyIds_.capacity()) {
            return false;
        }
        self->collapsedHierarchyIds_.push_back(stableId);
    }
    return true;
}

auto EditorWorkspaceState::hierarchyStableIdAtPosition(
    UI::UILogicalPoint position) const noexcept -> std::optional<u32>
{
    if (hierarchyTreeRect_.width <= 0.0F || hierarchyTreeRect_.height <= 0.0F ||
        position.x < hierarchyTreeRect_.x || position.x >= hierarchyTreeRect_.right() ||
        position.y < hierarchyTreeRect_.y || position.y >= hierarchyTreeRect_.bottom() ||
        hierarchyTreeRowHeight_ <= 0.0F) {
        return std::nullopt;
    }
    const float localY = position.y - hierarchyTreeRect_.y +
                         hierarchyTreeMetrics_.scrollOffset;
    if (localY < 0.0F) {
        return std::nullopt;
    }
    const u64 logicalIndex = static_cast<u64>(localY / hierarchyTreeRowHeight_);
    if (logicalIndex >= hierarchyTreeMetrics_.logicalItemCount) {
        return std::nullopt;
    }
    UI::UITreeViewItemDescriptor descriptor{};
    if (!resolveHierarchyItem(this, logicalIndex, descriptor)) {
        return std::nullopt;
    }
    return stableEntityIdForHierarchyItem(descriptor.key);
}

auto EditorWorkspaceState::hierarchyDropRequestAtPosition(
    UI::UILogicalPoint position) const noexcept
    -> std::optional<HierarchyDropRequest>
{
    if (hierarchyDragStableId_ == 0U) {
        return std::nullopt;
    }
    const auto targetStableId = hierarchyStableIdAtPosition(position);
    if (!targetStableId.has_value() || *targetStableId == hierarchyDragStableId_) {
        return std::nullopt;
    }
    const float localY = position.y - hierarchyTreeRect_.y +
                         hierarchyTreeMetrics_.scrollOffset;
    const float rowOffset = std::fmod(localY, hierarchyTreeRowHeight_);
    const float rowFraction = hierarchyTreeRowHeight_ > 0.0F
                                  ? rowOffset / hierarchyTreeRowHeight_
                                  : 0.5F;
    HierarchyDropIntent intent = HierarchyDropIntent::Reparent;
    if (*targetStableId != 0U && rowFraction < 0.25F) {
        intent = HierarchyDropIntent::ReorderBefore;
    } else if (*targetStableId != 0U && rowFraction > 0.75F) {
        intent = HierarchyDropIntent::ReorderAfter;
    }
    return HierarchyDropRequest{
        .sourceStableId = hierarchyDragStableId_,
        .targetStableId = *targetStableId,
        .intent = intent,
    };
}

auto EditorWorkspaceState::refreshHierarchyDropIndicator(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const auto hideIndicator = [&]() -> Tina::Core::Status {
        hierarchyDropIndicatorStableId_ = 0U;
        hierarchyPublishedDrop_.reset();
        hierarchyDropIndicatorIntent_ = HierarchyDropIntent::Reparent;
        hierarchyDropIndicatorLayout_ =
            hierarchyRenameLayout(UI::UIVisibility::Collapsed);
        return hierarchyDropIndicator_.hasValue()
                   ? tree.setLayoutStyle(
                         hierarchyDropIndicator_, hierarchyDropIndicatorLayout_)
                   : Tina::Core::success();
    };
    if (!hierarchyDropIndicator_.hasValue() || !hierarchyDragActive_ ||
        hierarchyDragStableId_ == 0U) {
        return hideIndicator();
    }

    const auto dropRequest =
        hierarchyDropRequestAtPosition(hierarchyDragCurrentPosition_);
    if (!dropRequest.has_value()) {
        return hideIndicator();
    }
    const u32 targetStableId = dropRequest->targetStableId;
    const auto logicalIndex = visibleHierarchyIndex(targetStableId);
    if (!logicalIndex.has_value()) {
        return hideIndicator();
    }
    auto rowNode = tree.treeViewMaterializedItemNode(
        hierarchyTree_, *logicalIndex);
    if (!rowNode) {
        return Tina::Core::failure(std::move(rowNode.error()));
    }
    if (!rowNode->hasValue()) {
        return hideIndicator();
    }
    auto rowRect = tree.committedLayoutRect(*rowNode);
    if (!rowRect) {
        return Tina::Core::failure(std::move(rowRect.error()));
    }
    auto dockRect = tree.committedLayoutRect(leftDock_);
    if (!dockRect) {
        return Tina::Core::failure(std::move(dockRect.error()));
    }

    const HierarchyDropIntent intent = dropRequest->intent;
    std::string_view indicatorText = "[Inside]";
    if (intent == HierarchyDropIntent::ReorderBefore) {
        indicatorText = "[Before]";
    } else if (intent == HierarchyDropIntent::ReorderAfter) {
        indicatorText = "[After]";
    }

    constexpr float IndicatorWidth = 96.0F;
    constexpr float IndicatorHeight = 22.0F;
    constexpr float IndicatorInset = 6.0F;
    const float contentOriginX = dockRect->x + leftDockLayout_.padding.left;
    const float contentOriginY = dockRect->y + leftDockLayout_.padding.top;
    const float offsetX =
        (std::max)(rowRect->x + IndicatorInset,
                   rowRect->right() - IndicatorWidth - IndicatorInset) -
        contentOriginX;
    float indicatorY = rowRect->y +
                       (rowRect->height - IndicatorHeight) * 0.5F;
    if (intent == HierarchyDropIntent::ReorderBefore) {
        indicatorY = rowRect->y;
    } else if (intent == HierarchyDropIntent::ReorderAfter) {
        indicatorY = rowRect->bottom() - IndicatorHeight;
    }
    hierarchyDropIndicatorStableId_ = targetStableId;
    hierarchyDropIndicatorIntent_ = intent;
    hierarchyPublishedDrop_ = *dropRequest;
    hierarchyDropIndicatorLayout_ = hierarchyRenameLayout(
        UI::UIVisibility::Visible, offsetX, indicatorY - contentOriginY,
        IndicatorWidth, IndicatorHeight);
    if (auto status = tree.setText(hierarchyDropIndicator_, indicatorText);
        !status) {
        return status;
    }
    return tree.setLayoutStyle(
        hierarchyDropIndicator_, hierarchyDropIndicatorLayout_);
}

auto EditorWorkspaceState::updateHierarchyPreselectionVisual(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    UI::UILayoutStyle collapsed =
        hierarchyRenameLayout(UI::UIVisibility::Collapsed);
    if (!hierarchyPreselectionNode_.hasValue() ||
        hierarchyPreselectionStableId_ == 0U || hierarchyDragActive_) {
        return tree.setLayoutStyle(hierarchyPreselectionNode_, collapsed);
    }
    const auto logicalIndex =
        visibleHierarchyIndex(hierarchyPreselectionStableId_);
    if (!logicalIndex.has_value()) {
        hierarchyPreselectionStableId_ = 0U;
        return tree.setLayoutStyle(hierarchyPreselectionNode_, collapsed);
    }
    auto rowNode = tree.treeViewMaterializedItemNode(
        hierarchyTree_, *logicalIndex);
    if (!rowNode) {
        return Tina::Core::failure(std::move(rowNode.error()));
    }
    if (!rowNode->hasValue()) {
        return tree.setLayoutStyle(hierarchyPreselectionNode_, collapsed);
    }
    auto rowRect = tree.committedLayoutRect(*rowNode);
    if (!rowRect) {
        return Tina::Core::failure(std::move(rowRect.error()));
    }
    auto dockRect = tree.committedLayoutRect(leftDock_);
    if (!dockRect) {
        return Tina::Core::failure(std::move(dockRect.error()));
    }
    const float contentOriginX = dockRect->x + leftDockLayout_.padding.left;
    const float contentOriginY = dockRect->y + leftDockLayout_.padding.top;
    constexpr float PreselectionInset = 1.0F;
    const float offsetX = rowRect->x - contentOriginX + PreselectionInset;
    const float offsetY = rowRect->y - contentOriginY + PreselectionInset;
    const float width = (std::max)(1.0F, rowRect->width - PreselectionInset * 2.0F);
    const float height = (std::max)(1.0F, rowRect->height - PreselectionInset * 2.0F);
    UI::UILayoutStyle layout = hierarchyRenameLayout(
        UI::UIVisibility::Visible, offsetX, offsetY, width, height);
    return tree.setLayoutStyle(hierarchyPreselectionNode_, layout);
}

auto EditorWorkspaceState::projectAssetVisibleIndexAtPosition(
    UI::UILogicalPoint position) const noexcept -> std::optional<u64>
{
    const UI::UILogicalRect& rect = projectAssetListRect_;
    const UI::UIVirtualGridViewMetrics& metrics = projectAssetListMetrics_;
    const UI::UIVirtualGridViewStyle& style = projectAssetListStyle_;
    if (rect.width <= 0.0F || rect.height <= 0.0F ||
        position.x < rect.x || position.x >= rect.right() ||
        position.y < rect.y || position.y >= rect.bottom() ||
        metrics.logicalColumnCount == 0U || metrics.itemWidth <= 0.0F ||
        style.itemHeight <= 0.0F) {
        return std::nullopt;
    }
    const float localX = position.x - rect.x;
    const float localY = position.y - rect.y + metrics.scrollOffset;
    const float columnStride = metrics.itemWidth + style.columnGap;
    const float rowStride = style.itemHeight + style.rowGap;
    if (localX < 0.0F || localY < 0.0F || columnStride <= 0.0F ||
        rowStride <= 0.0F) {
        return std::nullopt;
    }
    const u32 column = static_cast<u32>(localX / columnStride);
    const u64 row = static_cast<u64>(localY / rowStride);
    if (column >= metrics.logicalColumnCount ||
        localX - static_cast<float>(column) * columnStride >= metrics.itemWidth ||
        row > (std::numeric_limits<u64>::max)() /
                    static_cast<u64>(metrics.logicalColumnCount)) {
        return std::nullopt;
    }
    const u64 logicalIndex =
        row * static_cast<u64>(metrics.logicalColumnCount) + column;
    return logicalIndex < metrics.logicalItemCount
               ? std::optional<u64>{logicalIndex}
               : std::nullopt;
}

void EditorWorkspaceState::handleHierarchyPointerDown(
    UI::UIRoutedPointerEvent& event) noexcept
{
    const auto& input = event.input();
    hierarchyPreselectionStableId_ =
        hierarchyStableIdAtPosition(input.position).value_or(0U);
    if (input.button == Tina::Platform::PointerButton::Secondary) {
        const auto stableId = hierarchyStableIdAtPosition(input.position);
        hierarchyContextStableId_ = stableId.value_or(0U);
        if (stableId.has_value() && *stableId != 0U) {
            pendingSelectionStableId_ = *stableId;
        } else {
            // The context menu belongs to a node, not to empty tree chrome.
            // Prevent the UI menu fallback from opening a disabled menu for
            // blank space below the last hierarchy row.
            event.preventDefaultAction();
            event.consumeInputTransition();
        }
        return;
    }
    if (input.button != Tina::Platform::PointerButton::Primary) {
        return;
    }
    hierarchyContextStableId_ = 0U;
    const auto stableId = hierarchyStableIdAtPosition(input.position);
    if (!stableId.has_value() || *stableId == 0U) {
        return;
    }
    const u64 frame = counters_.frameUpdates;
    if (*stableId == lastHierarchyPointerDownStableId_ &&
        frame >= lastHierarchyPointerDownFrame_ &&
        frame - lastHierarchyPointerDownFrame_ <= 24U) {
        pendingHierarchyRenameStableId_ = *stableId;
    }
    lastHierarchyPointerDownStableId_ = *stableId;
    lastHierarchyPointerDownFrame_ = frame;
    hierarchyDragPointer_ = input.pointer;
    hierarchyDragStableId_ = *stableId;
    hierarchyDragStartPosition_ = input.position;
    hierarchyDragCurrentPosition_ = input.position;
    hierarchyDragActive_ = false;
    event.capturePointer();
}

void EditorWorkspaceState::handleHierarchyPointerMove(
    UI::UIRoutedPointerEvent& event) noexcept
{
    hierarchyPreselectionStableId_ =
        hierarchyStableIdAtPosition(event.input().position).value_or(0U);
    if (!hierarchyDragStableId_ || event.input().pointer != hierarchyDragPointer_) {
        return;
    }
    const auto& position = event.input().position;
    hierarchyDragCurrentPosition_ = position;
    const float dx = position.x - hierarchyDragStartPosition_.x;
    const float dy = position.y - hierarchyDragStartPosition_.y;
    if (!hierarchyDragActive_ && (dx * dx + dy * dy) >= 25.0F) {
        hierarchyDragActive_ = true;
        // A drag is never the second click of a rename gesture.
        lastHierarchyPointerDownStableId_ = 0U;
        pendingHierarchyRenameStableId_.reset();
        if (hierarchyRenameVisible_) {
            pendingHierarchyRenameCancel_ = true;
        }
    }
    if (hierarchyDragActive_) {
        hierarchyPublishedDrop_ = hierarchyDropRequestAtPosition(position);
        event.consumeInputTransition();
        event.preventDefaultAction();
    }
}

void EditorWorkspaceState::handleHierarchyPointerUp(
    UI::UIRoutedPointerEvent& event) noexcept
{
    hierarchyPreselectionStableId_ =
        hierarchyStableIdAtPosition(event.input().position).value_or(0U);
    if (event.input().pointer != hierarchyDragPointer_ ||
        event.input().button != Tina::Platform::PointerButton::Primary) {
        return;
    }
    if (hierarchyDragActive_) {
        if (hierarchyPublishedDrop_.has_value() &&
            hierarchyPublishedDrop_->sourceStableId == hierarchyDragStableId_ &&
            hierarchyPublishedDrop_->sourceStableId !=
                hierarchyPublishedDrop_->targetStableId) {
            pendingHierarchyDrop_ = *hierarchyPublishedDrop_;
        }
        event.consumeInputTransition();
        event.preventDefaultAction();
    }
    event.releasePointerCapture();
    hierarchyDragStableId_ = 0;
    hierarchyDragActive_ = false;
    hierarchyDropIndicatorStableId_ = 0U;
    hierarchyPublishedDrop_.reset();
    hierarchyDropIndicatorIntent_ = HierarchyDropIntent::Reparent;
}

void EditorWorkspaceState::handleHierarchyPointerCancel(
    UI::UIRoutedPointerEvent& event) noexcept
{
    if (hierarchyDragStableId_ == 0U ||
        event.input().pointer != hierarchyDragPointer_) {
        return;
    }
    event.releasePointerCapture();
    event.consumeInputTransition();
    event.preventDefaultAction();
    hierarchyDragStableId_ = 0U;
    hierarchyDragActive_ = false;
    hierarchyPreselectionStableId_ = 0U;
    hierarchyDragCurrentPosition_ = {};
    hierarchyDropIndicatorStableId_ = 0U;
    hierarchyDropIndicatorIntent_ = HierarchyDropIntent::Reparent;
    hierarchyPublishedDrop_.reset();
    lastHierarchyPointerDownStableId_ = 0U;
    pendingHierarchyRenameStableId_.reset();
}

void EditorWorkspaceState::handleProjectAssetPointerDown(
    UI::UIRoutedPointerEvent& event) noexcept
{
    const auto& input = event.input();
    if (input.button != Tina::Platform::PointerButton::Primary &&
        input.button != Tina::Platform::PointerButton::Secondary) {
        return;
    }
    if (!projectAssets_.visibleItemCount()) {
        if (input.button == Tina::Platform::PointerButton::Secondary) {
            event.preventDefaultAction();
            event.consumeInputTransition();
        }
        return;
    }
    const auto index = projectAssetVisibleIndexAtPosition(input.position);
    if (!index.has_value()) {
        if (input.button == Tina::Platform::PointerButton::Secondary) {
            event.preventDefaultAction();
            event.consumeInputTransition();
        }
        return;
    }
    const auto* asset = projectAssets_.visibleItem(
        static_cast<Tina::Core::usize>(*index));
    if (asset == nullptr) {
        if (input.button == Tina::Platform::PointerButton::Secondary) {
            event.preventDefaultAction();
            event.consumeInputTransition();
        }
        return;
    }
    projectAssetContextAssetId_ = asset->assetId;
    if (input.button == Tina::Platform::PointerButton::Secondary) {
        // The retained Menu opens from the anchored list through its default
        // secondary-button behavior. Empty chrome is handled below without
        // opening a disabled menu.
        return;
    }
    const u64 frame = counters_.frameUpdates;
    if (asset->assetId == lastProjectAssetPointerDownAssetId_ &&
        frame >= lastProjectAssetPointerDownFrame_ &&
        frame - lastProjectAssetPointerDownFrame_ <= 24U) {
        pendingProjectAssetOpen_ = asset->assetId;
        if (!queueEditorCommand(EditorCommand::OpenSelectedProjectAsset)) {
            pendingProjectAssetOpen_.reset();
        }
    }
    lastProjectAssetPointerDownAssetId_ = asset->assetId;
    lastProjectAssetPointerDownFrame_ = frame;
    projectAssetDragPointer_ = input.pointer;
    projectAssetDragAssetId_ = asset->assetId;
    projectAssetDragStartPosition_ = input.position;
    projectAssetDragActive_ = false;
    event.capturePointer();
}

void EditorWorkspaceState::handleProjectAssetPointerMove(
    UI::UIRoutedPointerEvent& event) noexcept
{
    if (event.input().pointer != projectAssetDragPointer_ ||
        !projectAssetDragAssetId_) {
        return;
    }
    const float dx = event.input().position.x - projectAssetDragStartPosition_.x;
    const float dy = event.input().position.y - projectAssetDragStartPosition_.y;
    if (!projectAssetDragActive_ && (dx * dx + dy * dy) >= 25.0F) {
        projectAssetDragActive_ = true;
    }
    if (projectAssetDragActive_) {
        event.consumeInputTransition();
        event.preventDefaultAction();
    }
}

void EditorWorkspaceState::handleProjectAssetPointerUp(
    UI::UIRoutedPointerEvent& event) noexcept
{
    if (event.input().pointer != projectAssetDragPointer_ ||
        event.input().button != Tina::Platform::PointerButton::Primary) {
        return;
    }
    if (projectAssetDragActive_) {
        const auto target = hierarchyStableIdAtPosition(event.input().position);
        if (target.has_value() && *target != 0U) {
            pendingProjectAssetDrop_ = ProjectAssetDropRequest{
                .assetId = projectAssetDragAssetId_,
                .targetStableId = *target,
            };
        } else {
            authoringFeedback_ =
                "Resource drag cancelled: release over a Sprite node in Hierarchy";
        }
        event.consumeInputTransition();
        event.preventDefaultAction();
    }
    event.releasePointerCapture();
    projectAssetDragAssetId_ = {};
    projectAssetDragActive_ = false;
}

void EditorWorkspaceState::handleProjectAssetPointerCancel(
    UI::UIRoutedPointerEvent& event) noexcept
{
    if (!projectAssetDragAssetId_ ||
        event.input().pointer != projectAssetDragPointer_) {
        return;
    }
    event.releasePointerCapture();
    event.consumeInputTransition();
    event.preventDefaultAction();
    projectAssetDragAssetId_ = {};
    projectAssetDragActive_ = false;
    lastProjectAssetPointerDownAssetId_ = {};
    pendingProjectAssetOpen_.reset();
}

} // namespace Tina::EditorApp::WorkspaceInternal
