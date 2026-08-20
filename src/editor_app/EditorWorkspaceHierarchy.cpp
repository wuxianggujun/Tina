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
    if (stableEntityIdForHierarchyItem(key) == 0U) {
        return workspaceMode_ == WorkspaceMode::World2D
                   ? std::string_view{"World2D document"}
                   : std::string_view{"Prefab v2 document"};
    }
    return workspaceMode_ == WorkspaceMode::World2D
               ? std::string_view{"Entity2D"}
               : std::string_view{"Node3D"};
}

auto EditorWorkspaceState::hierarchyDisplayNote(
    UI::UITreeViewItemKey key) const noexcept -> std::string_view{
    if (stableEntityIdForHierarchyItem(key) == 0U) {
        return workspaceMode_ == WorkspaceMode::World2D
                   ? std::string_view{"Canonical World2D scene root."}
                   : std::string_view{"Canonical Prefab v2 scene root."};
    }
    return workspaceMode_ == WorkspaceMode::World2D
               ? std::string_view{"Stable entity identity with validated components."}
               : std::string_view{"Stable node identity in the parent-first hierarchy."};
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
    WorkspaceMode mode, u32 stableId, bool hasRenderable,
    bool hasCamera, bool hasLight) -> std::string{
    std::string label;
    if (mode == WorkspaceMode::World2D) {
        if (hasCamera) {
            label = "Camera2D";
        } else if (hasLight) {
            label = "PointLight2D";
        } else if (hasRenderable) {
            label = "Sprite2D";
        } else {
            label = "Entity2D";
        }
    } else {
        label = hasRenderable ? "Mesh3D" : "Node3D";
    }
    label += " #";
    label += std::to_string(stableId);
    return label;
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
                candidate.push_back({
                    .key = entity.stableEntityId,
                    .stableId = entity.stableEntityId,
                    .parentStableId = entity.parentStableEntityId,
                    .level = current.level,
                    .expandable = !children[current.index].empty(),
                    .label = hierarchyEntityLabel(
                        WorkspaceMode::World2D, entity.stableEntityId,
                        entity.sprite.has_value(), entity.camera.has_value(),
                        entity.pointLight.has_value()),
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
                candidate.push_back({
                    .key = node.stableNodeId,
                    .stableId = node.stableNodeId,
                    .parentStableId = parentStableId,
                    .level = current.level,
                    .expandable = !children[current.index].empty(),
                    .label = hierarchyEntityLabel(
                        WorkspaceMode::World3D, node.stableNodeId,
                        node.hasMesh, false, false),
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

} // namespace Tina::EditorApp::WorkspaceInternal
