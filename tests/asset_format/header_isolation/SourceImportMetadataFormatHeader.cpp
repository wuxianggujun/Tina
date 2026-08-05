#include <tina/asset_format/SourceImportMetadataFormat.hpp>

static_assert(Tina::AssetFormat::SourceImportWire::HeaderBytes == 144);
static_assert(sizeof(Tina::AssetFormat::SourceImportUnitId::Bytes) == 16);
