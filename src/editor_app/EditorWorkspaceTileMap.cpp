#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::queueViewportTileBrush(UI::UILogicalPoint position) noexcept -> bool{
    if (!authoringEnabled() || !tileMapEditingContext() ||
        (viewportToolMode_ != ViewportToolMode::TilePaint &&
         viewportToolMode_ != ViewportToolMode::TileErase) ||
        pendingTileCellEdit_.has_value() || !previewWorld_.has_value() ||
        tileMapWidthCells_ == 0U || tileMapHeightCells_ == 0U ||
        viewportLogicalRect_.width <= 0.0F || viewportLogicalRect_.height <= 0.0F) {
        return false;
    }
    const Tina::Scene::WorldTransform* cameraTransform =
        previewWorld_->worldTransform(previewCamera2D_);
    if (cameraTransform == nullptr) {
        return false;
    }
    const float normalizedX =
        (position.x - viewportLogicalRect_.x) / viewportLogicalRect_.width;
    const float normalizedY =
        (position.y - viewportLogicalRect_.y) / viewportLogicalRect_.height;
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
        normalizedX < 0.0F || normalizedX >= 1.0F ||
        normalizedY < 0.0F || normalizedY >= 1.0F) {
        return false;
    }
    const float worldHeight = viewportWorldHeight();
    const float worldWidth = viewportWorldWidth();
    const float worldX = cameraTransform->position.x +
                         (normalizedX - 0.5F) * worldWidth;
    const float worldY = cameraTransform->position.y +
                         (0.5F - normalizedY) * worldHeight;
    if (!std::isfinite(worldX) || !std::isfinite(worldY) ||
        worldX < 0.0F || worldY < 0.0F) {
        return false;
    }
    const u32 cellX = static_cast<u32>(std::floor(worldX));
    const u32 cellY = static_cast<u32>(std::floor(worldY));
    if (cellX >= tileMapWidthCells_ || cellY >= tileMapHeightCells_) {
        return false;
    }
    pendingTileCellEdit_ = Tina::Editor::TileMapAuthoringCellEdit{
        .x = cellX,
        .y = cellY,
        .localTileId = viewportToolMode_ == ViewportToolMode::TileErase
                           ? Tina::Core::u16{0}
                           : selectedTileId_,
    };
    pendingTileLayerId_ = activeTileMapLayerId_;
    return true;
}

auto EditorWorkspaceState::processPendingTileBrush(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!pendingTileCellEdit_.has_value()) {
        return Tina::Core::success();
    }
    const Tina::Editor::TileMapAuthoringCellEdit edit = *pendingTileCellEdit_;
    const auto layerId = pendingTileLayerId_;
    pendingTileCellEdit_.reset();
    pendingTileLayerId_ = 0;

    const u64 revisionBefore = tileMapDocument_.revision();
    if (auto status = tileMapDocument_.paintCell(
            layerId, edit.x, edit.y, edit.localTileId); !status) {
        return status;
    }
    if (tileMapDocument_.revision() == revisionBefore) {
        authoringFeedback_ = edit.localTileId == 0U
                                 ? "Tile erase left an already-empty cell unchanged"
                                 : "Tile paint left the existing tile unchanged";
        return refreshAuthoringUi(tree);
    }
    if (edit.localTileId != 0U) {
        lastPaintedTile_ = edit;
        selectedTileId_ = static_cast<Tina::Core::u16>(edit.localTileId % 4U + 1U);
    } else {
        lastPaintedTile_.reset();
    }
    ++counters_.authoringEdits;
    ++counters_.tileMapEdits;
    if (auto status = validateRuntimePreview(); !status) {
        return status;
    }
    authoringFeedback_ = edit.localTileId == 0U
                             ? "Viewport tile erase committed"
                             : "Viewport tile paint committed";
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

    const auto authoredCellAt = [&](u32 x, u32 y) noexcept {
        const u32 chunkX = x / authored->chunkSizeCells;
        const u32 chunkY = y / authored->chunkSizeCells;
        const auto chunk = std::find_if(
            layer->chunks.begin(), layer->chunks.end(),
            [chunkX, chunkY](const Tina::Editor::TileMapAuthoringChunk& candidate) {
                return candidate.chunkX == chunkX && candidate.chunkY == chunkY;
            });
        if (chunk == layer->chunks.end()) {
            return Tina::Core::u16{0};
        }
        const u32 originX = chunkX * authored->chunkSizeCells;
        const u32 originY = chunkY * authored->chunkSizeCells;
        const u32 width = (std::min)(
            static_cast<u32>(authored->chunkSizeCells), authored->widthCells - originX);
        const auto index = static_cast<Tina::Core::usize>(y - originY) * width +
                           (x - originX);
        return index < chunk->cells.size() ? chunk->cells[index] : Tina::Core::u16{0};
    };

    u32 x = tileBrushX_ % authored->widthCells;
    u32 y = tileBrushY_ % authored->heightCells;
    if (erase && lastPaintedTile_.has_value()) {
        x = lastPaintedTile_->x;
        y = lastPaintedTile_->y;
    } else if (erase && authoredCellAt(x, y) == 0U) {
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
    if (!erase && authoredCellAt(x, y) == tileId) {
        tileId = static_cast<Tina::Core::u16>(tileId % 4U + 1U);
    }
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
        selectedTileId_ = static_cast<Tina::Core::u16>(tileId % 4U + 1U);
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

} // namespace Tina::EditorApp::WorkspaceInternal
