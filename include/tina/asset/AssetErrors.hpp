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
inline constexpr Core::ErrorCode TileMapLayerNotFound{Core::ErrorDomain::Asset, 27};
inline constexpr Core::ErrorCode TileMapLayerTypeMismatch{Core::ErrorDomain::Asset, 28};
inline constexpr Core::ErrorCode TileMapChunkNotResident{Core::ErrorDomain::Asset, 29};
inline constexpr Core::ErrorCode SpriteBindingCapacityExceeded{Core::ErrorDomain::Asset, 30};
inline constexpr Core::ErrorCode SpriteBindingKeyExhausted{Core::ErrorDomain::Asset, 31};
inline constexpr Core::ErrorCode SpriteBindingConflict{Core::ErrorDomain::Asset, 32};
inline constexpr Core::ErrorCode SpriteBindingNotFound{Core::ErrorDomain::Asset, 33};
inline constexpr Core::ErrorCode Mesh3DBindingCapacityExceeded{Core::ErrorDomain::Asset, 34};
inline constexpr Core::ErrorCode Mesh3DBindingConflict{Core::ErrorDomain::Asset, 35};
inline constexpr Core::ErrorCode Mesh3DBindingNotFound{Core::ErrorDomain::Asset, 36};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Asset, 37};
inline constexpr Core::ErrorCode CatalogReloadBusy{Core::ErrorDomain::Asset, 38};
inline constexpr Core::ErrorCode SourceImportPlanCapacityExceeded{Core::ErrorDomain::Asset, 39};
inline constexpr Core::ErrorCode SourceImportCatalogMismatch{Core::ErrorDomain::Asset, 40};
inline constexpr Core::ErrorCode SourceImportCaptureCapacityExceeded{Core::ErrorDomain::Asset, 41};
inline constexpr Core::ErrorCode SourceImportCancelled{Core::ErrorDomain::Asset, 42};
inline constexpr Core::ErrorCode SourceImportTargetPlatformMismatch{Core::ErrorDomain::Asset, 43};
inline constexpr Core::ErrorCode TileMapPhysicsCapacityExceeded{Core::ErrorDomain::Asset, 44};
inline constexpr Core::ErrorCode PhysicsNavigationCapacityExceeded{Core::ErrorDomain::Asset, 45};
inline constexpr Core::ErrorCode PhysicsNavigationContractMismatch{Core::ErrorDomain::Asset, 46};
inline constexpr Core::ErrorCode PhysicsNavigationRegistrationNotFound{Core::ErrorDomain::Asset, 47};
// The shader compiler rejected a custom shader. nativeCode carries its exit code and the message
// its verbatim diagnostics, since an author needs the reported line rather than a summary.
inline constexpr Core::ErrorCode ShaderCompileFailed{Core::ErrorDomain::Asset, 48};
inline constexpr Core::ErrorCode ShaderBindingCapacityExceeded{Core::ErrorDomain::Asset, 49};
inline constexpr Core::ErrorCode ShaderBindingKeyExhausted{Core::ErrorDomain::Asset, 50};
inline constexpr Core::ErrorCode ShaderBindingConflict{Core::ErrorDomain::Asset, 51};
inline constexpr Core::ErrorCode ShaderBindingNotFound{Core::ErrorDomain::Asset, 52};
inline constexpr Core::ErrorCode ShaderUniformBindingKeyExhausted{Core::ErrorDomain::Asset, 53};

} // namespace Tina::Asset::AssetErrorCode
