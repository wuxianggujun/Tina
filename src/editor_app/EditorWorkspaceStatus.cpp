#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] bool containsAsciiInsensitive(std::string_view text,
                                             std::string_view needle) noexcept
{
    if (needle.empty() || needle.size() > text.size()) {
        return needle.empty();
    }
    for (Tina::Core::usize offset = 0;
         offset + needle.size() <= text.size(); ++offset) {
        bool matches = true;
        for (Tina::Core::usize index = 0; index < needle.size(); ++index) {
            const auto lower = [](char value) noexcept {
                return value >= 'A' && value <= 'Z'
                           ? static_cast<char>(value + ('a' - 'A'))
                           : value;
            };
            if (lower(text[offset + index]) != lower(needle[index])) {
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

[[nodiscard]] OutputFilter outputFilterFor(std::string_view feedback) noexcept
{
    for (const std::string_view word : {"failed", "rejected", "error", "invalid", "cannot"}) {
        if (containsAsciiInsensitive(feedback, word)) {
            return OutputFilter::Error;
        }
    }
    for (const std::string_view word : {"cancel", "unchanged", "preserved", "retry", "stop"}) {
        if (containsAsciiInsensitive(feedback, word)) {
            return OutputFilter::Warning;
        }
    }
    return OutputFilter::Info;
}

[[nodiscard]] bool outputFilterMatches(OutputFilter selected,
                                       OutputFilter message) noexcept
{
    return selected == OutputFilter::All || selected == message;
}

[[nodiscard]] std::string_view outputSeverityLabel(OutputFilter filter) noexcept
{
    switch (filter) {
    case OutputFilter::Info:
        return "[i] Info";
    case OutputFilter::Warning:
        return "[!] Warn";
    case OutputFilter::Error:
        return "[x] Error";
    case OutputFilter::All:
        break;
    }
    return "[i] Info";
}

[[nodiscard]] std::string_view outputContextFor(std::string_view feedback) noexcept
{
    if (containsAsciiInsensitive(feedback, "source import") ||
        containsAsciiInsensitive(feedback, "file drop") ||
        containsAsciiInsensitive(feedback, "dropped file") ||
        containsAsciiInsensitive(feedback, "image import")) {
        return "Source Import";
    }
    if (containsAsciiInsensitive(feedback, "catalog")) {
        return "Catalog";
    }
    if (containsAsciiInsensitive(feedback, "project asset") ||
        containsAsciiInsensitive(feedback, "resource drop") ||
        containsAsciiInsensitive(feedback, "asset inspector")) {
        return "Project Assets";
    }
    if (containsAsciiInsensitive(feedback, "animation") ||
        containsAsciiInsensitive(feedback, "clip") ||
        containsAsciiInsensitive(feedback, "notify")) {
        return "Animation";
    }
    if (containsAsciiInsensitive(feedback, "tile")) {
        return "TileMap";
    }
    if (containsAsciiInsensitive(feedback, "transform") ||
        containsAsciiInsensitive(feedback, "inspector")) {
        return "Inspector";
    }
    if (containsAsciiInsensitive(feedback, "viewport") ||
        containsAsciiInsensitive(feedback, "gizmo") ||
        containsAsciiInsensitive(feedback, "marquee")) {
        return "Viewport";
    }
    if (containsAsciiInsensitive(feedback, "hierarchy") ||
        containsAsciiInsensitive(feedback, "scene node") ||
        containsAsciiInsensitive(feedback, "scene item") ||
        containsAsciiInsensitive(feedback, "subtree") ||
        containsAsciiInsensitive(feedback, "sibling") ||
        containsAsciiInsensitive(feedback, "reparent") ||
        containsAsciiInsensitive(feedback, "rename")) {
        return "Hierarchy";
    }
    if (containsAsciiInsensitive(feedback, "document") ||
        containsAsciiInsensitive(feedback, "save") ||
        containsAsciiInsensitive(feedback, "prefab") ||
        containsAsciiInsensitive(feedback, "world2d") ||
        containsAsciiInsensitive(feedback, "world3d")) {
        return "Document";
    }
    return "Editor";
}

[[nodiscard]] bool outputContextTargetsDocument(std::string_view context) noexcept
{
    return context == "Animation" || context == "TileMap" ||
           context == "Inspector" || context == "Viewport" ||
           context == "Hierarchy" || context == "Document";
}

[[nodiscard]] bool outputContextTargetsSceneNode(std::string_view context) noexcept
{
    return context == "Inspector" || context == "Viewport" ||
           context == "Hierarchy";
}

} // namespace

auto EditorWorkspaceState::tilePaletteDataSource() const noexcept
    -> UI::UIVirtualGridViewDataSource
{
    return {.state = this,
            .itemCount = &EditorWorkspaceState::tilePaletteItemCount,
            .resolveItem = &EditorWorkspaceState::resolveTilePaletteItem};
}

auto EditorWorkspaceState::tilePaletteItemCount(const void* state) noexcept -> u64
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self != nullptr ? self->tilePaletteTileCount_ : 0U;
}

bool EditorWorkspaceState::resolveTilePaletteItem(
    const void* state, u64 logicalIndex,
    UI::UIVirtualGridViewItemDescriptor& output) noexcept
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalIndex >= self->tilePaletteTileCount_) {
        return false;
    }
    const auto& tile = self->tilePaletteTiles_[logicalIndex];
    output.key = static_cast<UI::UIVirtualGridViewItemKey>(tile.localId) + 1U;
    output.enabled = self->tileMapEditingContext();
    output.label = self->tilePaletteLabels_[logicalIndex];
    output.presentation.secondaryLabel =
        (tile.materialFlags & Tina::AssetFormat::TilesetWire::MaterialSolid) != 0U
            ? "Solid" : "Tile";
    return true;
}

auto EditorWorkspaceState::reportAuthoringFailure(
    std::string_view prefix, const Tina::Core::Error& error) -> Tina::Core::Status{
    try {
        authoringFeedback_.assign(prefix);
        authoringFeedback_ += error.message;
        for (const auto& context : error.context) {
            authoringFeedback_ += " [";
            authoringFeedback_ += context.operation;
            if (!context.detail.empty()) {
                authoringFeedback_ += ": ";
                authoringFeedback_ += context.detail;
            }
            authoringFeedback_ += "]";
        }
        if (error.nativeCode.has_value()) {
            authoringFeedback_ += " (native=";
            authoringFeedback_ += std::to_string(*error.nativeCode);
            authoringFeedback_ += ")";
        }
        return Tina::Core::success();
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor authoring failure feedback allocation failed");
    }
}

auto EditorWorkspaceState::outputGridDataSource() const noexcept
    -> UI::UIDataGridDataSource
{
    return UI::UIDataGridDataSource{
        .state = this,
        .rowCount = &EditorWorkspaceState::outputRowCount,
        .columnCount = &EditorWorkspaceState::outputColumnCount,
        .resolveRow = &EditorWorkspaceState::resolveOutputRow,
        .resolveColumn = &EditorWorkspaceState::resolveOutputColumn,
        .resolveCell = &EditorWorkspaceState::resolveOutputCell,
    };
}

auto EditorWorkspaceState::outputRowCount(const void* state) noexcept -> u64
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self != nullptr ? self->outputVisibleHistoryCount_ : 0U;
}

auto EditorWorkspaceState::outputColumnCount(const void* state) noexcept -> u32
{
    return state != nullptr ? OutputColumnCapacity : 0U;
}

auto EditorWorkspaceState::resolveOutputRow(
    const void* state, u64 logicalRow,
    UI::UIDataGridRowDescriptor& output) noexcept -> bool
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalRow >= self->outputVisibleHistoryCount_) {
        return false;
    }
    const u32 historyIndex = self->outputVisibleHistoryIndices_[logicalRow];
    if (historyIndex >= self->outputHistoryCount_) {
        return false;
    }
    output = UI::UIDataGridRowDescriptor{
        .key = self->outputHistory_[historyIndex].sequence,
        .enabled = true,
    };
    return output.key != UI::InvalidUIDataGridRowKey;
}

auto EditorWorkspaceState::resolveOutputColumn(
    const void* state, u32 logicalColumn,
    UI::UIDataGridColumnDescriptor& output) noexcept -> bool
{
    if (state == nullptr || logicalColumn >= OutputColumnCapacity) {
        return false;
    }
    if (logicalColumn == 0U) {
        output = UI::UIDataGridColumnDescriptor{
            .key = 1U,
            .header = "Level",
            .width = OutputSeverityColumnWidth,
        };
    } else if (logicalColumn == 1U) {
        output = UI::UIDataGridColumnDescriptor{
            .key = 2U,
            .header = "Context",
            .width = OutputContextColumnWidth,
        };
    } else {
        output = UI::UIDataGridColumnDescriptor{
            .key = 3U,
            .header = "Message",
            .width = OutputMessageColumnWidth,
        };
    }
    return true;
}

auto EditorWorkspaceState::resolveOutputCell(
    const void* state, u64 logicalRow, u32 logicalColumn,
    UI::UIDataGridCellDescriptor& output) noexcept -> bool
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalRow >= self->outputVisibleHistoryCount_ ||
        logicalColumn >= OutputColumnCapacity) {
        return false;
    }
    const u32 historyIndex = self->outputVisibleHistoryIndices_[logicalRow];
    if (historyIndex >= self->outputHistoryCount_) {
        return false;
    }
    const EditorOutputHistoryEntry& entry = self->outputHistory_[historyIndex];
    output.text = logicalColumn == 0U
                      ? outputSeverityLabel(entry.filter)
                      : logicalColumn == 1U ? std::string_view{entry.context}
                                            : std::string_view{entry.message};
    return true;
}

auto EditorWorkspaceState::selectedOutputEntry() const noexcept
    -> const EditorOutputHistoryEntry*
{
    if (!outputSelectedSequence_.has_value()) {
        return nullptr;
    }
    for (Tina::Core::usize index = 0U; index < outputHistoryCount_; ++index) {
        if (outputHistory_[index].sequence == *outputSelectedSequence_) {
            return &outputHistory_[index];
        }
    }
    return nullptr;
}

auto EditorWorkspaceState::outputTargetAvailable(
    const EditorOutputHistoryEntry& entry) const noexcept -> bool
{
    if (entry.targetKind == EditorOutputTargetKind::Asset) {
        return entry.targetAssetId.hasValue() &&
               projectAssets_.inspectorSnapshot(entry.targetAssetId) != nullptr;
    }
    if (entry.targetKind == EditorOutputTargetKind::Document ||
        entry.targetKind == EditorOutputTargetKind::SceneNode) {
        return documentTabs_.find(entry.targetDocumentKey).has_value();
    }
    return false;
}

void EditorWorkspaceState::handleOutputPointerDown(
    UI::UIRoutedPointerEvent& event) noexcept
{
    const UI::UIPointerInputEvent& input = event.input();
    if (input.button != Tina::Platform::PointerButton::Primary ||
        outputGridRect_.width <= 0.0F || outputGridRect_.height <= 0.0F ||
        outputGridStyle_.rowHeight <= 0.0F ||
        input.position.x < outputGridRect_.x ||
        input.position.x >= outputGridRect_.right() ||
        input.position.y < outputGridRect_.y +
                               outputGridStyle_.columnHeaderHeight ||
        input.position.y >= outputGridRect_.bottom()) {
        return;
    }
    const float contentY =
        input.position.y - outputGridRect_.y -
        outputGridStyle_.columnHeaderHeight + outputGridMetrics_.scrollOffset.y;
    if (!std::isfinite(contentY) || contentY < 0.0F) {
        return;
    }
    const u64 logicalRow = static_cast<u64>(
        std::floor(contentY / outputGridStyle_.rowHeight));
    if (logicalRow >= outputVisibleHistoryCount_) {
        return;
    }
    const u32 historyIndex = outputVisibleHistoryIndices_[logicalRow];
    if (historyIndex >= outputHistoryCount_) {
        return;
    }
    const EditorOutputHistoryEntry& entry = outputHistory_[historyIndex];
    const u64 frame = counters_.frameUpdates;
    if (entry.sequence == lastOutputPointerDownSequence_ &&
        frame >= lastOutputPointerDownFrame_ &&
        frame - lastOutputPointerDownFrame_ <= 24U &&
        outputTargetAvailable(entry)) {
        pendingOutputLocateSequence_ = entry.sequence;
    }
    lastOutputPointerDownSequence_ = entry.sequence;
    lastOutputPointerDownFrame_ = frame;
}

auto EditorWorkspaceState::processPendingOutputLocate(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingOutputLocateSequence_.has_value()) {
        return Tina::Core::success();
    }
    const u64 sequence = *pendingOutputLocateSequence_;
    pendingOutputLocateSequence_.reset();
    const EditorOutputHistoryEntry* target = nullptr;
    for (Tina::Core::usize index = 0U; index < outputHistoryCount_; ++index) {
        if (outputHistory_[index].sequence == sequence) {
            target = &outputHistory_[index];
            break;
        }
    }
    if (target == nullptr || !outputTargetAvailable(*target)) {
        authoringFeedback_ =
            "Output target is no longer available; no selection changed";
        return Tina::Core::success();
    }
    if (target->targetKind == EditorOutputTargetKind::Asset) {
        if (auto status = projectAssets_.setTypeFilter(
                Tina::Editor::ProjectAssetTypeFilter::All);
            !status) {
            return status;
        }
        if (auto status = projectAssets_.setSearchQuery({}); !status) {
            return status;
        }
        if (auto status = projectAssets_.selectAsset(target->targetAssetId);
            !status) {
            return status;
        }
        projectBrowserUiRefreshPending_ = true;
        assetInspectorActive_ = true;
        authoringFeedback_ = "Output target located in Project Assets";
        return Tina::Core::success();
    }

    const auto tabIndex = documentTabs_.find(target->targetDocumentKey);
    if (!tabIndex.has_value()) {
        authoringFeedback_ =
            "Output document target is no longer open; no selection changed";
        return Tina::Core::success();
    }
    if (playSessionActive() && *tabIndex != documentTabs_.activeIndex()) {
        authoringFeedback_ =
            "Stop the isolated play session before locating another document";
        return Tina::Core::success();
    }
    if (auto status = activateDocumentTab(tree, static_cast<u32>(*tabIndex));
        !status) {
        return status;
    }
    if (target->targetKind == EditorOutputTargetKind::SceneNode) {
        if (visibleHierarchyIndex(target->targetStableId).has_value()) {
            pendingSelectionStableId_ = target->targetStableId;
            authoringFeedback_ = "Output target located in the active document";
        } else {
            authoringFeedback_ =
                "Output document located, but its original scene object is unavailable";
        }
    } else {
        authoringFeedback_ = "Output target document activated";
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshWorkspaceChrome(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (auto status = refreshViewportViewModeUi(tree); !status) {
        return status;
    }
    return refreshOutputAndStatusUi(tree);
}

auto EditorWorkspaceState::refreshOutputAndStatusUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    bool historyChanged = false;
    try {
        if (!authoringFeedback_.empty() &&
            authoringFeedback_ != outputHistoryObservedFeedback_) {
            if (outputHistoryCount_ == EditorOutputHistoryCapacity) {
                for (Tina::Core::usize index = 1U;
                     index < outputHistoryCount_; ++index) {
                    outputHistory_[index - 1U] = std::move(outputHistory_[index]);
                }
                --outputHistoryCount_;
            }
            EditorOutputHistoryEntry& entry =
                outputHistory_[outputHistoryCount_];
            entry = EditorOutputHistoryEntry{};
            entry.sequence = outputNextSequence_++;
            entry.filter = outputFilterFor(authoringFeedback_);
            entry.context.assign(outputContextFor(authoringFeedback_));
            entry.message = authoringFeedback_;
            if (entry.context == "Project Assets") {
                if (const auto assetId = projectAssets_.selectedAssetId();
                    assetId.has_value()) {
                    entry.targetKind = EditorOutputTargetKind::Asset;
                    entry.targetAssetId = *assetId;
                }
            } else if (outputContextTargetsDocument(entry.context)) {
                if (const auto* activeTab = documentTabs_.activeTab();
                    activeTab != nullptr) {
                    entry.targetKind = EditorOutputTargetKind::Document;
                    entry.targetDocumentKey = activeTab->key;
                    const u32 stableId =
                        stableEntityIdForHierarchyItem(selectionKey_);
                    if (stableId != 0U &&
                        outputContextTargetsSceneNode(entry.context)) {
                        entry.targetKind = EditorOutputTargetKind::SceneNode;
                        entry.targetStableId = stableId;
                    }
                }
            }
            ++outputHistoryCount_;
            outputHistoryObservedFeedback_ = authoringFeedback_;
            historyChanged = true;
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor Output history allocation failed");
    }

    outputVisibleHistoryCount_ = 0U;
    for (Tina::Core::usize reverse = outputHistoryCount_; reverse != 0U;
         --reverse) {
        const u32 historyIndex = static_cast<u32>(reverse - 1U);
        if (outputFilterMatches(outputFilter_,
                                outputHistory_[historyIndex].filter)) {
            outputVisibleHistoryIndices_[outputVisibleHistoryCount_++] =
                historyIndex;
        }
    }
    outputGridRefreshPending_ = outputGridRefreshPending_ || historyChanged;

    std::array<u32, 4> counts{};
    for (Tina::Core::usize index = 0U; index < outputHistoryCount_; ++index) {
        ++counts[0];
        const auto filterIndex = static_cast<Tina::Core::usize>(
            outputHistory_[index].filter);
        if (filterIndex < counts.size()) {
            ++counts[filterIndex];
        }
    }
    const std::array<std::string_view, 4> labels{"All", "Info", "Warn", "Error"};
    for (u32 index = 0; index < outputFilterButtons_.size(); ++index) {
        std::string label{labels[index]};
        label += " ";
        label += std::to_string(counts[index]);
        if (auto status = tree.setText(outputFilterButtons_[index], label); !status) {
            return status;
        }
        if (auto status = tree.setRadioButtonSelected(
                outputFilterButtons_[index],
                static_cast<OutputFilter>(index) == outputFilter_);
            !status) {
            return status;
        }
    }
    if (outputGridRefreshPending_) {
        if (auto status = tree.setDataGridDataSource(
                outputGrid_, outputGridDataSource());
            !status) {
            return status;
        }
        if (auto status = tree.invalidateDataGridItems(outputGrid_); !status) {
            return status;
        }
        if (outputVisibleHistoryCount_ == 0U) {
            if (auto status = tree.clearDataGridSelection(outputGrid_); !status) {
                return status;
            }
            outputSelectedSequence_.reset();
        } else {
            u64 selectedRow = 0U;
            if (outputSelectedSequence_.has_value()) {
                for (Tina::Core::usize row = 0U;
                     row < outputVisibleHistoryCount_; ++row) {
                    const auto& entry = outputHistory_[
                        outputVisibleHistoryIndices_[row]];
                    if (entry.sequence == *outputSelectedSequence_) {
                        selectedRow = row;
                        break;
                    }
                }
            }
            if (auto status = tree.setDataGridSelectedCell(
                    outputGrid_, selectedRow, 0U);
                !status) {
                return status;
            }
            outputSelectedSequence_ = outputHistory_[
                outputVisibleHistoryIndices_[selectedRow]].sequence;
        }
        outputGridRefreshPending_ = false;
    } else {
        auto selection = tree.dataGridSelection(outputGrid_);
        if (!selection) {
            return Tina::Core::failure(std::move(selection.error()));
        }
        if (selection->hasValue() &&
            selection->logicalRow < outputVisibleHistoryCount_) {
            outputSelectedSequence_ = outputHistory_[
                outputVisibleHistoryIndices_[selection->logicalRow]].sequence;
        } else {
            outputSelectedSequence_.reset();
        }
    }
    auto outputGridRect = tree.committedLayoutRect(outputGrid_);
    if (!outputGridRect) {
        return Tina::Core::failure(std::move(outputGridRect.error()));
    }
    outputGridRect_ = *outputGridRect;
    auto outputGridMetrics = tree.dataGridMetrics(outputGrid_);
    if (!outputGridMetrics) {
        return Tina::Core::failure(std::move(outputGridMetrics.error()));
    }
    outputGridMetrics_ = *outputGridMetrics;
    std::string outputSummary = outputHistoryCount_ == 0U
                                    ? "Output is clear"
                                    : std::to_string(outputHistoryCount_) +
                                          " messages | Showing " +
                                          std::to_string(outputVisibleHistoryCount_);
    if (auto status = tree.setText(outputSummary_, outputSummary);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(outputClearButton_, outputHistoryCount_ != 0U); !status) {
        return status;
    }
    const EditorOutputHistoryEntry* selectedEntry = selectedOutputEntry();
    if (auto status = tree.setEnabled(outputDetailsToggleButton_,
                                      selectedEntry != nullptr);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            outputDetailsToggleButton_,
            outputDetailsExpanded_ ? "Hide" : "Details");
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            outputLocateButton_,
            selectedEntry != nullptr && outputTargetAvailable(*selectedEntry));
        !status) {
        return status;
    }
    UI::UILayoutStyle detailsLayout = outputDetailsLayout_;
    detailsLayout.visibility = selectedEntry != nullptr && outputDetailsExpanded_
                                   ? UI::UIVisibility::Visible
                                   : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(outputDetails_, detailsLayout); !status) {
        return status;
    }
    if (selectedEntry != nullptr) {
        std::string details{outputSeverityLabel(selectedEntry->filter)};
        details += " | ";
        details += selectedEntry->context;
        details += " | #";
        details += std::to_string(selectedEntry->sequence);
        details += "\n";
        details += selectedEntry->message;
        if (selectedEntry->targetKind == EditorOutputTargetKind::Asset) {
            const auto targetText = selectedEntry->targetAssetId.canonicalText();
            details += "\nTarget AssetId: ";
            details.append(targetText.data(), targetText.size());
        } else if (selectedEntry->targetKind ==
                   EditorOutputTargetKind::SceneNode) {
            details += "\nTarget stable ID: ";
            details += std::to_string(selectedEntry->targetStableId);
        } else if (selectedEntry->targetKind ==
                   EditorOutputTargetKind::Document) {
            details += "\nTarget: document";
        }
        if (auto status = tree.setText(outputDetails_, details); !status) {
            return status;
        }
    }

    using ImportState = Tina::EditorApp::Detail::EditorSourceImportServiceState;
    using ImportPhase = Tina::EditorApp::Detail::EditorSourceImportPhase;
    std::string taskText = "Tasks: idle";
    if (sourceImportService_.state() == ImportState::Running) {
        switch (sourceImportService_.phase()) {
        case ImportPhase::Preparing:
            taskText = "Import: preparing";
            break;
        case ImportPhase::Copying:
            taskText = "Import: copying";
            break;
        case ImportPhase::Cooking:
            taskText = "Import: cooking";
            break;
        case ImportPhase::Idle:
        case ImportPhase::ReadyToCommit:
        case ImportPhase::Failed:
            taskText = "Import: running";
            break;
        }
    } else if (sourceImportService_.state() == ImportState::Ready) {
        taskText = "Import: ready to commit";
    } else if (sourceImportLastFailed_) {
        taskText = "Import: failed";
    } else if (sourceImportStartPending_ || retrySourceImportPending_ ||
               !pendingSourceImportPathsUtf8_.empty()) {
        taskText = "Import: queued";
    }
    if (auto status = tree.setText(statusTask_, taskText); !status) {
        return status;
    }
    // Whole FPS and one decimal of milliseconds. Full precision would change the
    // string on every frame and dirty the tree just to republish it.
    const auto formatWholeMetric = [](double value) {
        return std::to_string(static_cast<u64>(value + 0.5));
    };
    const auto formatMillisecondMetric = [](double value) {
        std::string text = std::to_string(value);
        const auto dot = text.find('.');
        if (dot != std::string::npos && dot + 2U < text.size()) {
            text.resize(dot + 2U);
        }
        return text;
    };
    const double frameSeconds = counters_.lastFrameSeconds;
    if (frameSeconds > 0.0 && std::isfinite(frameSeconds)) {
        statusFrameMetricElapsedSeconds_ += frameSeconds;
        if (!statusFrameMetricValid_ ||
            statusFrameMetricElapsedSeconds_ >= StatusFrameMetricIntervalSeconds) {
            statusFrameMetricElapsedSeconds_ = 0.0;
            statusFrameMetricFps_ = 1.0 / frameSeconds;
            statusFrameMetricMilliseconds_ = frameSeconds * 1000.0;
            statusFrameMetricValid_ = true;
        }
    }
    std::string activityText = taskText;
    if (statusFrameMetricValid_) {
        activityText += "  |  ";
        activityText += formatWholeMetric(statusFrameMetricFps_);
        activityText += " FPS  |  ";
        activityText += formatMillisecondMetric(statusFrameMetricMilliseconds_);
        activityText += " ms";
    }
    if (statusActivity_.hasValue()) {
        if (auto status = tree.setText(statusActivity_, activityText); !status) {
            return status;
        }
    }
    std::string catalogText;
    if (catalogRefreshPending_) {
        catalogText = "Catalog: updating";
    } else if (sourceImportCatalogCommitted_) {
        catalogText = "Catalog: staged";
    } else if (sourceImportLastFailed_) {
        catalogText = "Catalog: previous preserved";
    } else if (assetResources_.projectCatalogConfigured) {
        catalogText = "Catalog: current";
    } else {
        catalogText = "Catalog: not configured";
    }
    return tree.setText(statusCatalog_, catalogText);
}

auto EditorWorkspaceState::refreshInspectorGridLayout(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (inspectorTransformGridWorkspace_ == workspaceMode_) {
        return Tina::Core::success();
    }
    for (Tina::Core::usize index = 0;
         index < inspectorTransformValueGrids_.size(); ++index) {
        auto& grid = inspectorTransformValueGrids_[index];
        if (workspaceMode_ == WorkspaceMode::World3D) {
            grid.layout.gridContainer.columns = UI::UIGridTrackList::Of({
                UI::UIGridTrack::Fr(), UI::UIGridTrack::Fr(),
                UI::UIGridTrack::Fr(),
            });
        } else if (index == 1U) {
            grid.layout.gridContainer.columns =
                UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()});
        } else {
            grid.layout.gridContainer.columns = UI::UIGridTrackList::Of(
                {UI::UIGridTrack::Fr(), UI::UIGridTrack::Fr()});
        }
        if (auto status = tree.setLayoutStyle(grid.root, grid.layout); !status) {
            return status;
        }
    }
    inspectorTransformGridWorkspace_ = workspaceMode_;
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshWorkspacePanelsUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (pendingWorkspacePanelToggle_.has_value()) {
        const WorkspacePanelKind panel = *pendingWorkspacePanelToggle_;
        const bool isLeftDock = panel == WorkspacePanelKind::LeftDock;
        bool& visible = isLeftDock ? leftDockVisible_ : inspectorVisible_;
        float& visibleFraction = isLeftDock ? leftDockVisibleFraction_
                                            : inspectorVisibleFraction_;
        const UI::UINodeId splitView = isLeftDock ? leftDockSplitView_
                                                   : inspectorSplitView_;
        const UI::UINodeId dock = isLeftDock ? leftDock_ : inspectorDock_;
        const UI::UINodeId splitter = isLeftDock ? leftDockSplitter_
                                                  : inspectorSplitter_;
        UI::UILayoutStyle dockLayout = isLeftDock ? leftDockLayout_
                                                   : inspectorDockLayout_;
        UI::UILayoutStyle splitterLayout = isLeftDock
                                                ? leftDockSplitterLayout_
                                                : inspectorSplitterLayout_;
        const bool nextVisible = !visible;

        if (!nextVisible) {
            auto fraction = tree.splitViewFraction(splitView);
            if (!fraction) {
                return Tina::Core::failure(std::move(fraction.error()));
            }
            if (*fraction > 0.0F && *fraction < 1.0F) {
                visibleFraction = *fraction;
            }
        }

        dockLayout.visibility = nextVisible ? UI::UIVisibility::Visible
                                            : UI::UIVisibility::Collapsed;
        splitterLayout.visibility = nextVisible ? UI::UIVisibility::Visible
                                                : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(dock, dockLayout); !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(splitter, splitterLayout); !status) {
            return status;
        }
        const float collapsedFraction = isLeftDock ? 0.0F : 1.0F;
        if (auto status = tree.setSplitViewFraction(
                splitView, nextVisible ? visibleFraction : collapsedFraction);
            !status) {
            return status;
        }
        if (auto status = tree.setMenuItemChecked(
                viewPanelMenuItems_[isLeftDock ? 0U : 1U], nextVisible);
            !status) {
            return status;
        }

        if (isLeftDock) {
            leftDockLayout_ = dockLayout;
            leftDockSplitterLayout_ = splitterLayout;
        } else {
            inspectorDockLayout_ = dockLayout;
            inspectorSplitterLayout_ = splitterLayout;
        }
        visible = nextVisible;
        pendingWorkspacePanelToggle_.reset();
    }

    if (pendingBottomPanelOpen_.has_value() ||
        pendingBottomPanelToggle_.has_value()) {
        const bool explicitOpen = pendingBottomPanelOpen_.has_value();
        const BottomPanelKind requested = explicitOpen
                                              ? *pendingBottomPanelOpen_
                                              : *pendingBottomPanelToggle_;
        const BottomPanelKind next = explicitOpen
                                         ? requested
                                         : (requested == bottomPanel_
                                                ? BottomPanelKind::None
                                                : requested);
        const bool panelVisible = next != BottomPanelKind::None;
        const bool panelWasVisible = bottomPanel_ != BottomPanelKind::None;

        if (!panelVisible && panelWasVisible) {
            auto fraction = tree.splitViewFraction(bottomPanelSplitView_);
            if (!fraction) {
                return Tina::Core::failure(std::move(fraction.error()));
            }
            if (*fraction > 0.0F && *fraction < 1.0F) {
                bottomPanelVisibleFraction_ = *fraction;
            }
        }

        UI::UILayoutStyle splitterLayout = bottomPanelSplitterLayout_;
        splitterLayout.visibility = panelVisible ? UI::UIVisibility::Visible
                                                 : UI::UIVisibility::Collapsed;
        UI::UILayoutStyle hostLayout = bottomPanelHostLayout_;
        hostLayout.visibility = panelVisible ? UI::UIVisibility::Visible
                                             : UI::UIVisibility::Collapsed;
        UI::UILayoutStyle animationLayout = animationPanelLayout_;
        animationLayout.visibility = next == BottomPanelKind::Animation
                                         ? UI::UIVisibility::Visible
                                         : UI::UIVisibility::Collapsed;
        UI::UILayoutStyle outputLayout = outputPanelLayout_;
        outputLayout.visibility = next == BottomPanelKind::Output
                                      ? UI::UIVisibility::Visible
                                      : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                bottomPanelSplitter_, splitterLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(bottomPanelHost_, hostLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(animationPanel_, animationLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(outputPanel_, outputLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setSplitViewFraction(
                bottomPanelSplitView_,
                panelVisible ? bottomPanelVisibleFraction_ : 1.0F);
            !status) {
            return status;
        }
        constexpr std::array BottomPanels{
            BottomPanelKind::Animation,
            BottomPanelKind::Output,
        };
        for (u32 index = 0; index < bottomPanelButtons_.size(); ++index) {
            if (auto status = tree.setRadioButtonSelected(
                    bottomPanelButtons_[index], next == BottomPanels[index]);
                !status) {
                return status;
            }
        }
        if (auto status = tree.setMenuItemChecked(
                viewLayoutDebuggerMenuItem_, layoutDebuggerVisible_);
            !status) {
            return status;
        }
        bottomPanelSplitterLayout_ = splitterLayout;
        bottomPanelHostLayout_ = hostLayout;
        animationPanelLayout_ = animationLayout;
        outputPanelLayout_ = outputLayout;
        bottomPanel_ = next;
        pendingBottomPanelOpen_.reset();
        pendingBottomPanelToggle_.reset();
    }

    if (pendingLayoutDebuggerOpen_ || pendingLayoutDebuggerToggle_) {
        const bool nextVisible = pendingLayoutDebuggerOpen_
            ? true
            : !layoutDebuggerVisible_;
        UI::UILayoutStyle layoutDebugLayout = layoutDebugPanelLayout_;
        layoutDebugLayout.visibility = nextVisible
            ? UI::UIVisibility::Visible
            : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(layoutDebugPanel_, layoutDebugLayout);
            !status) {
            return status;
        }
        layoutDebugPanelLayout_ = layoutDebugLayout;
        if (auto status = tree.setRadioButtonSelected(
                layoutDebugStatusButton_, nextVisible); !status) {
            return status;
        }
        if (auto status = tree.setMenuItemChecked(
                viewLayoutDebuggerMenuItem_, nextVisible); !status) {
            return status;
        }
        layoutDebuggerVisible_ = nextVisible;
        if (!nextVisible) {
            layoutDebugPickArmed_ = false;
            pendingLayoutDebugPickPoint_.reset();
            layoutDebugRevealSelectionPending_ = false;
            layoutDebugSelectedNode_ = {};
            layoutDebugSelectedEntry_.reset();
            layoutDebugSelectionKey_ = UI::InvalidUITreeViewItemKey;
            layoutDebugPickFeedback_.clear();
            if (auto status = tree.clearTreeViewSelection(layoutDebugTree_);
                !status) {
                return status;
            }
        }
        layoutDebugDetailsRefreshPending_ = true;
        pendingLayoutDebuggerOpen_ = false;
        pendingLayoutDebuggerToggle_ = false;
    }

    if (auto status = refreshInspectorGridLayout(tree); !status) {
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshMainMenuUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
    const bool editing = authoringEnabled();
    const bool tileMapContext = tileMapEditingContext();
    for (u32 index = 0; index < workspaceModeButtons_.size(); ++index) {
        if (auto status = tree.setRadioButtonSelected(
                workspaceModeButtons_[index], index == (world2D ? 0U : 1U));
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(workspaceModeButtons_[index], editing);
            !status) {
            return status;
        }
    }
    if (auto status = tree.setMenuItemChecked(
            viewWorkspaceMenuItems_[world2D ? 0U : 1U], true);
        !status) {
        return status;
    }
    for (const UI::UINodeId item : viewWorkspaceMenuItems_) {
        if (auto status = tree.setEnabled(item, editing); !status) {
            return status;
        }
    }
    if (auto status = tree.setMenuItemChecked(
            viewPanelMenuItems_[0], leftDockVisible_);
        !status) {
        return status;
    }
    if (auto status = tree.setMenuItemChecked(
            viewPanelMenuItems_[1], inspectorVisible_);
        !status) {
        return status;
    }
    if (auto status = tree.setMenuItemChecked(
            viewLayoutDebuggerMenuItem_,
            layoutDebuggerVisible_);
        !status) {
        return status;
    }

    for (u32 index = 0; index < viewportContextButtons_.size(); ++index) {
        viewportContextButtonLayouts_[index].visibility =
            world2D ? UI::UIVisibility::Visible
                    : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                viewportContextButtons_[index],
                viewportContextButtonLayouts_[index]);
            !status) {
            return status;
        }
        if (auto status = tree.setRadioButtonSelected(
                viewportContextButtons_[index],
                index == (tileMapContext ? 1U : 0U));
            !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(
            viewportContextButtons_[0], editing && world2D);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            viewportContextButtons_[1],
            editing && world2D &&
                documentTabs_.find(tileMapDocumentOwnerKey_).has_value());
        !status) {
        return status;
    }

    const auto mirrorEnabled = [&tree](
                                   UI::UINodeId source,
                                   UI::UINodeId target) -> Tina::Core::Status {
        auto enabled = tree.isEnabled(source);
        if (!enabled) {
            return Tina::Core::failure(std::move(enabled.error()));
        }
        return tree.setEnabled(target, *enabled);
    };
    const std::array mirroredItems{
        std::pair{importSourceButton_, fileImportSourceMenuItem_},
        std::pair{saveButton_, fileSaveMenuItem_},
        std::pair{saveAsButton_, fileSaveAsMenuItem_},
        std::pair{closeDocumentButton_, fileCloseDocumentMenuItem_},
        std::pair{undoButton_, editUndoMenuItem_},
        std::pair{redoButton_, editRedoMenuItem_},
        std::pair{duplicateEntityButton_, editDuplicateMenuItem_},
        std::pair{deleteEntityButton_, editDeleteMenuItem_},
        std::pair{frameAllButton_, viewFrameAllMenuItem_},
        std::pair{focusEntityButton_, viewFocusSelectionMenuItem_},
    };
    for (const auto& [source, target] : mirroredItems) {
        if (auto status = mirrorEnabled(source, target); !status) {
            return status;
        }
    }
    return tree.setEnabled(helpAboutMenuItem_, true);
}

auto EditorWorkspaceState::refreshAuthoringUi(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    publishWorkspaceSessionCounters();
    const bool dirty = counters_.documentDirty;
    const bool pathConfigured = counters_.documentPathConfigured;
    const bool selectionEditable = authoringEnabled() && !assetInspectorActive_ &&
                                   stableEntityIdForHierarchyItem(selectionKey_) != 0U &&
                                   !tileMapEditingContext();
    inspectorDirtyBadgeLayout_.visibility =
        dirty && selectionKey_ != UI::InvalidUITreeViewItemKey &&
                !assetInspectorActive_
            ? UI::UIVisibility::Visible
            : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            inspectorDirtyBadge_, inspectorDirtyBadgeLayout_);
        !status) {
        return status;
    }

    if (auto status = refreshWorkspaceChrome(tree); !status) {
        return status;
    }
    if (auto status = refreshProjectAssetUi(tree); !status) {
        return status;
    }
    if (auto status = refreshDocumentTabsUi(tree); !status) {
        return status;
    }
    if (auto status = refreshViewportToolUi(tree); !status) {
        return status;
    }
    if (auto status = refreshAnimationTimelineUi(tree); !status) {
        return status;
    }
    if (auto status = publishInspector(tree, selectionKey_); !status) {
        return status;
    }
    if (auto status = refreshNodePropertySectionsUi(tree); !status) {
        return status;
    }
    std::string_view documentKindLabel = "No document";
    std::string_view documentItemLabel = "items";
    if (const auto* activeTab = documentTabs_.activeTab(); activeTab != nullptr) {
        switch (activeTab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            documentKindLabel = "World2D v1";
            documentItemLabel = "entities";
            break;
        case Tina::Editor::EditorDocumentKind::World3D:
        documentKindLabel = "Prefab v4";
            documentItemLabel = "nodes";
            break;
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            documentKindLabel = "TileMap v3/v1";
            documentItemLabel = "layers";
            break;
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            documentKindLabel = "SpriteAnimationClip v2";
            documentItemLabel = "frames";
            break;
        case Tina::Editor::EditorDocumentKind::AssetInspector:
            documentKindLabel = "Asset Inspector";
            documentItemLabel = "items";
            break;
        }
    }
    std::string statusDocument{documentKindLabel};
    statusDocument += "  |  ";
    statusDocument += std::to_string(activeDocumentItemCount());
    statusDocument += ' ';
    statusDocument += documentItemLabel;
    statusDocument += "  |  Revision ";
    statusDocument += std::to_string(activeDocumentRevision());
    statusDocument += pathConfigured ? (dirty ? "  |  Modified" : "  |  Saved") : "  |  Unsaved";
    if (auto status = tree.setText(statusDocument_, statusDocument); !status) {
        return status;
    }
    if (auto status = publishRuntimePreviewStatus(tree); !status) {
        return status;
    }
    std::string statusSelection = "Selected: ";
    if (assetInspectorActive_) {
        const auto* asset = inspectedProjectAsset();
        statusSelection += asset != nullptr ? asset->displayName : "Unavailable Catalog asset";
    } else if (selectionKey_ == UI::InvalidUITreeViewItemKey) {
        statusSelection += "None";
    } else {
        statusSelection += hierarchyDisplayLabel(selectionKey_);
        if (viewportSelectedEntityCount_ > 1U) {
            statusSelection += "  |  ";
            statusSelection += std::to_string(viewportSelectedEntityCount_);
            statusSelection += " selected  |  Group pivot";
        }
    }
    if (auto status = tree.setText(statusSelection_, statusSelection); !status) {
        return status;
    }
    auto tileMapRoot = Tina::AssetFormat::parseTileMapPayload(
        tileMapDocument_.rootPayloadBytes());
    if (!tileMapRoot) {
        return Tina::Core::failure(std::move(tileMapRoot.error()));
    }
    std::string tileMapStatus = std::to_string(tileMapRoot->widthCells);
    tileMapStatus += " x ";
    tileMapStatus += std::to_string(tileMapRoot->heightCells);
    tileMapStatus += " | Layers ";
    tileMapStatus += std::to_string(tileMapDocument_.layerCount());
    tileMapStatus += " | Chunks ";
    tileMapStatus += std::to_string(tileMapDocument_.chunkCount());
    tileMapStatus += " | Cells ";
    tileMapStatus += std::to_string(tileMapDocument_.nonEmptyCellCount());
    tileMapStatus += " | Rev ";
    tileMapStatus += std::to_string(tileMapDocument_.revision());
    tileMapStatus += " | Nav ";
    tileMapStatus += navigationDocument_.isDirtyFor(tileMapDocument_.revision())
                         ? "Dirty"
                         : "Current";
    if (auto status = tree.setText(tileMapStatus_, tileMapStatus); !status) {
        return status;
    }
    const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
    const bool tileMapContextVisible = tileMapEditingContext();
    tilePaletteGridLayout_.visibility = tileMapContextVisible
        ? UI::UIVisibility::Visible : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(tilePaletteGrid_, tilePaletteGridLayout_); !status) {
        return status;
    }
    const bool entityContextVisible = !assetInspectorActive_ &&
                                      !tileMapContextVisible &&
                                      selectedStableId != 0U;
    const auto publishContextVisibility =
        [&](InspectorLayoutNodeUi& node, bool visible) -> Tina::Core::Status {
        node.layout.visibility = visible ? UI::UIVisibility::Visible
                                         : UI::UIVisibility::Collapsed;
        return tree.setLayoutStyle(node.root, node.layout);
    };
    if (auto status = publishContextVisibility(
            inspectorHierarchyHeaderUi_, entityContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorHierarchyParentRowUi_, entityContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorHierarchyApplyParentUi_, entityContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorTileMapHeaderUi_, tileMapContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorTileMapStatusUi_, tileMapContextVisible);
        !status) {
        return status;
    }
    for (auto& row : inspectorTileMapActionRows_) {
        if (auto status = publishContextVisibility(row, tileMapContextVisible);
            !status) {
            return status;
        }
    }
    const bool transformVisible = entityContextVisible;
    const UI::UIVisibility transformVisibility =
        transformVisible ? UI::UIVisibility::Visible
                         : UI::UIVisibility::Collapsed;
    inspectorTransformHeaderLayout_.visibility = transformVisibility;
    if (auto status = tree.setLayoutStyle(
            inspectorTransformHeader_, inspectorTransformHeaderLayout_);
        !status) {
        return status;
    }
    inspectorTransformFieldsLayout_.visibility = transformVisibility;
    if (auto status = tree.setLayoutStyle(
            inspectorTransformFields_, inspectorTransformFieldsLayout_);
        !status) {
        return status;
    }
    if (inspectorTransformErrorStableId_ != 0U &&
        inspectorTransformErrorStableId_ != selectedStableId) {
        inspectorTransformErrorUtf8_.clear();
        inspectorTransformErrorStableId_ = 0U;
    }
    const bool transformErrorVisible = transformVisible &&
        inspectorTransformErrorStableId_ == selectedStableId &&
        !inspectorTransformErrorUtf8_.empty();
    inspectorTransformErrorLayout_.visibility = transformErrorVisible
                                                    ? UI::UIVisibility::Visible
                                                    : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            inspectorTransformError_, inspectorTransformErrorLayout_);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            inspectorTransformError_,
            transformErrorVisible ? std::string_view{inspectorTransformErrorUtf8_}
                                  : std::string_view{});
        !status) {
        return status;
    }
    for (Tina::Core::usize index = 0;
         index < inspectorTransformAxisFields_.size(); ++index) {
        const auto field = static_cast<InspectorTransformField>(index);
        auto& axis = inspectorTransformAxisFields_[index];
        const bool fieldVisible =
            transformVisible &&
            (workspaceMode_ == WorkspaceMode::World3D ||
             !inspectorTransformFieldRequires3D(field));
        axis.layout.visibility = fieldVisible
                                     ? UI::UIVisibility::Visible
                                     : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(axis.root, axis.layout);
            !status) {
            return status;
        }
    }
    const std::array transformInputs{
        inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
        inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
        inspectorScaleX_, inspectorScaleY_, inspectorScaleZ_,
    };
    for (Tina::Core::usize index = 0;
         index < transformInputs.size(); ++index) {
        const auto field = static_cast<InspectorTransformField>(index);
        const bool enabled = selectionEditable &&
            (workspaceMode_ == WorkspaceMode::World3D ||
             !inspectorTransformFieldRequires3D(field));
        if (auto status = tree.setEnabled(transformInputs[index], enabled);
            !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(inspectorParentStableId_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(applyTransformButton_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(resetTransformButton_, selectionEditable);
        !status) {
        return status;
    }
    const EditorHierarchyRow* selectedRow = hierarchyRow(selectedStableId);
    const bool sceneEditable = authoringEnabled() && sceneDocumentActive() &&
                               !assetInspectorActive_;
    const bool sceneAuthoringAvailable = authoringEnabled() && sceneDocumentActive();
    const bool sceneItemSelected = selectedStableId != 0U && selectedRow != nullptr;
    if (auto status = tree.setEnabled(addEntityButton_, sceneAuthoringAvailable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            duplicateEntityButton_, sceneEditable && sceneItemSelected);
        !status) {
        return status;
    }
    const bool canDeleteSelection = sceneEditable && sceneItemSelected &&
        (workspaceMode_ == WorkspaceMode::World2D || document3D_.nodeCount() > 1U);
    if (auto status = tree.setEnabled(deleteEntityButton_, canDeleteSelection); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            reparentEntityButton_, sceneEditable && sceneItemSelected);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            focusEntityButton_, sceneDocumentActive() && sceneItemSelected &&
                                    counters_.runtimePreviewValid);
        !status) {
        return status;
    }
    const EditorHierarchyRow* contextRow = hierarchyRow(hierarchyContextStableId_);
    const bool contextItemAvailable = sceneAuthoringAvailable &&
        hierarchyContextStableId_ != 0U && contextRow != nullptr;
    u32 contextPreviousSibling = 0U;
    u32 contextNextSibling = 0U;
    bool contextSourceSeen = false;
    if (contextItemAvailable) {
        for (const EditorHierarchyRow& row : hierarchyRows_) {
            if (row.stableId == hierarchyContextStableId_) {
                contextSourceSeen = true;
                continue;
            }
            if (row.stableId == 0U || row.parentStableId != contextRow->parentStableId) {
                continue;
            }
            if (!contextSourceSeen) {
                contextPreviousSibling = row.stableId;
            } else if (contextNextSibling == 0U) {
                contextNextSibling = row.stableId;
            }
        }
    }
    if (auto status = tree.setEnabled(hierarchyContextMenu_, sceneAuthoringAvailable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            hierarchyContextRenameItem_, contextItemAvailable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            hierarchyContextMoveUpItem_, contextItemAvailable &&
                contextPreviousSibling != 0U); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            hierarchyContextMoveDownItem_, contextItemAvailable &&
                contextNextSibling != 0U); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            hierarchyContextMoveToRootItem_, contextItemAvailable &&
                contextRow->parentStableId != 0U); !status) {
        return status;
    }
    const bool contextCanDelete = contextItemAvailable &&
        (workspaceMode_ == WorkspaceMode::World2D || document3D_.nodeCount() > 1U);
    if (auto status = tree.setEnabled(
            hierarchyContextDeleteItem_, contextCanDelete); !status) {
        return status;
    }
    const Tina::Editor::ProjectAssetDescriptor* contextAsset =
        projectAssets_.inspectorSnapshot(projectAssetContextAssetId_);
    const bool projectAssetContextAvailable =
        contextAsset != nullptr && projectAssetContextAssetId_;
    const bool contextSourceImportIdle =
        sourceImportService_.state() ==
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
    const bool mappedSourceAsset =
        projectAssetContextAvailable &&
        sourceImportUnitForAsset(projectAssetContextAssetId_) != nullptr;
    const bool projectAssetMutationAvailable =
        projectAssetContextAvailable && activeProjectWorkspace_.has_value() &&
        contextSourceImportIdle && !pendingProjectSwitch_.has_value() &&
        pendingSourceImportPathsUtf8_.empty() && !sourceImportStartPending_ &&
        !retrySourceImportPending_;
    if (auto status = tree.setEnabled(
            projectAssetContextMenu_, projectAssetContextAvailable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextOpenItem_, projectAssetContextAvailable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextRenameItem_, projectAssetMutationAvailable &&
                projectAssetSupportsSourceRename(projectAssetContextAssetId_)); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextNewFolderItem_,
            activeProjectWorkspace_.has_value() && contextSourceImportIdle &&
                !pendingProjectSwitch_.has_value()); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextReimportItem_,
            projectAssetMutationAvailable && mappedSourceAsset); !status) {
        return status;
    }
    // No platform clipboard or shell reveal adapter is exposed by Tina yet.
    // Keep these commands visible for discoverability but fail closed.
    if (auto status = tree.setEnabled(
            projectAssetContextLocateSourceItem_, false); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextCopyAssetIdItem_, false); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextCopySourcePathItem_, false); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextRevealDependenciesItem_, projectAssetContextAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            projectAssetContextRemoveItem_, projectAssetMutationAvailable && mappedSourceAsset);
        !status) {
        return status;
    }
    const bool tileMapControlsEnabled = authoringEnabled() && tileMapEditingContext();
    const bool tilePaletteAvailable = tileMapControlsEnabled && tilePaletteTileCount_ != 0U;
    if (auto status = tree.setEnabled(paintTileButton_, tilePaletteAvailable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(eraseTileButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(toggleTileLayerButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(addTileLayerButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(addObjectLayerButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(cookTileMapButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(bakeNavigationButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(generateTileMapGameplayButton_, tileMapControlsEnabled);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(undoButton_, activeCanUndo()); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(redoButton_, activeCanRedo()); !status) {
        return status;
    }
    const bool documentCanSave = activeDocumentSession() != nullptr;
    const bool temporaryProjectCanSave =
        temporaryProjectActive() && !pendingProjectSwitch_.has_value() &&
        pendingSourceImportPathsUtf8_.empty() &&
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
    if (auto status = tree.setEnabled(
            saveAsButton_, documentCanSave || temporaryProjectCanSave);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            saveButton_, temporaryProjectCanSave ||
                             (documentCanSave && pathConfigured && dirty));
        !status) {
        return status;
    }
    if (auto status = refreshPlaySessionUi(tree); !status) {
        return status;
    }
    return refreshMainMenuUi(tree);
}

auto EditorWorkspaceState::refreshPlaySessionUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const Tina::Editor::EditorPlayState state = playSession_.has_value()
        ? playSession_->snapshot().state
        : Tina::Editor::EditorPlayState::Editing;
    const bool editing = state == Tina::Editor::EditorPlayState::Editing;
    const bool playing = state == Tina::Editor::EditorPlayState::Playing;
    const bool paused = state == Tina::Editor::EditorPlayState::Paused;

    if (auto status = tree.setEnabled(
            playButton_,
            (editing && !pendingDirtyCloseKey_.has_value() &&
             sceneDocumentActive() && playStartReady()) || paused);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(pauseButton_, playing); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(stepButton_, paused); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(stopButton_, playing || paused); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(projectAssetList_, editing); !status) {
        return status;
    }
    const bool sourceImportSelectionEnabled =
        editing && activeProjectWorkspace_.has_value() &&
        !pendingProjectSwitch_.has_value() && !catalogRefreshPending_ &&
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
    if (auto status = tree.setEnabled(sourceImportGrid_,
                                      sourceImportSelectionEnabled);
        !status) {
        return status;
    }
    if (editing) {
        return Tina::Core::success();
    }

    const std::array lockedButtons{
        undoButton_, redoButton_, saveButton_,
        saveAsButton_, addEntityButton_, duplicateEntityButton_, deleteEntityButton_,
        reparentEntityButton_, inspectorParentStableId_,
        translateToolButtons_[0], rotateToolButtons_[0], scaleToolButtons_[0],
        orientationButton_, snapButton_, tilePaintToolButton_, tileEraseToolButton_,
        paintTileButton_, eraseTileButton_, toggleTileLayerButton_, addTileLayerButton_,
        addObjectLayerButton_, cookTileMapButton_, generateTileMapGameplayButton_,
        bakeNavigationButton_,
        animationModeButton_, animationPlaybackButtons_.play.button,
        animationPlaybackButtons_.pause.button, animationCookButton_,
        animationPreviousButton_, animationNextButton_, animationAddButton_,
        animationDuplicateButton_, animationDeleteButton_, animationMoveLeftButton_,
        animationMoveRightButton_, animationCycleSpriteButton_,
        animationDurationDecreaseButton_, animationDurationIncreaseButton_,
        animationEventPreviousButton_, animationEventNextButton_,
        animationEventAddButton_, animationEventApplyButton_,
        animationEventRemoveButton_, openProjectAssetButton_,
        refreshProjectCatalogButton_,
        importSourceButton_, removeSourceImportButton_, closeDocumentButton_,
    };
    for (const UI::UINodeId button : lockedButtons) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(projectAssetTypeDropdown_, false); !status) {
        return status;
    }
    for (const UI::UINodeId button : projectAssetViewButtons_) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : documentTabButtons_) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : animationFrameButtons_) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(animationEventTag_, false); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationEventOffset_, false); !status) {
        return status;
    }
    for (const NodePropertySectionUi& section : nodePropertySections_) {
        const std::array sectionControls{
            section.activeSwitch, section.resourceAssignButton, section.applyButton};
        for (const UI::UINodeId control : sectionControls) {
            if (!control.hasValue()) {
                continue;
            }
            if (auto status = tree.setEnabled(control, false); !status) {
                return status;
            }
        }
        for (Tina::Core::usize index = 0; index < section.fieldCount; ++index) {
            if (auto status = tree.setEnabled(section.fields[index], false);
                !status) {
                return status;
            }
        }
    }
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
