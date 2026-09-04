#include <tina/asset_format/ShaderPayload.hpp>

static_assert(Tina::AssetFormat::ShaderWire::HeaderBytes == 16U);
static_assert(Tina::AssetFormat::ShaderWire::BlobEntryBytes == 12U);
static_assert(Tina::AssetFormat::ShaderWire::SchemaVersion == 1U);
