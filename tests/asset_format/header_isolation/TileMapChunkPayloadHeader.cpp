#include <tina/asset_format/TileMapChunkPayload.hpp>

static_assert(Tina::AssetFormat::TileMapChunkWire::HeaderBytes == 48);
static_assert(Tina::AssetFormat::TileMapChunkWire::MaxCells == 4096);
