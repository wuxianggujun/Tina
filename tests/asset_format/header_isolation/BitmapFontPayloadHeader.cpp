#include <tina/asset_format/BitmapFontPayload.hpp>

static_assert(Tina::AssetFormat::BitmapFontWire::SchemaVersion == 1);
static_assert(Tina::AssetFormat::BitmapFontWire::HeaderBytes == 32);
static_assert(Tina::AssetFormat::BitmapFontWire::GlyphBytes == 36);
