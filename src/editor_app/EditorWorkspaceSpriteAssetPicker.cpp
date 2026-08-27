#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

// Sprite2D/AnimatedSprite2D accept an authored Sprite or an imported Texture2D
// used directly as the sprite source. Resource nodes accept exactly the kind
// their template declares. Anything else would author a reference the binding
// registry rejects.
[[nodiscard]] bool isPickableKind(
    Tina::AssetFormat::AssetKind candidate,
    Tina::AssetFormat::AssetKind required) noexcept
{
    if (required != Tina::AssetFormat::AssetKind::Invalid) {
        return candidate == required;
    }
    return candidate == Tina::AssetFormat::AssetKind::Sprite ||
           candidate == Tina::AssetFormat::AssetKind::Texture2D;
}

[[nodiscard]] bool matchesPickerFilter(std::string_view text,
                                      std::string_view filter) noexcept
{
    if (filter.empty()) {
        return true;
    }
    if (filter.size() > text.size()) {
        return false;
    }
    const auto lower = [](char value) noexcept {
        return value >= 'A' && value <= 'Z'
                   ? static_cast<char>(value + ('a' - 'A'))
                   : value;
    };
    for (Tina::Core::usize offset = 0; offset + filter.size() <= text.size();
         ++offset) {
        bool matches = true;
        for (Tina::Core::usize index = 0; index < filter.size(); ++index) {
            if (lower(text[offset + index]) != lower(filter[index])) {
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

void EditorWorkspaceState::rebuildSpriteAssetPickerRows() noexcept
try {
    spriteAssetPickerRows_.clear();
    const auto assets = projectAssets_.items();
    for (Tina::Core::usize index = 0; index < assets.size(); ++index) {
        const auto& asset = assets[index];
        if (!isPickableKind(asset.assetKind, spriteAssetPickerKind_)) {
            continue;
        }
        if (!matchesPickerFilter(asset.displayName,
                                 spriteAssetPickerFilterUtf8_) &&
            !matchesPickerFilter(
                Tina::Editor::projectAssetKindLabel(asset.assetKind),
                spriteAssetPickerFilterUtf8_)) {
            continue;
        }
        spriteAssetPickerRows_.push_back(index);
    }
    // A selection hidden by the filter is no longer confirmable, so drop it
    // instead of letting Assign publish an invisible row.
    if (spriteAssetPickerSelectedAssetId_) {
        const bool visible = std::any_of(
            spriteAssetPickerRows_.begin(), spriteAssetPickerRows_.end(),
            [&](Tina::Core::usize index) {
                return assets[index].assetId == spriteAssetPickerSelectedAssetId_;
            });
        if (!visible) {
            spriteAssetPickerSelectedAssetId_ = {};
        }
    }
} catch (const std::bad_alloc&) {
    // Fail closed to an empty list: the picker then reports "no assets" instead
    // of indexing a partially rebuilt row table.
    spriteAssetPickerRows_.clear();
    spriteAssetPickerSelectedAssetId_ = {};
}

auto EditorWorkspaceState::spriteAssetPickerItemCount(const void* state) noexcept
    -> u64
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self == nullptr
               ? 0U
               : static_cast<u64>(self->spriteAssetPickerRows_.size());
}

auto EditorWorkspaceState::spriteAssetPickerResolveItem(
    const void* state, u64 logicalIndex,
    UI::UIVirtualGridViewItemDescriptor& output) noexcept -> bool
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr ||
        logicalIndex >= static_cast<u64>(self->spriteAssetPickerRows_.size())) {
        return false;
    }
    const auto assets = self->projectAssets_.items();
    const Tina::Core::usize assetIndex =
        self->spriteAssetPickerRows_[static_cast<Tina::Core::usize>(logicalIndex)];
    if (assetIndex >= assets.size()) {
        return false;
    }
    const auto& asset = assets[assetIndex];
    output = UI::UIVirtualGridViewItemDescriptor{
        // Keys must be non-zero and stable within one Catalog snapshot; the
        // owned sorted index plus one satisfies both.
        .key = static_cast<UI::UIVirtualGridViewItemKey>(assetIndex + 1U),
        .label = asset.displayName,
        .enabled = true,
        .presentation = UI::UIVirtualGridViewItemPresentation{
            .secondaryLabel =
                Tina::Editor::projectAssetKindLabel(asset.assetKind),
            .status = UI::UIVirtualGridViewItemStatus::Ready,
        },
    };
    return true;
}

auto EditorWorkspaceState::spriteAssetPickerDataSource() const noexcept
    -> UI::UIVirtualGridViewDataSource
{
    return UI::UIVirtualGridViewDataSource{
        .state = this,
        .itemCount = &EditorWorkspaceState::spriteAssetPickerItemCount,
        .resolveItem = &EditorWorkspaceState::spriteAssetPickerResolveItem,
    };
}

auto EditorWorkspaceState::showSpriteAssetPicker(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (spriteAssetPickerVisible_) {
        return Tina::Core::success();
    }
    // One Dialog per window: refuse instead of stacking modals.
    if (pendingSceneAddRequest_.has_value() ||
        pendingSceneDeleteConfirmation_.has_value() ||
        pendingProjectAssetRemoveConfirmation_.has_value() ||
        pendingDirtyCloseKey_.has_value()) {
        authoringFeedback_ =
            "Sprite picker unavailable: close the open dialog first";
        return Tina::Core::success();
    }
    const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
    if (!authoringEnabled() || !sceneDocumentActive() ||
        workspaceMode_ != WorkspaceMode::World2D || stableId == 0U) {
        authoringFeedback_ =
            "Resource picker requires an editable World2D node selection";
        return Tina::Core::success();
    }
    // Seed the picker with the node's current binding so reopening it shows what
    // is bound instead of an empty selection.
    std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document_.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Tina::Core::failure(std::move(snapshot.error()));
    }
    const auto entity = std::find_if(
        storage.begin(), storage.end(), [stableId](const auto& candidate) {
            return candidate.stableEntityId == stableId;
        });
    if (entity == storage.end()) {
        authoringFeedback_ =
            "Resource picker cancelled: the selection is absent from the document";
        return Tina::Core::success();
    }
    auto nodeTemplate = Tina::Editor::classifyWorld2DNodeTemplate(*entity);
    if (!nodeTemplate) {
        return Tina::Core::failure(std::move(nodeTemplate.error()));
    }
    if (entity->sprite.has_value()) {
        spriteAssetPickerKind_ = Tina::AssetFormat::AssetKind::Invalid;
        spriteAssetPickerSelectedAssetId_ = entity->sprite->spriteId;
    } else if (entity->resource.has_value()) {
        spriteAssetPickerKind_ = requiredResourceAssetKind(*nodeTemplate);
        spriteAssetPickerSelectedAssetId_ = entity->resource->assetId;
    } else {
        authoringFeedback_ =
            "This node kind has no asset binding to pick";
        return Tina::Core::success();
    }
    spriteAssetPickerTargetStableId_ = stableId;
    spriteAssetPickerFilterUtf8_.clear();
    rebuildSpriteAssetPickerRows();
    spriteAssetPickerObservedSelection_.reset();
    if (auto status = tree.setText(spriteAssetPickerSearchInput_, {}); !status) {
        return status;
    }
    if (auto status = refreshSpriteAssetPickerUi(tree); !status) {
        return status;
    }
    if (auto status = tree.setText(
            spriteAssetPickerDialog_.title,
            spriteAssetPickerKind_ == Tina::AssetFormat::AssetKind::Invalid
                ? std::string_view{"Select Sprite or Texture2D"}
                : Tina::Editor::projectAssetKindLabel(spriteAssetPickerKind_));
        !status) {
        return status;
    }
    if (auto status = tree.openDialog(spriteAssetPickerDialog_.modal); !status) {
        return status;
    }
    spriteAssetPickerVisible_ = true;
    spriteAssetPickerFocusPending_ = true;
    authoringFeedback_ = "Select an asset to assign";
    return Tina::Core::success();
}

auto EditorWorkspaceState::hideSpriteAssetPicker(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!spriteAssetPickerVisible_) {
        return Tina::Core::success();
    }
    if (auto status = tree.dismissDialog(spriteAssetPickerDialog_.modal);
        !status) {
        return status;
    }
    spriteAssetPickerVisible_ = false;
    spriteAssetPickerFocusPending_ = false;
    spriteAssetPickerTargetStableId_ = 0U;
    spriteAssetPickerSelectedAssetId_ = {};
    spriteAssetPickerKind_ = Tina::AssetFormat::AssetKind::Invalid;
    spriteAssetPickerObservedSelection_.reset();
    spriteAssetPickerRows_.clear();
    spriteAssetPickerFilterUtf8_.clear();
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::refreshSpriteAssetPickerUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (auto status = tree.setVirtualGridViewDataSource(
            spriteAssetPickerList_, spriteAssetPickerDataSource());
        !status) {
        return status;
    }
    if (auto status = tree.invalidateVirtualGridViewItems(spriteAssetPickerList_);
        !status) {
        return status;
    }
    const auto assets = projectAssets_.items();
    std::optional<Tina::Core::usize> selectedRow{};
    if (spriteAssetPickerSelectedAssetId_) {
        for (Tina::Core::usize row = 0; row < spriteAssetPickerRows_.size();
             ++row) {
            if (assets[spriteAssetPickerRows_[row]].assetId ==
                spriteAssetPickerSelectedAssetId_) {
                selectedRow = row;
                break;
            }
        }
    }
    if (selectedRow.has_value()) {
        auto metrics = tree.virtualGridViewMetrics(spriteAssetPickerList_);
        if (!metrics) {
            return Tina::Core::failure(std::move(metrics.error()));
        }
        // Only publish an index the collection has actually committed, or the
        // selection request would be rejected against a stale item shape.
        if (metrics->logicalColumnCount != 0U &&
            metrics->logicalItemCount ==
                static_cast<u64>(spriteAssetPickerRows_.size())) {
            if (auto status = tree.setVirtualGridViewSelectedIndex(
                    spriteAssetPickerList_, static_cast<u64>(*selectedRow));
                !status) {
                return status;
            }
            spriteAssetPickerObservedSelection_ = static_cast<u64>(*selectedRow);
        }
    } else if (auto status =
                   tree.clearVirtualGridViewSelection(spriteAssetPickerList_);
               !status) {
        return status;
    } else {
        spriteAssetPickerObservedSelection_.reset();
    }
    try {
        if (spriteAssetPickerRows_.empty()) {
            spriteAssetPickerStatusUtf8_.assign(
                projectAssets_.itemCount() == 0U
                    ? "This project has no assets yet. Use Import Files to add an image."
                    : "No Sprite or Texture2D matches this search.");
        } else {
            spriteAssetPickerStatusUtf8_.assign(
                std::to_string(spriteAssetPickerRows_.size()));
            spriteAssetPickerStatusUtf8_ +=
                spriteAssetPickerRows_.size() == 1U ? " asset" : " assets";
            if (spriteAssetPickerSelectedAssetId_) {
                const auto* selected = projectAssets_.inspectorSnapshot(
                    spriteAssetPickerSelectedAssetId_);
                if (selected != nullptr) {
                    spriteAssetPickerStatusUtf8_ += "  |  Selected: ";
                    spriteAssetPickerStatusUtf8_ += selected->displayName;
                }
            }
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Sprite picker status allocation failed");
    }
    if (auto status = tree.setText(spriteAssetPickerStatus_,
                                   spriteAssetPickerStatusUtf8_);
        !status) {
        return status;
    }
    return tree.setEnabled(spriteAssetPickerConfirmButton_,
                           spriteAssetPickerSelectedAssetId_.operator bool());
}

auto EditorWorkspaceState::updateSpriteAssetPickerSearch(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!spriteAssetPickerVisible_) {
        return Tina::Core::success();
    }
    auto text = tree.text(spriteAssetPickerSearchInput_);
    if (!text) {
        return Tina::Core::failure(std::move(text.error()));
    }
    if (*text == spriteAssetPickerFilterUtf8_) {
        return Tina::Core::success();
    }
    try {
        spriteAssetPickerFilterUtf8_.assign(*text);
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Sprite picker search allocation failed");
    }
    rebuildSpriteAssetPickerRows();
    spriteAssetPickerObservedSelection_.reset();
    return refreshSpriteAssetPickerUi(tree);
}

auto EditorWorkspaceState::synchronizeSpriteAssetPickerSelection(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!spriteAssetPickerVisible_) {
        return Tina::Core::success();
    }
    auto selection = tree.virtualGridViewSelection(spriteAssetPickerList_);
    if (!selection) {
        return Tina::Core::failure(std::move(selection.error()));
    }
    if (!selection->hasValue() ||
        spriteAssetPickerObservedSelection_ == selection->logicalIndex) {
        return Tina::Core::success();
    }
    spriteAssetPickerObservedSelection_ = selection->logicalIndex;
    const auto row = static_cast<Tina::Core::usize>(selection->logicalIndex);
    if (row >= spriteAssetPickerRows_.size()) {
        return Tina::Core::success();
    }
    const auto assets = projectAssets_.items();
    const Tina::Core::usize assetIndex = spriteAssetPickerRows_[row];
    if (assetIndex >= assets.size()) {
        return Tina::Core::success();
    }
    spriteAssetPickerSelectedAssetId_ = assets[assetIndex].assetId;
    return refreshSpriteAssetPickerUi(tree);
}

auto EditorWorkspaceState::confirmSpriteAssetPicker(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!spriteAssetPickerVisible_) {
        return Tina::Core::success();
    }
    const Tina::Core::AssetId assetId = spriteAssetPickerSelectedAssetId_;
    const u32 stableId = spriteAssetPickerTargetStableId_;
    if (!assetId || stableId == 0U) {
        authoringFeedback_ =
            "Assign cancelled: select a Sprite or Texture2D first";
        return hideSpriteAssetPicker(tree);
    }
    // The picker was opened against one node; if selection moved while it was
    // open the edit no longer belongs to that node.
    if (stableEntityIdForHierarchyItem(selectionKey_) != stableId) {
        authoringFeedback_ =
            "Assign cancelled: the scene selection changed while the picker was open";
        return hideSpriteAssetPicker(tree);
    }
    const bool spriteBinding =
        spriteAssetPickerKind_ == Tina::AssetFormat::AssetKind::Invalid;
    if (auto status = hideSpriteAssetPicker(tree); !status) {
        return status;
    }
    const std::array<u32, 1> ids{stableId};
    auto result = spriteBinding
        ? Tina::Editor::applyWorld2DSpriteNodeProperties(
              document_, ids, {.spriteId = assetId})
        : Tina::Editor::applyWorld2DResourceNodeProperties(
              document_, ids, {.assetId = assetId});
    if (!result) {
        return reportAuthoringFailure("Assign rejected: ", result.error());
    }
    if (result->affectedItemCount == 0U) {
        authoringFeedback_ =
            "Resource unchanged; no document revision was published";
        return refreshAuthoringUi(tree);
    }
    ++counters_.authoringEdits;
    ++counters_.inspectorTransactions;
    // Keep Project Assets pointing at what was just assigned without letting the
    // Asset Inspector take the panel from the node being edited.
    if (auto status = projectAssets_.selectAsset(assetId); status) {
        projectAssetSelectionSyncPending_ = true;
        preserveNodeInspectorOnProjectAssetSelection_ = true;
        assetInspectorActive_ = false;
    }
    authoringFeedback_ = "Resource assigned as one document revision";
    if (auto previewStatus = validateRuntimePreview(); !previewStatus) {
        return previewStatus;
    }
    return refreshAuthoringUi(tree);
}

} // namespace Tina::EditorApp::WorkspaceInternal
