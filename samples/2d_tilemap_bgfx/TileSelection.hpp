#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/InputActions.hpp>

#include <cmath>
#include <optional>
#include <span>

namespace Tina::Sample2D {

// The product sample owns this map-local projection. Runtime has already
// locked the world pointer payload, so this consumer must not inspect Camera2D
// or Platform coordinates again.
struct TileSelectionGrid final {
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float cellSizeMeters = 0.0F;
};

struct SelectedTile final {
    Core::u32 cellX = 0;
    Core::u32 cellY = 0;
    Render::WorldPointerSample worldPointer{};

    auto operator<=>(const SelectedTile&) const = default;
};

struct TileSelectionCounters final {
    Core::u64 pointerPresses = 0;
    Core::u64 missingWorldPointerSamples = 0;
    Core::u64 viewportMisses = 0;
    Core::u64 mapMisses = 0;
    Core::u64 selectionHits = 0;
    std::optional<SelectedTile> lastSelection{};
};

// Stable entity key reserved for the single selection overlay sprite.
// Tiles use small keys from emitVisibleTileMapSprites; character/crate use 900001+.
inline constexpr Core::u64 SelectionHighlightStableEntityKey = 800001;

// Sorting: tiles default layer 0; character/crate use layer 1. Selection uses
// layer 2 so it draws above both tiles and actors as an explicit selection ring.
// Only the latest lastSelection cell is emitted (cancel/reselect replaces it).
inline constexpr Core::i16 SelectionHighlightSortingLayer = 2;
inline constexpr Core::i32 SelectionHighlightOrderInLayer = 0;

// Slightly inset relative to the cell so the highlight reads as a frame over
// the tile rather than fully covering it.
inline constexpr float SelectionHighlightInsetScale = 0.92F;

// Consumes only the edge batch supplied to the current fixed-step callback.
// EngineHost supplies transitions to the first callback for a target tick; a
// catch-up callback receives an empty transition span and therefore cannot
// replay a pointer selection.
inline void consumeTileSelectionTransitions(std::span<const SimulationActionTransition> transitions,
                                             InputActionId selectionAction, TileSelectionGrid grid,
                                             TileSelectionCounters& counters) noexcept
{
    for (const SimulationActionTransition& transition : transitions)
    {
        const auto* actionTransition = std::get_if<InputActionTransition>(&transition);
        if (actionTransition == nullptr || actionTransition->action != selectionAction ||
            actionTransition->kind != InputActionTransitionKind::Started)
        {
            continue;
        }

        ++counters.pointerPresses;
        if (!actionTransition->worldPointerSample.has_value())
        {
            ++counters.missingWorldPointerSamples;
            continue;
        }

        const Render::WorldPointerSample& sample = *actionTransition->worldPointerSample;
        if (!sample.hit)
        {
            ++counters.viewportMisses;
            continue;
        }

        if (grid.widthCells == 0U || grid.heightCells == 0U || !std::isfinite(grid.cellSizeMeters) ||
            grid.cellSizeMeters <= 0.0F || !std::isfinite(sample.worldX) || !std::isfinite(sample.worldY))
        {
            ++counters.mapMisses;
            continue;
        }

        // TileMapInstance uses a bottom-left map-local origin. The upper edge
        // is intentionally exclusive, matching the viewport pick contract.
        const double cellX = static_cast<double>(sample.worldX) / static_cast<double>(grid.cellSizeMeters);
        const double cellY = static_cast<double>(sample.worldY) / static_cast<double>(grid.cellSizeMeters);
        if (!std::isfinite(cellX) || !std::isfinite(cellY) || cellX < 0.0 || cellY < 0.0 ||
            cellX >= static_cast<double>(grid.widthCells) || cellY >= static_cast<double>(grid.heightCells))
        {
            ++counters.mapMisses;
            continue;
        }

        counters.lastSelection = SelectedTile{
            .cellX = static_cast<Core::u32>(std::floor(cellX)),
            .cellY = static_cast<Core::u32>(std::floor(cellY)),
            .worldPointer = sample,
        };
        ++counters.selectionHits;
    }
}

// Builds one translucent overlay sprite for the latest selected cell. Uses the
// same ProductSpriteKey atlas binding as tiles; tint + alpha provide visibility
// without a second texture. Returns structured error on invalid grid/cell;
// callers must not leave a half-emitted selection highlight on failure.
[[nodiscard]] inline Core::Result<Render::RenderSprite2DInput>
makeSelectionHighlightSprite(const SelectedTile& selection, TileSelectionGrid grid, Core::u32 spriteKey) noexcept
{
    if (spriteKey == 0U)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "selection highlight spriteKey must be non-zero");
    }
    if (grid.widthCells == 0U || grid.heightCells == 0U || !std::isfinite(grid.cellSizeMeters) ||
        grid.cellSizeMeters <= 0.0F)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "selection highlight grid is invalid");
    }
    if (selection.cellX >= grid.widthCells || selection.cellY >= grid.heightCells)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "selection highlight cell is out of map bounds");
    }

    const float cell = grid.cellSizeMeters;
    const float size = cell * SelectionHighlightInsetScale;
    return Render::RenderSprite2DInput{
        .spriteKey = spriteKey,
        .stableEntityKey = SelectionHighlightStableEntityKey,
        .centerX = (static_cast<float>(selection.cellX) + 0.5F) * cell,
        .centerY = (static_cast<float>(selection.cellY) + 0.5F) * cell,
        .rotationRadians = 0.0F,
        .widthMeters = size,
        .heightMeters = size,
        .scaleX = 1.0F,
        .scaleY = 1.0F,
        // Full-atlas UV; cyan tint + alpha reads as a solid selection overlay.
        .u0 = 0.0F,
        .v0 = 0.0F,
        .u1 = 1.0F,
        .v1 = 1.0F,
        .sortingLayer = SelectionHighlightSortingLayer,
        .orderInLayer = SelectionHighlightOrderInLayer,
        .red = 80,
        .green = 220,
        .blue = 255,
        .alpha = 160,
        .flipX = false,
        .flipY = false,
        .visible = true,
    };
}

// Hermetic product-gate helper: seed lastSelection from a map cell center as if
// an unconsumed A42 Started+hit worldPointerSample had been consumed. Does not
// re-read Camera/Platform. Returns false on invalid grid/cell (no half-state).
[[nodiscard]] inline bool seedTileSelection(Core::u32 cellX, Core::u32 cellY, TileSelectionGrid grid,
                                            TileSelectionCounters& counters, Core::u64 inputSequence = 1) noexcept
{
    if (grid.widthCells == 0U || grid.heightCells == 0U || !std::isfinite(grid.cellSizeMeters) ||
        grid.cellSizeMeters <= 0.0F)
    {
        return false;
    }
    if (cellX >= grid.widthCells || cellY >= grid.heightCells)
    {
        return false;
    }

    const float cell = grid.cellSizeMeters;
    counters.lastSelection = SelectedTile{
        .cellX = cellX,
        .cellY = cellY,
        .worldPointer =
            Render::WorldPointerSample{
                .worldX = (static_cast<float>(cellX) + 0.5F) * cell,
                .worldY = (static_cast<float>(cellY) + 0.5F) * cell,
                .cameraRevision = 0,
                .surfaceRevision = 0,
                .inputSequence = inputSequence,
                .stableCameraKey = 1,
                .hit = true,
            },
    };
    ++counters.pointerPresses;
    ++counters.selectionHits;
    return true;
}

// Optional path that builds a Started Simulation edge with a locked sample so
// consumeTileSelectionTransitions exercises the same consumer as real A42
// edges. Prefer seedTileSelection for the product CLI gate; this helper is for
// unit tests that assert consumer-path equivalence.
[[nodiscard]] inline Core::Result<InputActionTransition>
makeScriptedWorldCellPress(InputActionId selectionAction, TileSelectionGrid grid, Core::u32 cellX, Core::u32 cellY,
                           Core::u64 sourceSequence = 1) noexcept
{
    if (grid.widthCells == 0U || grid.heightCells == 0U || !std::isfinite(grid.cellSizeMeters) ||
        grid.cellSizeMeters <= 0.0F)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "scripted world click grid is invalid");
    }
    if (cellX >= grid.widthCells || cellY >= grid.heightCells)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "scripted world click cell is out of map bounds");
    }

    const float cell = grid.cellSizeMeters;
    const Render::WorldPointerSample sample{
        .worldX = (static_cast<float>(cellX) + 0.5F) * cell,
        .worldY = (static_cast<float>(cellY) + 0.5F) * cell,
        .cameraRevision = 0,
        .surfaceRevision = 0,
        .inputSequence = sourceSequence,
        .stableCameraKey = 1,
        .hit = true,
    };
    return InputActionTransition{
        .action = selectionAction,
        .kind = InputActionTransitionKind::Started,
        .value = 1.0F,
        .sourceSequence = sourceSequence,
        .worldPointerSample = sample,
    };
}

} // namespace Tina::Sample2D
