#include <tina/asset_format/MaterialPayload.hpp>

// Header isolation: MaterialPayload.hpp must compile without third-party tokens.
static_assert(Tina::AssetFormat::MaterialWire::HeaderBytes == 24U);
