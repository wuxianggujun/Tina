#pragma once

#include <tina/core/base/Types.hpp>
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
        const auto* digital = std::get_if<DigitalActionTransition>(&transition);
        if (digital == nullptr || digital->action != selectionAction ||
            digital->kind != DigitalActionTransitionKind::Pressed)
        {
            continue;
        }

        ++counters.pointerPresses;
        if (!digital->worldPointerSample.has_value())
        {
            ++counters.missingWorldPointerSamples;
            continue;
        }

        const Render::WorldPointerSample& sample = *digital->worldPointerSample;
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

} // namespace Tina::Sample2D
