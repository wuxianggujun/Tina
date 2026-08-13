#include <tina/asset_format/Fx2DPayload.hpp>

#include <type_traits>

static_assert(Tina::AssetFormat::Fx2DWire::PayloadBytes == 184U);
static_assert(std::is_trivially_copyable_v<Tina::AssetFormat::Fx2DPayloadDesc>);
