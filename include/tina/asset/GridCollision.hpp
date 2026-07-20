#pragma once

#include <tina/asset/TileMapInstance.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <optional>
#include <vector>

namespace Tina::Asset {

// Read-only grid collision SPI (game-2d). Does not expose concrete TileMap class ownership.
// Character controller / gameplay systems depend on this; tina_scene must not.
class IGridCollisionProvider {
  public:
    virtual ~IGridCollisionProvider() = default;

    [[nodiscard]] virtual Core::u32 widthCells() const noexcept = 0;
    [[nodiscard]] virtual Core::u32 heightCells() const noexcept = 0;
    [[nodiscard]] virtual float cellSizeMeters() const noexcept = 0;

    // Material flags for a cell (0 if empty/out of bounds). Uses TilesetWire material bits.
    [[nodiscard]] virtual Core::u16 materialFlagsAt(Core::u32 cellX, Core::u32 cellY) const noexcept = 0;

    // Collects solid cells overlapping AABB. Clears `out` first; returns hit count.
    [[nodiscard]] virtual Core::Result<Core::u32>
    querySolidAabb(const TileMapSolidQuery& query, std::pmr::vector<TileMapSolidHit>& out) const = 0;
};

// Non-owning adapter over TileMapInstance.
class TileMapGridCollision final : public IGridCollisionProvider {
  public:
    explicit TileMapGridCollision(const TileMapInstance& map) noexcept : m_map(&map) {}

    [[nodiscard]] Core::u32 widthCells() const noexcept override
    {
        return m_map->widthCells();
    }
    [[nodiscard]] Core::u32 heightCells() const noexcept override
    {
        return m_map->heightCells();
    }
    [[nodiscard]] float cellSizeMeters() const noexcept override
    {
        return m_map->cellSizeMeters();
    }
    [[nodiscard]] Core::u16 materialFlagsAt(Core::u32 cellX, Core::u32 cellY) const noexcept override;
    [[nodiscard]] Core::Result<Core::u32> querySolidAabb(const TileMapSolidQuery& query,
                                                         std::pmr::vector<TileMapSolidHit>& out) const override;

  private:
    const TileMapInstance* m_map = nullptr;
};

} // namespace Tina::Asset
