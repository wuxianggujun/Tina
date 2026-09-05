#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::bakeAndPublishNavigation2D() -> Tina::Core::Status{
    if (!tileMapEditingContext() || !previewTileMap_.has_value()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Navigation bake requires a resident 2D TileMap preview");
    }
    if (!assetResources_.system.has_value() ||
        assetResources_.system->catalog() == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Navigation bake requires an open Editor Catalog");
    }

    if (auto status = navigationDocument_.bakeFromTileMap(
            *previewTileMap_,
            Tina::Asset::TileMapNavigation2DDataBuildConfig{
                .solidTileLayerId = InitialTileMapLayerId,
                .blockerObjectLayerId = InitialGameplayObjectLayerId,
                .blockerPropertyKey = "navigation",
                .blockerPropertyValue = "blocked",
            },
            editorAssetId(NavigationPreviewAssetMarker),
            tileMapDocument_.revision(), editorTargetPlatform());
        !status) {
        return status;
    }

    auto stageParent = activeProjectWorkspace_.has_value()
        ? createAuthoringStageRoot(*activeProjectWorkspace_)
        : createUniqueEditorTempDirectory();
    if (!stageParent) {
        return Tina::Core::failure(std::move(stageParent.error()));
    }
    auto cleanupStage = Tina::Core::makeScopeExit([&stageParent]() noexcept {
        std::error_code cleanupError;
        std::filesystem::remove_all(*stageParent, cleanupError);
    });
    const std::filesystem::path catalogPath = *stageParent / "catalog";
    const std::string catalogRootUtf8 = pathToUtf8(catalogPath);
    std::pmr::unsynchronized_pool_resource operationMemory{};
    Tina::Asset::CatalogPackageStageConfig stageConfig =
        Tina::EditorApp::Detail::makeEditorSourceImportStageConfig(operationMemory);
    auto staged = navigationDocument_.stageCatalog(
        catalogRootUtf8, assetResources_.catalogRootUtf8,
        *assetResources_.system->catalog(), stageConfig);
    if (!staged) {
        return Tina::Core::failure(std::move(staged.error()));
    }

    const auto previousFilter = projectAssets_.typeFilter();
    std::optional<Tina::Core::AssetId> previousSelection{};
    previousSelection = projectAssets_.selectedAssetId();
    const std::string_view previousSearchQuery = projectAssets_.searchQuery();
    auto browser = prepareProjectBrowserForSnapshot(
        *staged, previousFilter, previousSelection, previousSearchQuery,
        sourceImportUnitOutputs_, assetMetadata_,
        activeProjectWorkspace_.has_value()
            ? activeProjectWorkspace_->sourceRootUtf8()
            : std::string_view{});
    if (!browser) {
        return Tina::Core::failure(std::move(browser.error()));
    }

    Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
        spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
    Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
        mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
    Tina::Asset::ShaderBindingRegistry* shaderParticipant =
        shaderBindings_.has_value() ? &*shaderBindings_ : nullptr;
    Tina::Asset::CatalogReloadConfig reloadConfig{};
    reloadConfig.package.manifest.catalog.maxEntries = 4096;
    reloadConfig.package.manifest.catalog.maxDependencies = 16384;
    reloadConfig.package.manifest.catalog.maxDependenciesPerAsset = 4096;
    reloadConfig.package.validation.verifyTypedPayload = true;
    if (spriteParticipant != nullptr) {
        reloadConfig.bindings.sprite2D =
            std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
    }
    if (meshParticipant != nullptr) {
        reloadConfig.bindings.mesh3D =
            std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
    }
    if (shaderParticipant != nullptr) {
        reloadConfig.bindings.shader =
            std::span<Tina::Asset::ShaderBindingRegistry*>{&shaderParticipant, 1U};
    }
    const bool persistentProjectStage = activeProjectWorkspace_.has_value();
    bool temporaryOwnerRegistered = false;
    if (!persistentProjectStage) {
        try {
            assetResources_.ownedCatalogStageRoots.reserve(
                assetResources_.ownedCatalogStageRoots.size() + 1U);
            assetResources_.ownedCatalogStageRoots.push_back(*stageParent);
            temporaryOwnerRegistered = true;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Navigation Catalog stage ownership allocation failed before reload");
        }
    }

    std::filesystem::path authoringPointerPath;
    std::string previousPointerBytes;
    bool pointerExisted = false;
    if (activeProjectWorkspace_.has_value()) {
        const auto cache = authoringCachePaths(*activeProjectWorkspace_);
        authoringPointerPath = cache.activeCatalogPointer;
        std::error_code pointerError;
        const auto pointerStatus = std::filesystem::symlink_status(authoringPointerPath, pointerError);
        if (!pointerError && std::filesystem::is_regular_file(pointerStatus)) {
            auto bytes = Tina::Core::readFile(pathToUtf8(authoringPointerPath),
                                              {.maxBytes = 4096, .memoryResource = &operationMemory});
            if (!bytes) {
                if (temporaryOwnerRegistered) {
                    assetResources_.ownedCatalogStageRoots.pop_back();
                }
                return Tina::Core::failure(std::move(bytes.error()));
            }
            previousPointerBytes.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            pointerExisted = true;
        } else if (pointerError && pointerError != std::errc::no_such_file_or_directory) {
            if (temporaryOwnerRegistered) {
                assetResources_.ownedCatalogStageRoots.pop_back();
            }
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                       "Navigation authoring Catalog pointer inspection failed");
        }
        const std::string stageRootText = catalogRootUtf8;
        const auto pointerText = std::as_bytes(
            std::span{stageRootText.data(), stageRootText.size()});
        if (auto status = Tina::Core::writeFile(pathToUtf8(authoringPointerPath), pointerText); !status) {
            if (temporaryOwnerRegistered) {
                assetResources_.ownedCatalogStageRoots.pop_back();
            }
            return status;
        }
    }

    auto reload = assetResources_.system->reloadCatalog(catalogRootUtf8, reloadConfig);
    if (!reload) {
        if (activeProjectWorkspace_.has_value()) {
            if (pointerExisted) {
                const auto bytes = std::as_bytes(std::span{previousPointerBytes.data(), previousPointerBytes.size()});
                (void)Tina::Core::writeFile(pathToUtf8(authoringPointerPath), bytes);
            } else {
                std::error_code pointerError;
                std::filesystem::remove(authoringPointerPath, pointerError);
            }
        }
        if (temporaryOwnerRegistered) {
            assetResources_.ownedCatalogStageRoots.pop_back();
        }
        return reportAuthoringFailure(
            "Navigation bake staged; previous Catalog preserved: ", reload.error());
    }

    std::string nextCatalogRoot = catalogRootUtf8;
    std::string supersededAuthoringCatalogRoot;
    if (persistentProjectStage) {
        supersededAuthoringCatalogRoot = assetResources_.authoringCatalogRootUtf8;
    }
    cleanupStage.release();
    assetResources_.catalogRootUtf8.swap(nextCatalogRoot);
    if (persistentProjectStage) {
        assetResources_.authoringCatalogRootUtf8 = assetResources_.catalogRootUtf8;
        cleanupOwnedAuthoringStage(supersededAuthoringCatalogRoot);
    }
    assetResources_.catalogEntryCount = static_cast<u32>(browser->itemCount());
    counters_.catalogEntryCount = assetResources_.catalogEntryCount;
    projectAssets_ = std::move(*browser);
    navigationDocument_.markCatalogPublished();
    ++counters_.navigationCatalogPublishes;
    observedProjectAssetSelectionIndex_.reset();
    projectBrowserUiRefreshPending_ = true;
    // This command runs during UIUpdate, after RenderExtract has borrowed the
    // current bindings into the frame packet. Rebuild them next frame before
    // the next extraction, once those borrows have been released.
    previewAssetBindingsRefreshPending_ = true;
    authoringFeedback_ =
        "Navigation2D baked and published; preview rebuild scheduled";
    return Tina::Core::success();
}

auto EditorWorkspaceState::viewportTileCellAtPosition(
    UI::UILogicalPoint position) const noexcept
    -> std::optional<Tina::Editor::TileMapAuthoringCellEdit>{
    if (!tileMapEditingContext() || !previewWorld_.has_value() ||
        tileMapWidthCells_ == 0U || tileMapHeightCells_ == 0U ||
        viewportLogicalRect_.width <= 0.0F || viewportLogicalRect_.height <= 0.0F) {
        return std::nullopt;
    }
    const Tina::Scene::WorldTransform* cameraTransform =
        previewWorld_->worldTransform(previewCamera2D_);
    if (cameraTransform == nullptr) {
        return std::nullopt;
    }
    const float normalizedX =
        (position.x - viewportLogicalRect_.x) / viewportLogicalRect_.width;
    const float normalizedY =
        (position.y - viewportLogicalRect_.y) / viewportLogicalRect_.height;
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
        normalizedX < 0.0F || normalizedX >= 1.0F ||
        normalizedY < 0.0F || normalizedY >= 1.0F) {
        return std::nullopt;
    }
    const float worldX = cameraTransform->position.x +
                         (normalizedX - 0.5F) * viewportWorldWidth();
    const float worldY = cameraTransform->position.y +
                         (0.5F - normalizedY) * viewportWorldHeight();
    if (!std::isfinite(worldX) || !std::isfinite(worldY) ||
        worldX < 0.0F || worldY < 0.0F) {
        return std::nullopt;
    }
    const u32 cellX = static_cast<u32>(std::floor(worldX));
    const u32 cellY = static_cast<u32>(std::floor(worldY));
    if (cellX >= tileMapWidthCells_ || cellY >= tileMapHeightCells_) {
        return std::nullopt;
    }
    return Tina::Editor::TileMapAuthoringCellEdit{.x = cellX, .y = cellY};
}

auto EditorWorkspaceState::authoredTileAt(u32 x, u32 y) const noexcept
    -> Tina::Core::u16{
    auto authored = tileMapDocument_.snapshot();
    if (!authored || x >= authored->widthCells || y >= authored->heightCells ||
        authored->chunkSizeCells == 0U) {
        return Tina::AssetFormat::TileMapWire::EmptyTileId;
    }
    const auto layer = std::find_if(
        authored->layers.begin(), authored->layers.end(), [this](const auto& candidate) {
            return candidate.stableLayerId == activeTileMapLayerId_ &&
                   candidate.kind == Tina::AssetFormat::TileMapLayerKind::Tile;
        });
    if (layer == authored->layers.end()) {
        return Tina::AssetFormat::TileMapWire::EmptyTileId;
    }
    const u32 chunkX = x / authored->chunkSizeCells;
    const u32 chunkY = y / authored->chunkSizeCells;
    const auto chunk = std::find_if(
        layer->chunks.begin(), layer->chunks.end(),
        [chunkX, chunkY](const Tina::Editor::TileMapAuthoringChunk& candidate) {
            return candidate.chunkX == chunkX && candidate.chunkY == chunkY;
        });
    if (chunk == layer->chunks.end()) {
        // A chunk holding only empty cells is dropped by the document, so a
        // missing chunk means empty rather than out of range.
        return Tina::AssetFormat::TileMapWire::EmptyTileId;
    }
    const u32 originX = chunkX * authored->chunkSizeCells;
    const u32 originY = chunkY * authored->chunkSizeCells;
    const u32 width = (std::min)(static_cast<u32>(authored->chunkSizeCells),
                                 authored->widthCells - originX);
    if (width == 0U) {
        return Tina::AssetFormat::TileMapWire::EmptyTileId;
    }
    const auto index =
        static_cast<Tina::Core::usize>(y - originY) * width + (x - originX);
    return index < chunk->cells.size()
               ? chunk->cells[index]
               : Tina::AssetFormat::TileMapWire::EmptyTileId;
}

auto EditorWorkspaceState::appendViewportTileStrokeCell(u32 x, u32 y) noexcept -> bool{
    for (Tina::Core::usize index = 0; index < tileStroke_.cellCount; ++index) {
        if (tileStroke_.cells[index].x == x && tileStroke_.cells[index].y == y) {
            // Already covered. Rewriting in place keeps the batch free of the
            // duplicate coordinates setCells rejects, which is what a drag that
            // wanders back over its own path would otherwise produce.
            tileStroke_.cells[index].localTileId = tileStroke_.localTileId;
            return true;
        }
    }
    if (tileStroke_.cellCount == tileStroke_.cells.size()) {
        tileStroke_.truncated = true;
        return false;
    }
    tileStroke_.cells[tileStroke_.cellCount++] = Tina::Editor::TileMapAuthoringCellEdit{
        .x = x,
        .y = y,
        .localTileId = tileStroke_.localTileId,
    };
    return true;
}

auto EditorWorkspaceState::rebuildViewportTileStrokeRectangle() noexcept -> void{
    tileStroke_.cellCount = 0;
    tileStroke_.truncated = false;
    const u32 minX = (std::min)(tileStroke_.anchorX, tileStroke_.currentX);
    const u32 maxX = (std::max)(tileStroke_.anchorX, tileStroke_.currentX);
    const u32 minY = (std::min)(tileStroke_.anchorY, tileStroke_.currentY);
    const u32 maxY = (std::max)(tileStroke_.anchorY, tileStroke_.currentY);
    for (u32 y = minY; y <= maxY; ++y) {
        for (u32 x = minX; x <= maxX; ++x) {
            if (!appendViewportTileStrokeCell(x, y)) {
                return;
            }
        }
    }
}

auto EditorWorkspaceState::beginViewportTileStroke(
    Tina::Platform::PointerId pointer,
    UI::UILogicalPoint position) noexcept -> bool{
    if (!authoringEnabled() || !tileMapEditingContext() ||
        (viewportToolMode_ != ViewportToolMode::TilePaint &&
         viewportToolMode_ != ViewportToolMode::TileErase) ||
        tileStroke_.captured || viewportNavigationDrag_.captured ||
        viewportGizmo_.captured || viewportMarquee_.captured) {
        return false;
    }
    const auto cell = viewportTileCellAtPosition(position);
    if (!cell.has_value()) {
        return false;
    }
    const bool erase = viewportToolMode_ == ViewportToolMode::TileErase;
    tileStroke_ = ViewportTileBrushStroke{
        .pointer = pointer,
        .layerId = activeTileMapLayerId_,
        .localTileId = erase ? Tina::AssetFormat::TileMapWire::EmptyTileId
                             : selectedTileId_,
        .anchorX = cell->x,
        .anchorY = cell->y,
        .currentX = cell->x,
        .currentY = cell->y,
        .captured = true,
    };
    // The chord is not readable yet this frame, so the gesture starts as a
    // freehand stroke of one cell. processViewportTileStroke promotes it to a
    // rectangle if Shift turns out to be held.
    return appendViewportTileStrokeCell(cell->x, cell->y);
}

auto EditorWorkspaceState::updateViewportTileStroke(
    Tina::Platform::PointerId pointer,
    UI::UILogicalPoint position) noexcept -> bool{
    if (!tileStroke_.captured || pointer != tileStroke_.pointer ||
        tileStroke_.commitRequested || tileStroke_.cancelRequested) {
        return false;
    }
    const auto cell = viewportTileCellAtPosition(position);
    if (!cell.has_value()) {
        // Leaving the map does not end the gesture: the pointer can travel back
        // in. It just contributes no cell, so no clamped edge cell is painted.
        return true;
    }
    tileStroke_.currentX = cell->x;
    tileStroke_.currentY = cell->y;
    if (tileStroke_.rectangle) {
        rebuildViewportTileStrokeRectangle();
        return true;
    }
    (void)appendViewportTileStrokeCell(cell->x, cell->y);
    return true;
}

auto EditorWorkspaceState::pickViewportTileUnderCursor(
    UI::UILogicalPoint position) noexcept -> bool{
    if (!authoringEnabled() || !tileMapEditingContext()) {
        return false;
    }
    const auto cell = viewportTileCellAtPosition(position);
    if (!cell.has_value()) {
        return false;
    }
    const Tina::Core::u16 sampled = authoredTileAt(cell->x, cell->y);
    if (sampled == Tina::AssetFormat::TileMapWire::EmptyTileId) {
        // Picking empty would set an unpaintable brush: id 0 is the erase value,
        // so adopting it would silently turn Paint into Erase.
        authoringFeedback_ = "Tile pick found an empty cell; brush unchanged";
        return true;
    }
    selectedTileId_ = sampled;
    // Let the palette re-derive its highlight from the new brush id.
    observedTilePaletteSelection_.reset();
    return true;
}

auto EditorWorkspaceState::processViewportTileStroke(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!tileStroke_.captured) {
        return Tina::Core::success();
    }
    if (tileStroke_.cancelRequested) {
        tileStroke_ = {};
        return refreshAuthoringUi(tree);
    }
    // Resolve the gesture's mode now that this frame's chord is mapped. Promoting
    // mid-gesture is deliberate: the user may press Shift after starting to drag,
    // and the rectangle then spans from the original anchor.
    if (tileBrushShiftActive_ != tileStroke_.rectangle) {
        tileStroke_.rectangle = tileBrushShiftActive_;
        if (tileStroke_.rectangle) {
            rebuildViewportTileStrokeRectangle();
        }
    }
    if (!tileStroke_.commitRequested) {
        return Tina::Core::success();
    }

    const ViewportTileBrushStroke stroke = tileStroke_;
    tileStroke_ = {};
    if (stroke.cellCount == 0U) {
        return Tina::Core::success();
    }

    const u64 revisionBefore = tileMapDocument_.revision();
    if (auto status = tileMapDocument_.setCells(
            stroke.layerId,
            std::span{stroke.cells.data(), stroke.cellCount});
        !status) {
        return status;
    }
    const bool erase =
        stroke.localTileId == Tina::AssetFormat::TileMapWire::EmptyTileId;
    if (tileMapDocument_.revision() == revisionBefore) {
        // Every cell already held this value. That is a successful no-op, and it
        // must not consume an undo step or report an edit.
        authoringFeedback_ = erase
                                 ? "Tile erase left already-empty cells unchanged"
                                 : "Tile paint left the existing tiles unchanged";
        return refreshAuthoringUi(tree);
    }
    if (erase) {
        lastPaintedTile_.reset();
    } else {
        lastPaintedTile_ = stroke.cells[stroke.cellCount - 1U];
    }
    // One stroke is one edit, regardless of how many cells it covered: the
    // counters track undoable revisions, and the document published exactly one.
    ++counters_.authoringEdits;
    ++counters_.tileMapEdits;
    if (auto status = validateRuntimePreview(); !status) {
        return status;
    }
    try {
        authoringFeedback_ = erase ? "Viewport tile erase committed: "
                                   : "Viewport tile paint committed: ";
        authoringFeedback_ += std::to_string(stroke.cellCount);
        authoringFeedback_ += stroke.cellCount == 1U ? " cell" : " cells";
        if (stroke.rectangle) {
            authoringFeedback_ += " (rectangle)";
        }
        if (stroke.truncated) {
            authoringFeedback_ += "; stroke reached its cell limit";
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor tile brush feedback allocation failed");
    }
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::editTileMapBrushCell(bool erase) -> Tina::Core::Status{
    if (!tileMapEditingContext()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "TileMap brush requires the 2D TileMap selection");
    }
    auto authored = tileMapDocument_.snapshot();
    if (!authored) {
        return Tina::Core::failure(std::move(authored.error()));
    }
    auto layer = std::find_if(
        authored->layers.begin(), authored->layers.end(), [this](const auto& candidate) {
            return candidate.stableLayerId == activeTileMapLayerId_ &&
                   candidate.kind == Tina::AssetFormat::TileMapLayerKind::Tile;
        });
    if (layer == authored->layers.end()) {
        layer = std::find_if(authored->layers.begin(), authored->layers.end(),
                             [](const auto& candidate) {
                                 return candidate.kind ==
                                        Tina::AssetFormat::TileMapLayerKind::Tile;
                             });
    }
    if (layer == authored->layers.end()) {
        return Tina::Core::failure(Tina::Editor::EditorErrorCode::LayerNotFound,
                                   "TileMap document has no tile layer for the brush");
    }
    activeTileMapLayerId_ = layer->stableLayerId;

    u32 x = tileBrushX_ % authored->widthCells;
    u32 y = tileBrushY_ % authored->heightCells;
    if (erase && lastPaintedTile_.has_value()) {
        x = lastPaintedTile_->x;
        y = lastPaintedTile_->y;
        // authoredTileAt reads the active layer, which the line above just set to
        // this layer, so the lookup matches the layer being edited here.
    } else if (erase && authoredTileAt(x, y) ==
                            Tina::AssetFormat::TileMapWire::EmptyTileId) {
        bool found = false;
        for (const auto& chunk : layer->chunks) {
            const u32 originX = chunk.chunkX * authored->chunkSizeCells;
            const u32 originY = chunk.chunkY * authored->chunkSizeCells;
            const u32 width = (std::min)(
                static_cast<u32>(authored->chunkSizeCells), authored->widthCells - originX);
            for (Tina::Core::usize index = 0; index < chunk.cells.size(); ++index) {
                if (chunk.cells[index] == 0U) {
                    continue;
                }
                x = originX + static_cast<u32>(index % width);
                y = originY + static_cast<u32>(index / width);
                found = true;
                break;
            }
            if (found) {
                break;
            }
        }
    }

    Tina::Core::u16 tileId = erase ? 0U : selectedTileId_;
    const auto revisionBefore = tileMapDocument_.revision();
    if (auto status = tileMapDocument_.paintCell(layer->stableLayerId, x, y, tileId);
        !status) {
        return status;
    }
    if (tileMapDocument_.revision() == revisionBefore) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "TileMap brush operation did not change the selected cell");
    }
    if (erase) {
        lastPaintedTile_.reset();
    } else {
        lastPaintedTile_ = Tina::Editor::TileMapAuthoringCellEdit{
            .x = x,
            .y = y,
            .localTileId = tileId,
        };
        selectedTileId_ = tileId;
        tileBrushX_ = (x + 1U) % authored->widthCells;
        tileBrushY_ = y + (tileBrushX_ == 0U ? 1U : 0U);
        tileBrushY_ %= authored->heightCells;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::toggleActiveTileMapLayer() -> Tina::Core::Status{
    if (!tileMapEditingContext()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "TileMap layer controls require the 2D TileMap selection");
    }
    auto authored = tileMapDocument_.snapshot();
    if (!authored) {
        return Tina::Core::failure(std::move(authored.error()));
    }
    auto layer = std::find_if(
        authored->layers.begin(), authored->layers.end(), [this](const auto& candidate) {
            return candidate.stableLayerId == activeTileMapLayerId_ &&
                   candidate.kind == Tina::AssetFormat::TileMapLayerKind::Tile;
        });
    if (layer == authored->layers.end()) {
        layer = std::find_if(authored->layers.begin(), authored->layers.end(),
                             [](const auto& candidate) {
                                 return candidate.kind ==
                                        Tina::AssetFormat::TileMapLayerKind::Tile;
                             });
    }
    if (layer == authored->layers.end()) {
        return Tina::Core::failure(Tina::Editor::EditorErrorCode::LayerNotFound,
                                   "TileMap document has no tile layer to toggle");
    }
    activeTileMapLayerId_ = layer->stableLayerId;
    return tileMapDocument_.setLayerVisibility(layer->stableLayerId, !layer->visible);
}

auto EditorWorkspaceState::addTileMapLayer(Tina::AssetFormat::TileMapLayerKind kind) -> Tina::Core::Status{
    if (!tileMapEditingContext()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "TileMap layer controls require the 2D TileMap selection");
    }
    auto authored = tileMapDocument_.snapshot();
    if (!authored) {
        return Tina::Core::failure(std::move(authored.error()));
    }
    u32 layerId = 1U;
    while (std::any_of(authored->layers.begin(), authored->layers.end(),
                       [layerId](const auto& candidate) {
                           return candidate.stableLayerId == layerId;
                       })) {
        if (layerId == (std::numeric_limits<u32>::max)()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                "TileMap layer identity space is exhausted");
        }
        ++layerId;
    }
    if (kind == Tina::AssetFormat::TileMapLayerKind::Tile) {
        if (auto status = tileMapDocument_.addTileLayer(
                layerId, "Tile Layer " + std::to_string(layerId)); !status) {
            return status;
        }
        activeTileMapLayerId_ = layerId;
        return Tina::Core::success();
    }
    return tileMapDocument_.addObjectLayer(
        layerId, "Object Layer " + std::to_string(layerId));
}

auto EditorWorkspaceState::updateViewportTileCursorVisual(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    Tina::Core::usize nodeCount = 0;
    const auto hideRemaining = [&]() -> Tina::Core::Status {
        UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
        collapsed.placement = UI::UILayoutPlacement::Overlay;
        collapsed.visibility = UI::UIVisibility::Collapsed;
        for (Tina::Core::usize index = nodeCount;
             index < viewportTileCursorVisibleNodeCount_; ++index) {
            if (auto status = tree.setLayoutStyle(
                    viewportTileCursorNodes_[index], collapsed);
                !status) {
                return status;
            }
        }
        viewportTileCursorVisibleNodeCount_ = nodeCount;
        return Tina::Core::success();
    };

    const bool tileTool = viewportToolMode_ == ViewportToolMode::TilePaint ||
                          viewportToolMode_ == ViewportToolMode::TileErase;
    if (!tileTool || !tileMapEditingContext() || !hoveredTileCell_.has_value() ||
        tileMapWidthCells_ == 0U || tileMapHeightCells_ == 0U ||
        !previewWorld_.has_value() ||
        viewportLogicalRect_.width <= 0.0F ||
        viewportLogicalRect_.height <= 0.0F) {
        return hideRemaining();
    }
    const Tina::Scene::WorldTransform* cameraTransform =
        previewWorld_->worldTransform(previewCamera2D_);
    if (cameraTransform == nullptr) {
        return hideRemaining();
    }
    const float worldWidth = viewportWorldWidth();
    const float worldHeight = viewportWorldHeight();
    if (!(worldWidth > 0.0F) || !(worldHeight > 0.0F)) {
        return hideRemaining();
    }
    // A cell is one world unit, matching the world-to-cell floor above.
    const float pixelsPerCellX = viewportLogicalRect_.width / worldWidth;
    const float pixelsPerCellY = viewportLogicalRect_.height / worldHeight;
    if (!std::isfinite(pixelsPerCellX) || !std::isfinite(pixelsPerCellY)) {
        return hideRemaining();
    }
    const float originWorldX = cameraTransform->position.x - worldWidth * 0.5F;
    const float originWorldY = cameraTransform->position.y + worldHeight * 0.5F;

    // Viewport-local rect of one cell. Y is flipped: world Y grows upward while
    // the viewport's logical Y grows downward.
    const auto cellRect = [&](u32 minCellX, u32 minCellY, u32 maxCellX,
                              u32 maxCellY) noexcept {
        struct Rect final {
            float left = 0.0F;
            float top = 0.0F;
            float right = 0.0F;
            float bottom = 0.0F;
        };
        const float left =
            (static_cast<float>(minCellX) - originWorldX) * pixelsPerCellX;
        const float right =
            (static_cast<float>(maxCellX + 1U) - originWorldX) * pixelsPerCellX;
        const float top =
            (originWorldY - static_cast<float>(maxCellY + 1U)) * pixelsPerCellY;
        const float bottom =
            (originWorldY - static_cast<float>(minCellY)) * pixelsPerCellY;
        return Rect{.left = left, .top = top, .right = right, .bottom = bottom};
    };
    const auto placeQuad = [&](float left, float top, float right, float bottom,
                               const UI::UIBoxPaint& paint) -> Tina::Core::Status {
        if (nodeCount == viewportTileCursorNodes_.size()) {
            return Tina::Core::success();
        }
        const float clippedLeft =
            std::clamp(left, 0.0F, viewportLogicalRect_.width);
        const float clippedTop =
            std::clamp(top, 0.0F, viewportLogicalRect_.height);
        const float clippedRight =
            std::clamp(right, 0.0F, viewportLogicalRect_.width);
        const float clippedBottom =
            std::clamp(bottom, 0.0F, viewportLogicalRect_.height);
        if (!(clippedRight > clippedLeft) || !(clippedBottom > clippedTop)) {
            return Tina::Core::success();
        }
        UI::UILayoutStyle style = fixedSize(clippedRight - clippedLeft,
                                            clippedBottom - clippedTop);
        style.placement = UI::UILayoutPlacement::Overlay;
        style.overlay.horizontal = UI::UIAxisAlignment::Start;
        style.overlay.vertical = UI::UIAxisAlignment::Start;
        style.overlay.offset.x = UI::UILayoutLength::Px(clippedLeft);
        style.overlay.offset.y = UI::UILayoutLength::Px(clippedTop);
        const UI::UINodeId node = viewportTileCursorNodes_[nodeCount++];
        if (auto status = tree.setLayoutStyle(node, style); !status) {
            return status;
        }
        return tree.setBoxPaint(node, paint);
    };

    // Erase reads as the destructive colour; paint reuses the selection teal.
    const bool erase = viewportToolMode_ == ViewportToolMode::TileErase;
    const UI::UIStraightSrgba8Color fill =
        erase ? UI::rgb(0xFF9B91, 70) : UI::rgb(0x64D8B4, 70);
    const UI::UIStraightSrgba8Color edge =
        erase ? UI::rgb(0xFF9B91, 220) : UI::rgb(0x8BE8CC, 220);

    const auto hovered = cellRect(hoveredTileCell_->x, hoveredTileCell_->y,
                                  hoveredTileCell_->x, hoveredTileCell_->y);
    if (auto status = placeQuad(hovered.left, hovered.top, hovered.right,
                                hovered.bottom, UI::makeSolidBox(fill));
        !status) {
        return status;
    }

    // A rectangle stroke in progress also outlines the region it would commit,
    // which is the only way to see the extent before releasing.
    if (tileStroke_.captured && tileStroke_.rectangle) {
        const auto region = cellRect(
            (std::min)(tileStroke_.anchorX, tileStroke_.currentX),
            (std::min)(tileStroke_.anchorY, tileStroke_.currentY),
            (std::max)(tileStroke_.anchorX, tileStroke_.currentX),
            (std::max)(tileStroke_.anchorY, tileStroke_.currentY));
        constexpr float EdgeThickness = 1.5F;
        const UI::UIBoxPaint edgePaint = UI::makeSolidBox(edge);
        if (auto status = placeQuad(region.left, region.top, region.right,
                                    region.top + EdgeThickness, edgePaint);
            !status) {
            return status;
        }
        if (auto status = placeQuad(region.left, region.bottom - EdgeThickness,
                                    region.right, region.bottom, edgePaint);
            !status) {
            return status;
        }
        if (auto status = placeQuad(region.left, region.top,
                                    region.left + EdgeThickness, region.bottom,
                                    edgePaint);
            !status) {
            return status;
        }
        if (auto status = placeQuad(region.right - EdgeThickness, region.top,
                                    region.right, region.bottom, edgePaint);
            !status) {
            return status;
        }
    }
    return hideRemaining();
}

} // namespace Tina::EditorApp::WorkspaceInternal
