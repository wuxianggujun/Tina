#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Asset::AssetErrorCode {

// AssetFormat uses Asset domain values 1-12. CatalogSnapshot starts at 13.
inline constexpr Core::ErrorCode InvalidCatalogConfig{Core::ErrorDomain::Asset, 13};
inline constexpr Core::ErrorCode CatalogCapacityExceeded{Core::ErrorDomain::Asset, 14};
inline constexpr Core::ErrorCode DependencyCycle{Core::ErrorDomain::Asset, 15};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Asset, 16};

} // namespace Tina::Asset::AssetErrorCode
