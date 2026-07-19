#include <tina/asset/AssetErrors.hpp>

static_assert(Tina::Asset::AssetErrorCode::InvalidCatalogConfig.value == 13);
static_assert(Tina::Asset::AssetErrorCode::DependencyCycle.value == 15);
