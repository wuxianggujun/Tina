#include <tina/asset_format/AssetFormatErrors.hpp>

static_assert(Tina::AssetFormat::AssetFormatErrorCode::InvalidMagic.domain ==
              Tina::Core::ErrorDomain::AssetFormat);
static_assert(Tina::AssetFormat::AssetFormatErrorCode::ContentHashMismatch.domain ==
              Tina::Core::ErrorDomain::AssetFormat);
