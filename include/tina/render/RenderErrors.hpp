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

} // namespace Tina::Render::RenderErrorCode
