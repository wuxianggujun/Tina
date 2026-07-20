#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Asset::AssetErrorCode {

// AssetFormat uses Asset domain values 1-12. CatalogSnapshot starts at 13.
inline constexpr Core::ErrorCode InvalidCatalogConfig{Core::ErrorDomain::Asset, 13};
inline constexpr Core::ErrorCode CatalogCapacityExceeded{Core::ErrorDomain::Asset, 14};
inline constexpr Core::ErrorCode DependencyCycle{Core::ErrorDomain::Asset, 15};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Asset, 16};
// AssetFormat ContentHashMismatch uses 17.
inline constexpr Core::ErrorCode CatalogFileLoadFailed{Core::ErrorDomain::Asset, 18};
inline constexpr Core::ErrorCode CatalogEntryMismatch{Core::ErrorDomain::Asset, 19};
// M10-A3 CPU Handle/Lease registry (ADR 0016 first slice).
inline constexpr Core::ErrorCode InvalidHandle{Core::ErrorDomain::Asset, 20};
inline constexpr Core::ErrorCode AssetNotReady{Core::ErrorDomain::Asset, 21};
inline constexpr Core::ErrorCode AssetUnloaded{Core::ErrorDomain::Asset, 22};
inline constexpr Core::ErrorCode LeaseCountOverflow{Core::ErrorDomain::Asset, 23};
inline constexpr Core::ErrorCode AssetFailed{Core::ErrorDomain::Asset, 24};
inline constexpr Core::ErrorCode AssetQueueFull{Core::ErrorDomain::Asset, 25};
inline constexpr Core::ErrorCode AssetUploadFailed{Core::ErrorDomain::Asset, 26};

} // namespace Tina::Asset::AssetErrorCode
