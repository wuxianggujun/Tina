#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::refreshDocumentTabsUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (auto status = synchronizeActiveTabDirty(); !status) {
        return status;
    }
    auto productTheme = tree.productTheme();
    if (!productTheme) {
        return Tina::Core::failure(std::move(productTheme.error()));
    }
    u32 selectedIndex = static_cast<u32>(documentTabs_.activeIndex());
    bool hasExternalDocumentTab = false;
    if (const auto* active = documentTabs_.activeTab();
        active != nullptr && isWorkspaceContextDocumentTab(*active)) {
        selectedIndex = 0U;
    }
    for (u32 index = 0; index < documentTabButtons_.size(); ++index) {
        const auto* tab = documentTabs_.tab(index);
        const bool visible = tab != nullptr && !isWorkspaceContextDocumentTab(*tab);
        hasExternalDocumentTab = hasExternalDocumentTab || visible;
        std::string title = tab != nullptr ? tab->title : std::string{};
        if (tab != nullptr && tab->dirty) {
            title += " *";
        }
        if (auto status = tree.setText(documentTabButtons_[index], title); !status) {
            return status;
        }
        if (auto status = tree.setRadioButtonSelected(
                documentTabButtons_[index], visible && index == selectedIndex);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(documentTabButtons_[index], visible); !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(
                documentTabButtons_[index],
                editorDocumentTabLayout(
                    *productTheme, visible ? UI::UIVisibility::Visible
                                           : UI::UIVisibility::Collapsed));
            !status) {
            return status;
        }
    }
    documentTabsBarLayout_.visibility = hasExternalDocumentTab
                                            ? UI::UIVisibility::Visible
                                            : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            documentTabsBar_, documentTabsBarLayout_);
        !status) {
        return status;
    }
    const auto* active = documentTabs_.activeTab();
    const bool closeVisible = active != nullptr && !active->pinned &&
                              !isWorkspaceContextDocumentTab(*active);
    closeDocumentButtonLayout_.visibility = closeVisible
                                                ? UI::UIVisibility::Visible
                                                : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            closeDocumentButtonRoot_, closeDocumentButtonLayout_);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(closeDocumentButton_, closeVisible);
        !status) {
        return status;
    }
    counters_.documentTabCount = documentTabs_.tabCount();
    return Tina::Core::success();
}

auto EditorWorkspaceState::findDocumentSession(Tina::Editor::EditorDocumentKey key) noexcept -> WorkspaceSessionState*{
    return documentSessions_.find(key);
}

auto EditorWorkspaceState::findDocumentSession(Tina::Editor::EditorDocumentKey key) const noexcept -> const WorkspaceSessionState*{
    return documentSessions_.find(key);
}

auto EditorWorkspaceState::installDocumentSession(WorkspaceSessionState session) -> Tina::Core::Status{
    return documentSessions_.install(std::move(session));
}

auto EditorWorkspaceState::discardDocumentSession(Tina::Editor::EditorDocumentKey key) noexcept -> void{
    documentSessions_.discard(key);
}

auto EditorWorkspaceState::initializePinnedDocumentSessions() -> Tina::Core::Status{
    if (findDocumentSession(tileMapDocumentOwnerKey_) != nullptr &&
        findDocumentSession(spriteAnimationDocumentOwnerKey_) != nullptr) {
        return Tina::Core::success();
    }
    auto tileMapBaseline = captureSavedBaseline(tileMapDocument_);
    if (!tileMapBaseline) {
        return Tina::Core::failure(std::move(tileMapBaseline.error()));
    }
    auto animationBaseline = captureSavedBaseline(spriteAnimationDocument_);
    if (!animationBaseline) {
        return Tina::Core::failure(std::move(animationBaseline.error()));
    }
    WorkspaceSessionState tileMapSession{
        .key = tileMapDocumentOwnerKey_,
        .savedBaseline = std::move(*tileMapBaseline),
    };
    WorkspaceSessionState animationSession{
        .key = spriteAnimationDocumentOwnerKey_,
        .savedBaseline = std::move(*animationBaseline),
    };
    bool installedTileMapSession = false;
    if (findDocumentSession(tileMapDocumentOwnerKey_) == nullptr) {
        if (auto status = installDocumentSession(std::move(tileMapSession)); !status) {
            return status;
        }
        installedTileMapSession = true;
    }
    if (findDocumentSession(spriteAnimationDocumentOwnerKey_) == nullptr) {
        if (auto status = installDocumentSession(std::move(animationSession)); !status) {
            if (installedTileMapSession) {
                discardDocumentSession(tileMapDocumentOwnerKey_);
            }
            return status;
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::makeProjectAssetSession(Tina::Editor::EditorDocumentKey key,
                        const TabAuthoringDocument& document,
                        Tina::AssetFormat::TargetPlatform targetPlatform) const -> Tina::Core::Result<WorkspaceSessionState>{
    auto baseline = std::visit(
        [](const auto& activeDocument) {
            return captureSavedBaseline(activeDocument);
        },
        document);
    if (!baseline) {
        return Tina::Core::failure(std::move(baseline.error()));
    }
    return WorkspaceSessionState{
        .key = key,
        .savedBaseline = std::move(*baseline),
        .targetPlatform = targetPlatform,
        .loadedFromPath = true,
    };
}

auto EditorWorkspaceState::activeDocumentSession() noexcept -> WorkspaceSessionState*{
    const auto* tab = documentTabs_.activeTab();
    return tab != nullptr ? findDocumentSession(tab->key) : nullptr;
}

auto EditorWorkspaceState::activeDocumentSession() const noexcept -> const WorkspaceSessionState*{
    const auto* tab = documentTabs_.activeTab();
    return tab != nullptr ? findDocumentSession(tab->key) : nullptr;
}

auto EditorWorkspaceState::documentPathOwnedByOtherSession(
    std::string_view path,
    Tina::Editor::EditorDocumentKey activeKey) const noexcept -> bool{
    return documentSessions_.pathOwnedByOtherSession(path, activeKey);
}

auto EditorWorkspaceState::activeAuthoringDocumentOwner(Tina::Editor::EditorDocumentKind kind) noexcept -> Tina::Editor::EditorDocumentKey*{
    switch (kind) {
    case Tina::Editor::EditorDocumentKind::World3D:
        return &world3DDocumentOwnerKey_;
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return &tileMapDocumentOwnerKey_;
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return &spriteAnimationDocumentOwnerKey_;
    case Tina::Editor::EditorDocumentKind::World2D:
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return nullptr;
    }
}

auto EditorWorkspaceState::findSuspendedAuthoringDocument(Tina::Editor::EditorDocumentKey key) const noexcept -> const SuspendedTabAuthoringDocument*{
    return documentSessions_.findSuspended(key);
}

auto EditorWorkspaceState::findSuspendedAuthoringDocument(Tina::Editor::EditorDocumentKey key) noexcept -> SuspendedTabAuthoringDocument*{
    return documentSessions_.findSuspended(key);
}

auto EditorWorkspaceState::switchActiveAuthoringDocument(Tina::Editor::EditorDocumentKey key) noexcept -> Tina::Core::Status{
    auto* owner = activeAuthoringDocumentOwner(key.kind);
    if (owner == nullptr || *owner == key) {
        return Tina::Core::success();
    }
    auto* suspended = findSuspendedAuthoringDocument(key);
    if (suspended == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabNotFound,
            "Catalog authoring tab has no suspended document state");
    }
    switch (key.kind) {
    case Tina::Editor::EditorDocumentKind::World3D: {
        auto* target = std::get_if<Tina::Editor::World3DAuthoringDocument>(
            &suspended->document);
        if (target == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "World3D tab state has the wrong document kind");
        }
        std::swap(document3D_, *target);
        break;
    }
    case Tina::Editor::EditorDocumentKind::TileMap2D: {
        auto* target = std::get_if<Tina::Editor::TileMapAuthoringDocument>(
            &suspended->document);
        if (target == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "TileMap tab state has the wrong document kind");
        }
        std::swap(tileMapDocument_, *target);
        break;
    }
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D: {
        auto* target = std::get_if<Tina::Editor::SpriteAnimationAuthoringDocument>(
            &suspended->document);
        if (target == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "SpriteAnimation tab state has the wrong document kind");
        }
        std::swap(spriteAnimationDocument_, *target);
        break;
    }
    case Tina::Editor::EditorDocumentKind::World2D:
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return Tina::Core::success();
    }
    std::swap(*owner, suspended->key);
    ++counters_.tabOwnedDocumentSwaps;
    previewAssetBindingsRefreshPending_ = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::installNewAuthoringDocument(Tina::Editor::EditorDocumentKey key,
                            TabAuthoringDocument document) -> Tina::Core::Status{
    auto* owner = activeAuthoringDocumentOwner(key.kind);
    if (owner == nullptr || *owner == key) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Catalog document kind cannot own an authoring tab");
    }
    if (auto status = documentSessions_.storeSuspended(SuspendedTabAuthoringDocument{
            .key = key,
            .document = std::move(document),
        }); !status) {
        return status;
    }
    if (auto status = switchActiveAuthoringDocument(key); !status) {
        documentSessions_.discardSuspended(key);
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::discardSuspendedAuthoringDocument(
    Tina::Editor::EditorDocumentKey key) noexcept -> void{
    documentSessions_.discardSuspended(key);
}

auto EditorWorkspaceState::loadProjectAssetDocument(
    const Tina::Editor::ProjectAssetDescriptor& asset) -> Tina::Core::Result<LoadedProjectAssetDocument>{
    if (!assetResources_.system.has_value()) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Tina Editor AssetSystem is unavailable");
    }
    auto loaded = assetResources_.system->loadOne(asset.assetId);
    if (!loaded) {
        return Tina::Core::failure(std::move(loaded.error()));
    }
    const Tina::Asset::CookedAssetFile* file =
        assetResources_.system->tryGet(*loaded);
    if (file == nullptr) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Opened Catalog asset has no CPU payload");
    }
    switch (Tina::Editor::projectAssetOpenKind(asset.assetKind)) {
    case Tina::Editor::ProjectAssetOpenKind::World3D:
    {
        auto candidate = Tina::Editor::World3DAuthoringDocument::Create(
            document3D_.config());
        if (!candidate) {
            return Tina::Core::failure(std::move(candidate.error()));
        }
        if (auto status = candidate->loadPayload(file->payload()); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        TabAuthoringDocument state{
            std::in_place_type<Tina::Editor::World3DAuthoringDocument>,
            std::move(*candidate)};
        return LoadedProjectAssetDocument{
            .document = std::optional<TabAuthoringDocument>{std::move(state)},
            .targetPlatform = file->header().targetPlatform,
        };
    }
    case Tina::Editor::ProjectAssetOpenKind::SpriteAnimation2D:
    {
        auto seed = spriteAnimationDocument_.snapshot();
        if (!seed) {
            return Tina::Core::failure(std::move(seed.error()));
        }
        seed->clipId = asset.assetId;
        auto candidate = Tina::Editor::SpriteAnimationAuthoringDocument::Create(
            *seed, spriteAnimationDocument_.config());
        if (!candidate) {
            return Tina::Core::failure(std::move(candidate.error()));
        }
        if (auto status = candidate->loadCookedAsset(file->bytes()); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        TabAuthoringDocument state{
            std::in_place_type<Tina::Editor::SpriteAnimationAuthoringDocument>,
            std::move(*candidate)};
        return LoadedProjectAssetDocument{
            .document = std::optional<TabAuthoringDocument>{std::move(state)},
            .targetPlatform = file->header().targetPlatform,
        };
    }
    case Tina::Editor::ProjectAssetOpenKind::TileMap2D: {
        Tina::Core::AssetId tilesetId{};
        std::vector<Tina::Core::AssetId> chunkIds;
        std::vector<Tina::Core::AssetId> dependencies;
        try {
            chunkIds.reserve(file->header().dependencyCount);
            dependencies.reserve(file->header().dependencyCount);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "TileMap document dependency allocation failed");
        }
        for (u32 index = 0; index < file->header().dependencyCount; ++index) {
            const auto dependency = file->dependency(index);
            if (!dependency) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "TileMap document dependency disappeared");
            }
            if (dependency->expectedKind == Tina::AssetFormat::AssetKind::Tileset) {
                tilesetId = dependency->assetId;
                dependencies.push_back(dependency->assetId);
            } else if (dependency->expectedKind ==
                       Tina::AssetFormat::AssetKind::TileMapChunk) {
                chunkIds.push_back(dependency->assetId);
                dependencies.push_back(dependency->assetId);
            }
        }
        if (!dependencies.empty()) {
            auto dependencyHandles = assetResources_.system->load(dependencies);
            if (!dependencyHandles) {
                return Tina::Core::failure(std::move(dependencyHandles.error()));
            }
        }
        std::vector<Tina::Editor::TileMapAuthoringChunkSource> chunks;
        try {
            chunks.reserve(chunkIds.size());
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "TileMap document chunk allocation failed");
        }
        for (const Tina::Core::AssetId chunkId : chunkIds) {
            const auto handle = assetResources_.system->find(chunkId);
            const auto* chunkFile = handle ? assetResources_.system->tryGet(*handle)
                                           : nullptr;
            if (chunkFile == nullptr) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "TileMap Catalog chunk is not resident");
            }
            chunks.push_back({.assetId = chunkId,
                              .payloadBytes = chunkFile->payload()});
        }
        if (!tilesetId) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "TileMap Catalog asset has no Tileset dependency");
        }
        auto seed = tileMapDocument_.snapshot();
        if (!seed) {
            return Tina::Core::failure(std::move(seed.error()));
        }
        seed->tileMapId = asset.assetId;
        seed->tilesetId = tilesetId;
        auto candidate = Tina::Editor::TileMapAuthoringDocument::Create(
            *seed, tileMapDocument_.config());
        if (!candidate) {
            return Tina::Core::failure(std::move(candidate.error()));
        }
        if (auto status = candidate->loadPayloadFamily(
                asset.assetId, tilesetId, file->payload(), chunks);
            !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        TabAuthoringDocument state{
            std::in_place_type<Tina::Editor::TileMapAuthoringDocument>,
            std::move(*candidate)};
        return LoadedProjectAssetDocument{
            .document = std::optional<TabAuthoringDocument>{std::move(state)},
            .targetPlatform = file->header().targetPlatform,
        };
    }
    case Tina::Editor::ProjectAssetOpenKind::AssetInspector:
    default:
        return LoadedProjectAssetDocument{
            .targetPlatform = file->header().targetPlatform,
        };
    }
}

auto EditorWorkspaceState::openSelectedProjectAsset(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (auto status = synchronizeActiveTabDirty(); !status) {
        return status;
    }
    const auto* asset = projectAssets_.selectedItem();
    if (asset == nullptr) {
        return Tina::Core::failure(Tina::Editor::EditorErrorCode::ProjectAssetNotFound,
                                   "Project Asset Browser has no selected asset");
    }
    const Tina::Editor::EditorDocumentKind documentKind =
        Tina::Editor::editorDocumentKindForAsset(asset->assetKind);
    const Tina::Editor::EditorDocumentKey documentKey{
        .kind = documentKind,
        .assetId = asset->assetId,
    };
    projectAssetSelectionSyncPending_ = true;
    assetInspectorActive_ = true;
    if (const auto existing = documentTabs_.find(documentKey); existing.has_value()) {
        ++counters_.projectAssetOpenCount;
        authoringFeedback_ = "Catalog asset tab activated";
        return activateDocumentTab(tree, static_cast<u32>(*existing));
    }
    if (documentTabs_.tabCount() >= documentTabs_.config().tabCapacity) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabCapacityExceeded,
            "Close a document before opening another Catalog asset");
    }
    auto loadedDocument = loadProjectAssetDocument(*asset);
    if (!loadedDocument) {
        return Tina::Core::failure(std::move(loadedDocument.error()));
    }
    std::optional<WorkspaceSessionState> preparedSession{};
    if (loadedDocument->document.has_value()) {
        auto session = makeProjectAssetSession(
            documentKey, *loadedDocument->document,
            loadedDocument->targetPlatform);
        if (!session) {
            return Tina::Core::failure(std::move(session.error()));
        }
        preparedSession.emplace(std::move(*session));
    }
    const Tina::Core::usize previousActiveIndex = documentTabs_.activeIndex();
    auto opened = documentTabs_.open(Tina::Editor::EditorDocumentTabDesc{
        .key = documentKey,
        .title = asset->displayName,
    });
    if (!opened) {
        return Tina::Core::failure(std::move(opened.error()));
    }
    if (preparedSession.has_value()) {
        if (auto status = installDocumentSession(std::move(*preparedSession)); !status) {
            (void)documentTabs_.close(*opened, true);
            (void)documentTabs_.activate(previousActiveIndex);
            return status;
        }
    }
    if (loadedDocument->document.has_value()) {
        if (auto status = installNewAuthoringDocument(
                documentKey, std::move(*loadedDocument->document));
            !status) {
            discardDocumentSession(documentKey);
            (void)documentTabs_.close(*opened, true);
            (void)documentTabs_.activate(previousActiveIndex);
            return status;
        }
        ++counters_.tabOwnedDocumentLoads;
    }
    ++counters_.projectAssetOpenCount;
    authoringFeedback_ = "Catalog asset opened in a document tab";
    return activateDocumentTab(tree, static_cast<u32>(*opened));
}

auto EditorWorkspaceState::showDirtyCloseModal(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr || tab->pinned || !tab->dirty) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Dirty-close confirmation requires a modified unpinned document");
    }
    const WorkspaceSessionState* session = findDocumentSession(tab->key);
    try {
        std::string title = "Save changes to ";
        title += tab->title;
        title += '?';
        if (auto status = tree.setText(dirtyCloseTitle_, title); !status) {
            return status;
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Dirty-close title allocation failed");
    }
    if (auto status = tree.setText(
            dirtyCloseMessage_,
            session != nullptr && session->hasDocumentPath()
                ? "Save the canonical document, discard the edits, or cancel closing."
                : "Choose a Save As destination, discard the edits, or cancel closing.");
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            dirtyClosePathInput_,
            session != nullptr ? session->documentPathUtf8 : std::string_view{});
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(dirtyClosePathInput_, session != nullptr); !status) {
        return status;
    }
    if (auto status = tree.setText(
            dirtyCloseDialog_.actions[DirtyCloseSaveActionIndex],
            session != nullptr && session->hasDocumentPath() ? "Save" : "Save As");
        !status) {
        return status;
    }
    pendingDirtyCloseKey_ = tab->key;
    if (auto status = tree.openDialog(dirtyCloseDialog_.modal); !status) {
        pendingDirtyCloseKey_.reset();
        return status;
    }
    pendingDirtyCloseDialogFocus_ = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::hideDirtyCloseModal(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (auto status = tree.dismissDialog(dirtyCloseDialog_.modal); !status) {
        return status;
    }
    pendingDirtyCloseKey_.reset();
    pendingDirtyCloseDialogFocus_ = false;
    return Tina::Core::success();
}

auto EditorWorkspaceState::closeActiveDocument(
    Tina::PrimaryWindowUITreeUpdater& tree,
    bool discardDirty) -> Tina::Core::Status{
    if (auto status = synchronizeActiveTabDirty(); !status) {
        return status;
    }
    const Tina::Core::usize closingIndex = documentTabs_.activeIndex();
    const auto* closingTab = documentTabs_.activeTab();
    if (closingTab == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabNotFound,
            "Editor has no active document tab to close");
    }
    if (closingTab->pinned) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::PinnedDocumentCannotClose,
            "Pinned Editor document cannot be closed");
    }
    if (closingTab->dirty && !discardDirty) {
        return showDirtyCloseModal(tree);
    }
    const Tina::Editor::EditorDocumentKey closingKey = closingTab->key;
    if (auto* owner = activeAuthoringDocumentOwner(closingKey.kind);
        owner != nullptr && *owner == closingKey) {
        const Tina::Editor::EditorDocumentTabDesc* replacement = nullptr;
        for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
            const auto* candidate = documentTabs_.tab(index);
            if (index != closingIndex && candidate != nullptr &&
                candidate->key.kind == closingKey.kind) {
                replacement = candidate;
                break;
            }
        }
        if (replacement == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Closing authoring tab has no remaining document owner");
        }
        if (auto status = switchActiveAuthoringDocument(replacement->key); !status) {
            return status;
        }
        discardSuspendedAuthoringDocument(closingKey);
    }
    if (auto status = documentTabs_.close(closingIndex, discardDirty); !status) {
        return status;
    }
    discardDocumentSession(closingKey);
    assetInspectorActive_ = false;
    synchronizeViewportSelectionFromHierarchy();
    authoringFeedback_ = "Document tab closed";
    if (documentTabs_.activeTab() == nullptr) {
        return refreshDocumentTabsUi(tree);
    }
    return activateDocumentTab(tree,
                               static_cast<u32>(documentTabs_.activeIndex()));
}

auto EditorWorkspaceState::dirtyCloseTargetsActiveDocument() const noexcept -> bool{
    const auto* tab = documentTabs_.activeTab();
    return tab != nullptr && pendingDirtyCloseKey_.has_value() &&
           tab->key == *pendingDirtyCloseKey_;
}

auto EditorWorkspaceState::confirmDirtyCloseSave(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!dirtyCloseTargetsActiveDocument()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabNotFound,
            "Dirty-close target is no longer the active document");
    }
    const WorkspaceSessionState* session = activeDocumentSession();
    if (session == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Active dirty document does not support persistence");
    }

    Tina::Core::Status saveStatus = Tina::Core::success();
    if (session->hasDocumentPath()) {
        saveStatus = saveActiveDocument();
    } else {
        auto path = tree.text(dirtyClosePathInput_);
        if (!path) {
            return Tina::Core::failure(std::move(path.error()));
        }
        auto selectedPath = requestNativeSaveAsPath(*path);
        if (!selectedPath) {
            if (selectedPath.error().code == Tina::Core::CoreErrorCode::Unsupported) {
                saveStatus = saveActiveDocument(*path);
            } else {
                saveStatus = Tina::Core::failure(std::move(selectedPath.error()));
            }
        } else if (!selectedPath->has_value()) {
            authoringFeedback_ = "Save As cancelled; dirty document remains open";
            return tree.setText(dirtyCloseMessage_, authoringFeedback_);
        } else {
            saveStatus = saveActiveDocument(**selectedPath);
        }
    }
    if (!saveStatus) {
        try {
            std::string message = "Save failed: ";
            message += saveStatus.error().message;
            authoringFeedback_ = message;
            return tree.setText(dirtyCloseMessage_, message);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Save failure message allocation failed");
        }
    }
    if (auto status = closeActiveDocument(tree); !status) {
        return status;
    }
    projectSwitchBlockedByDirty_ = false;
    return hideDirtyCloseModal(tree);
}

auto EditorWorkspaceState::confirmDirtyCloseDiscard(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!dirtyCloseTargetsActiveDocument()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabNotFound,
            "Dirty-close target is no longer the active document");
    }
    if (auto status = closeActiveDocument(tree, true); !status) {
        return status;
    }
    projectSwitchBlockedByDirty_ = false;
    return hideDirtyCloseModal(tree);
}

auto EditorWorkspaceState::cancelDirtyClose(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!dirtyCloseTargetsActiveDocument()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabNotFound,
            "Dirty-close target is no longer the active document");
    }
    authoringFeedback_ = "Close cancelled; document and selection preserved";
    return hideDirtyCloseModal(tree);
}

auto EditorWorkspaceState::activateDocumentTab(
    Tina::PrimaryWindowUITreeUpdater& tree, u32 index) -> Tina::Core::Status{
    if (index != documentTabs_.activeIndex()) {
        if (auto status = synchronizeActiveTabDirty(); !status) {
            return status;
        }
    }
    const auto* tab = documentTabs_.tab(index);
    if (tab == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabNotFound,
            "Editor document tab does not exist");
    }
    if (index != documentTabs_.activeIndex()) {
        resetViewportInteractionState();
    }
    if (auto status = switchActiveAuthoringDocument(tab->key); !status) {
        return status;
    }
    if (auto status = documentTabs_.activate(index); !status) {
        return status;
    }
    const bool restoreSceneSelection = assetInspectorActive_;
    const u32 preferredHierarchyStableId = restoreSceneSelection
                                               ? stableEntityIdForHierarchyItem(
                                                     selectionKey_)
                                               : 0U;
    assetInspectorActive_ = tab->key.kind ==
                            Tina::Editor::EditorDocumentKind::AssetInspector;
    if (assetInspectorActive_) {
        synchronizeViewportSelectionFromHierarchy();
    }
    switch (Tina::Editor::editorDocumentWorkspace(tab->key.kind)) {
    case Tina::Editor::EditorDocumentWorkspace::TwoD:
        if (auto status = activateWorkspace(tree, WorkspaceMode::World2D); !status) {
            return status;
        }
        break;
    case Tina::Editor::EditorDocumentWorkspace::ThreeD:
        if (auto status = activateWorkspace(tree, WorkspaceMode::World3D); !status) {
            return status;
        }
        break;
    case Tina::Editor::EditorDocumentWorkspace::None:
        break;
    }
    if (!assetInspectorActive_) {
        if (auto status = refreshHierarchyTree(
                tree, preferredHierarchyStableId); !status) {
            return status;
        }
    }
    ++counters_.documentTabSwitches;
    if (auto status = refreshAuthoringUi(tree); !status) {
        return status;
    }
    return refreshDocumentTabsUi(tree);
}

auto EditorWorkspaceState::captureActiveDocumentSavedBaseline() const -> Tina::Core::Result<SavedDocumentBaseline>{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabNotFound,
            "Editor has no active document to save");
    }
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return captureSavedBaseline(document_);
    case Tina::Editor::EditorDocumentKind::World3D:
        return captureSavedBaseline(document3D_);
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return captureSavedBaseline(tileMapDocument_);
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return captureSavedBaseline(spriteAnimationDocument_);
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Read-only Asset Inspector documents cannot be saved");
    }
}

auto EditorWorkspaceState::makeSaveDialogLocation(std::string_view currentPathUtf8,
                       std::string_view fallbackFileNameUtf8,
                       bool directoryTarget) const -> Tina::Core::Result<SaveDialogLocation>{
    SaveDialogLocation location{};
    try {
        location.suggestedFileNameUtf8.assign(fallbackFileNameUtf8);
        if (currentPathUtf8.empty() ||
            !Tina::Core::isStrictUtf8WithoutNul(currentPathUtf8)) {
            return location;
        }

        const std::filesystem::path currentPath =
            std::filesystem::u8path(currentPathUtf8.begin(), currentPathUtf8.end());
        std::filesystem::path directory = directoryTarget
                                              ? currentPath
                                              : currentPath.parent_path();
        std::error_code directoryError;
        if (!directory.empty() &&
            std::filesystem::is_directory(directory, directoryError) &&
            !directoryError) {
            location.initialDirectoryUtf8 = pathToUtf8(directory);
        } else if (directoryTarget) {
            directoryError.clear();
            directory = currentPath.parent_path();
            if (!directory.empty() &&
                std::filesystem::is_directory(directory, directoryError) &&
                !directoryError) {
                location.initialDirectoryUtf8 = pathToUtf8(directory);
            }
        }

        if (!directoryTarget && currentPath.has_filename()) {
            const std::string candidate = pathToUtf8(currentPath.filename());
            if (!candidate.empty()) {
                location.suggestedFileNameUtf8 = candidate;
            }
        }
        return location;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Editor Save As dialog path allocation failed");
    } catch (const std::filesystem::filesystem_error&) {
        return location;
    }
}

auto EditorWorkspaceState::requestNativeSaveAsPath(std::string_view currentPathUtf8) -> Tina::Core::Result<std::optional<std::string>>{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr || activeDocumentSession() == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Active Editor document does not support Save As");
    }

    if (tab->key.kind == Tina::Editor::EditorDocumentKind::TileMap2D) {
        auto location = makeSaveDialogLocation(currentPathUtf8, {}, true);
        if (!location) {
            return Tina::Core::failure(std::move(location.error()));
        }
        auto selected = fileDialog_.pickFolder({
            .titleUtf8 = "Save Tina TileMap",
            .initialDirectoryUtf8 = location->initialDirectoryUtf8,
        });
        if (!selected) {
            return Tina::Core::failure(std::move(selected.error()));
        }
        if (!selected->selected()) {
            return std::optional<std::string>{};
        }
        return std::optional<std::string>{std::move(selected->selectedPathUtf8)};
    }

    std::string_view title = "Save Tina document";
    std::string_view fallbackFileName = "document.tasset";
    std::string_view extension = "tasset";
    Tina::EditorApp::Detail::EditorFileDialogFilter filter{
        .labelUtf8 = "Tina Asset",
        .patternUtf8 = "*.tasset",
    };
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        title = "Save Tina World2D";
        fallbackFileName = "world.tworld";
        extension = "tworld";
        filter = {.labelUtf8 = "Tina World2D", .patternUtf8 = "*.tworld"};
        break;
    case Tina::Editor::EditorDocumentKind::World3D:
        title = "Save Tina World3D Prefab";
        fallbackFileName = "world.tprefab";
        extension = "tprefab";
        filter = {.labelUtf8 = "Tina Prefab", .patternUtf8 = "*.tprefab"};
        break;
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        title = "Save Tina Sprite Animation";
        fallbackFileName = "animation.tasset";
        break;
    case Tina::Editor::EditorDocumentKind::TileMap2D:
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Active Editor document does not support file Save As");
    }

    auto location = makeSaveDialogLocation(currentPathUtf8, fallbackFileName, false);
    if (!location) {
        return Tina::Core::failure(std::move(location.error()));
    }
    const std::array filters{filter};
    auto selected = fileDialog_.saveFile({
        .titleUtf8 = title,
        .initialDirectoryUtf8 = location->initialDirectoryUtf8,
        .suggestedFileNameUtf8 = location->suggestedFileNameUtf8,
        .defaultExtensionUtf8 = extension,
        .filters = filters,
    });
    if (!selected) {
        return Tina::Core::failure(std::move(selected.error()));
    }
    if (!selected->selected()) {
        return std::optional<std::string>{};
    }
    return std::optional<std::string>{std::move(selected->selectedPathUtf8)};
}

auto EditorWorkspaceState::saveActiveDocument(
    std::optional<std::string_view> saveAsPath) -> Tina::Core::Status{
    const auto* tab = documentTabs_.activeTab();
    WorkspaceSessionState* session = activeDocumentSession();
    if (tab == nullptr || session == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Active Editor document does not support persistence");
    }

    std::string preparedPath{};
    try {
        const std::string_view target = saveAsPath.has_value()
                                            ? *saveAsPath
                                            : std::string_view{session->documentPathUtf8};
        if (target.empty() || !Tina::Core::isStrictUtf8WithoutNul(target)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::InvalidArgument,
                "Editor save path must be non-empty strict UTF-8 without NUL");
        }
        if (documentPathOwnedByOtherSession(target, tab->key)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Another open Editor document already owns this save path");
        }
        preparedPath.assign(target);
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Editor save path allocation failed");
    }

    auto preparedBaseline = captureActiveDocumentSavedBaseline();
    if (!preparedBaseline) {
        return Tina::Core::failure(std::move(preparedBaseline.error()));
    }

    Tina::Core::Status status = Tina::Core::success();
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        status = Tina::Editor::saveWorld2DAuthoringDocument(preparedPath,
                                                            document_);
        break;
    case Tina::Editor::EditorDocumentKind::World3D:
        status = Tina::Editor::saveWorld3DAuthoringDocument(preparedPath,
                                                            document3D_);
        break;
    case Tina::Editor::EditorDocumentKind::TileMap2D: {
        auto saved = Tina::Editor::saveTileMapAuthoringDocument(
            preparedPath, tileMapDocument_, session->targetPlatform);
        if (!saved) {
            status = Tina::Core::failure(std::move(saved.error()));
        }
        break;
    }
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        status = Tina::Editor::saveSpriteAnimationAuthoringDocument(
            preparedPath, spriteAnimationDocument_, session->targetPlatform);
        break;
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Read-only Asset Inspector documents cannot be saved");
    }
    if (!status) {
        return status;
    }

    session->documentPathUtf8 = std::move(preparedPath);
    session->savedBaseline = std::move(*preparedBaseline);
    ++counters_.authoringSaves;
    return synchronizeActiveTabDirty();
}

auto EditorWorkspaceState::activeWorkspaceSession() noexcept -> WorkspaceSessionState&{
    return documentSessions_.workspaceSession(workspaceMode_);
}

auto EditorWorkspaceState::activeWorkspaceSession() const noexcept -> const WorkspaceSessionState&{
    return documentSessions_.workspaceSession(workspaceMode_);
}

auto EditorWorkspaceState::workspaceSession(WorkspaceMode mode) const noexcept -> const WorkspaceSessionState&{
    return documentSessions_.workspaceSession(mode);
}

auto EditorWorkspaceState::documentBytes(WorkspaceMode mode) const noexcept -> std::span<const std::byte>{
    return mode == WorkspaceMode::World2D ? document_.snapshotBytes()
                                           : document3D_.payloadBytes();
}

auto EditorWorkspaceState::workspaceSessionDocumentBytes(WorkspaceMode mode) const noexcept -> std::span<const std::byte>{
    if (mode == WorkspaceMode::World2D || !world3DDocumentOwnerKey_.assetId) {
        return documentBytes(mode);
    }
    const Tina::Editor::EditorDocumentKey baseWorld3DKey{
        .kind = Tina::Editor::EditorDocumentKind::World3D,
    };
    const auto* suspended = findSuspendedAuthoringDocument(baseWorld3DKey);
    if (suspended == nullptr) {
        return {};
    }
    const auto* document = std::get_if<Tina::Editor::World3DAuthoringDocument>(
        &suspended->document);
    return document != nullptr ? document->payloadBytes()
                               : std::span<const std::byte>{};
}

auto EditorWorkspaceState::isDocumentDirty(WorkspaceMode mode) const noexcept -> bool{
    const WorkspaceSessionState& session = workspaceSession(mode);
    const std::span<const std::byte> current = workspaceSessionDocumentBytes(mode);
    return !session.savedBaseline.captured ||
           !baselineBytesMatch(session.savedBaseline.primaryBytes, current);
}

auto EditorWorkspaceState::activeTabDocumentDirty() const noexcept -> bool{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr) {
        return false;
    }
    const WorkspaceSessionState* session = findDocumentSession(tab->key);
    if (session == nullptr) {
        return false;
    }
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return !savedBaselineMatches(session->savedBaseline, document_);
    case Tina::Editor::EditorDocumentKind::World3D:
        return !savedBaselineMatches(session->savedBaseline, document3D_);
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return !savedBaselineMatches(session->savedBaseline, tileMapDocument_);
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return !savedBaselineMatches(session->savedBaseline,
                                     spriteAnimationDocument_);
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return false;
    }
}

auto EditorWorkspaceState::synchronizeActiveTabDirty() noexcept -> Tina::Core::Status{
    if (documentTabs_.activeTab() == nullptr) {
        return Tina::Core::success();
    }
    return documentTabs_.setDirty(documentTabs_.activeIndex(),
                                  activeTabDocumentDirty());
}

auto EditorWorkspaceState::publishWorkspaceSessionCounters() noexcept -> void{
    const bool world2DDirty = isDocumentDirty(WorkspaceMode::World2D);
    const bool world3DDirty = isDocumentDirty(WorkspaceMode::World3D);
    const WorkspaceSessionState* active = activeDocumentSession();

    counters_.finalWorkspaceWorld2D = workspaceMode_ == WorkspaceMode::World2D;
    counters_.world2DDocumentPathConfigured = documentSessions_.workspaceSession(WorkspaceMode::World2D).hasDocumentPath();
    counters_.world3DDocumentPathConfigured = documentSessions_.workspaceSession(WorkspaceMode::World3D).hasDocumentPath();
    counters_.world2DDocumentLoaded = documentSessions_.workspaceSession(WorkspaceMode::World2D).loadedFromPath;
    counters_.world3DDocumentLoaded = documentSessions_.workspaceSession(WorkspaceMode::World3D).loadedFromPath;
    counters_.world2DDocumentDirty = world2DDirty;
    counters_.world3DDocumentDirty = world3DDirty;
    counters_.world2DSavedSnapshotBytes =
        documentSessions_.workspaceSession(WorkspaceMode::World2D).savedBaseline.primaryBytes.size();
    counters_.world3DSavedSnapshotBytes =
        documentSessions_.workspaceSession(WorkspaceMode::World3D).savedBaseline.primaryBytes.size();
    counters_.documentPathConfigured =
        active != nullptr && active->hasDocumentPath();
    counters_.documentLoaded = active != nullptr && active->loadedFromPath;
    counters_.documentDirty = activeTabDocumentDirty();
    counters_.documentSaved = counters_.documentPathConfigured &&
                              !counters_.documentDirty;
    counters_.savedSnapshotBytes =
        active != nullptr ? active->savedBaseline.byteCount() : 0U;
    counters_.navigationBakeRevision = navigationDocument_.revision();
    counters_.navigationSourceTileMapRevision =
        navigationDocument_.sourceTileMapRevision();
    counters_.navigationPayloadBytes = navigationDocument_.hasBake()
        ? navigationDocument_.preview().payloadBytes.size()
        : 0U;
    counters_.navigationBakeReady = navigationDocument_.hasBake();
    counters_.navigationBakeDirty =
        navigationDocument_.isDirtyFor(tileMapDocument_.revision());
}

auto EditorWorkspaceState::activeDocumentBytes() const noexcept -> std::span<const std::byte>{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr) {
        return {};
    }
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return document_.snapshotBytes();
    case Tina::Editor::EditorDocumentKind::World3D:
        return document3D_.payloadBytes();
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return tileMapDocument_.rootPayloadBytes();
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return spriteAnimationDocument_.payloadBytes();
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return {};
    }
}

auto EditorWorkspaceState::activeDocumentCanonicalByteCount() const noexcept -> u64{
    u64 total = activeDocumentBytes().size();
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr ||
        tab->key.kind != Tina::Editor::EditorDocumentKind::TileMap2D) {
        return total;
    }
    for (Tina::Core::usize index = 0; index < tileMapDocument_.chunkCount();
         ++index) {
        const auto chunk = tileMapDocument_.chunkPayloadAt(index);
        if (chunk) {
            total += chunk->payloadBytes.size();
        }
    }
    return total;
}

auto EditorWorkspaceState::activeDocumentRevision() const noexcept -> u64{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr) {
        return 0U;
    }
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return document_.revision();
    case Tina::Editor::EditorDocumentKind::World3D:
        return document3D_.revision();
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return tileMapDocument_.revision();
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return spriteAnimationDocument_.revision();
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return 0U;
    }
}

auto EditorWorkspaceState::activeDocumentItemCount() const noexcept -> u64{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr) {
        return 0U;
    }
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return document_.entityCount();
    case Tina::Editor::EditorDocumentKind::World3D:
        return document3D_.nodeCount();
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return tileMapDocument_.layerCount();
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return spriteAnimationDocument_.frameCount();
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return 0U;
    }
}

auto EditorWorkspaceState::activeUndoDepth() const noexcept -> u64{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr) {
        return 0U;
    }
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return document_.undoDepth();
    case Tina::Editor::EditorDocumentKind::World3D:
        return document3D_.undoDepth();
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return tileMapDocument_.undoDepth();
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return spriteAnimationDocument_.undoDepth();
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return 0U;
    }
}

auto EditorWorkspaceState::activeRedoDepth() const noexcept -> u64{
    const auto* tab = documentTabs_.activeTab();
    if (tab == nullptr) {
        return 0U;
    }
    switch (tab->key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
        return document_.redoDepth();
    case Tina::Editor::EditorDocumentKind::World3D:
        return document3D_.redoDepth();
    case Tina::Editor::EditorDocumentKind::TileMap2D:
        return tileMapDocument_.redoDepth();
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return spriteAnimationDocument_.redoDepth();
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return 0U;
    }
}

auto EditorWorkspaceState::activeCanUndo() const noexcept -> bool{
    return activeUndoDepth() != 0U;
}

auto EditorWorkspaceState::activeCanRedo() const noexcept -> bool{
    return activeRedoDepth() != 0U;
}

} // namespace Tina::EditorApp::WorkspaceInternal
