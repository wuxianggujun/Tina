#include <tina/asset_format/MaterialPayload.hpp>

// Header isolation: MaterialPayload.hpp must compile without third-party tokens.
static_assert(Tina::AssetFormat::MaterialWire::SchemaVersion == 3U);
static_assert(Tina::AssetFormat::MaterialWire::HeaderBytes == 48U);
static_assert(Tina::AssetFormat::MaterialWire::TextureRoleCount == 4U);
