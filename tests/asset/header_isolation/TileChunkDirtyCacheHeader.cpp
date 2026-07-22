#include <tina/asset/TileChunkDirtyCache.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Asset::TileChunkDirtyCache>);
static_assert(std::is_move_constructible_v<Tina::Asset::TileChunkDirtyCache>);
