#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Render::RenderErrorCode {

inline constexpr Core::ErrorCode DeviceStopped{Core::ErrorDomain::Render, 1};
inline constexpr Core::ErrorCode FrameAlreadyOpen{Core::ErrorDomain::Render, 2};
inline constexpr Core::ErrorCode NoFrameSubmitted{Core::ErrorDomain::Render, 3};
inline constexpr Core::ErrorCode UnexpectedFrameIndex{Core::ErrorDomain::Render, 4};
inline constexpr Core::ErrorCode InvalidSurfaceState{Core::ErrorDomain::Render, 5};
inline constexpr Core::ErrorCode SurfaceReconfigureFailed{Core::ErrorDomain::Render, 6};
inline constexpr Core::ErrorCode NativeWindowBindingChangedUnsupported{Core::ErrorDomain::Render, 7};
inline constexpr Core::ErrorCode DeviceInitializationFailed{Core::ErrorDomain::Render, 8};
inline constexpr Core::ErrorCode InvalidNativeWindowBinding{Core::ErrorDomain::Render, 9};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Render, 10};
inline constexpr Core::ErrorCode InvalidDisplayListCapacity{Core::ErrorDomain::Render, 11};
inline constexpr Core::ErrorCode DisplayListStorageAllocationFailed{Core::ErrorDomain::Render, 12};
inline constexpr Core::ErrorCode DisplayListBuildAlreadyOpen{Core::ErrorDomain::Render, 13};
inline constexpr Core::ErrorCode DisplayListBuildNotOpen{Core::ErrorDomain::Render, 14};
inline constexpr Core::ErrorCode DisplayListCapacityExceeded{Core::ErrorDomain::Render, 15};
inline constexpr Core::ErrorCode InvalidPremultipliedColor{Core::ErrorDomain::Render, 16};
inline constexpr Core::ErrorCode InvalidDrawCommand{Core::ErrorDomain::Render, 17};
inline constexpr Core::ErrorCode TransientBufferCapacityExceeded{Core::ErrorDomain::Render, 18};
inline constexpr Core::ErrorCode InvalidRenderSceneCapacity{Core::ErrorDomain::Render, 19};
inline constexpr Core::ErrorCode RenderSceneStorageAllocationFailed{Core::ErrorDomain::Render, 20};
inline constexpr Core::ErrorCode RenderSceneBuildAlreadyOpen{Core::ErrorDomain::Render, 21};
inline constexpr Core::ErrorCode RenderSceneBuildNotOpen{Core::ErrorDomain::Render, 22};
inline constexpr Core::ErrorCode RenderSceneCapacityExceeded{Core::ErrorDomain::Render, 23};
inline constexpr Core::ErrorCode InvalidRenderSceneInput{Core::ErrorDomain::Render, 24};
inline constexpr Core::ErrorCode RenderSceneCameraConflict{Core::ErrorDomain::Render, 25};
inline constexpr Core::ErrorCode RenderSceneMissingCamera{Core::ErrorDomain::Render, 26};

} // namespace Tina::Render::RenderErrorCode
