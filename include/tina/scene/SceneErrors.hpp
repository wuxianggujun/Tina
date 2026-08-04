#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Scene {

namespace SceneErrorCode {

inline constexpr Core::ErrorCode InvalidEntity{Core::ErrorDomain::Scene, 1};
inline constexpr Core::ErrorCode WrongWorld{Core::ErrorDomain::Scene, 2};
inline constexpr Core::ErrorCode StaleEntity{Core::ErrorDomain::Scene, 3};
inline constexpr Core::ErrorCode HierarchyCycle{Core::ErrorDomain::Scene, 4};
inline constexpr Core::ErrorCode InvalidTransform{Core::ErrorDomain::Scene, 5};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Scene, 6};
inline constexpr Core::ErrorCode CorruptHierarchy{Core::ErrorDomain::Scene, 7};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Scene, 8};
inline constexpr Core::ErrorCode TransformOverflow{Core::ErrorDomain::Scene, 9};
inline constexpr Core::ErrorCode UnsupportedTransformComposition{Core::ErrorDomain::Scene, 10};
inline constexpr Core::ErrorCode ConstructionFailed{Core::ErrorDomain::Scene, 11};
inline constexpr Core::ErrorCode InvalidComponent{Core::ErrorDomain::Scene, 12};
inline constexpr Core::ErrorCode MultipleActiveCameras{Core::ErrorDomain::Scene, 13};
inline constexpr Core::ErrorCode UnresolvedSprite{Core::ErrorDomain::Scene, 14};
inline constexpr Core::ErrorCode UnresolvedMesh{Core::ErrorDomain::Scene, 15};
inline constexpr Core::ErrorCode InvalidAnimation{Core::ErrorDomain::Scene, 16};
inline constexpr Core::ErrorCode TooManyActiveDirectionalLights{Core::ErrorDomain::Scene, 17};
inline constexpr Core::ErrorCode TooManyActivePointLights2D{Core::ErrorDomain::Scene, 18};
inline constexpr Core::ErrorCode TooManyActiveShadowOccluders2D{Core::ErrorDomain::Scene, 19};
inline constexpr Core::ErrorCode TooManyActivePointLights3D{Core::ErrorDomain::Scene, 20};
inline constexpr Core::ErrorCode TooManyActiveSpotLights3D{Core::ErrorDomain::Scene, 21};
inline constexpr Core::ErrorCode TooManyActiveCascadedDirectionalShadows{Core::ErrorDomain::Scene, 22};

} // namespace SceneErrorCode

} // namespace Tina::Scene
