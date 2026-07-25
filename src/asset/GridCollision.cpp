#include <tina/asset/GridCollision.hpp>

#include <tina/asset/AssetErrors.hpp>

namespace Tina::Asset {

Core::u16 TileMapGridCollision::materialFlagsAt(Core::u32 cellX, Core::u32 cellY) const noexcept
{
    if (m_map == nullptr)
    {
        return 0;
    }
    const auto info = m_map->tileInfoAt(m_layerId, cellX, cellY);
    if (!info || !*info || (*info)->empty)
    {
        return 0;
    }
    return (*info)->materialFlags;
}

Core::Result<Core::u32> TileMapGridCollision::querySolidAabb(const TileMapSolidQuery& query,
                                                            std::pmr::vector<TileMapSolidHit>& out) const
{
    if (m_map == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "grid collision has no map");
    }
    return m_map->querySolidAabb(m_layerId, query, out);
}

} // namespace Tina::Asset
