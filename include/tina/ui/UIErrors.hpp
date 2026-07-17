#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::UI::UIErrorCode {

inline constexpr Core::ErrorCode InvalidContextConfig{Core::ErrorDomain::UI, 1};
inline constexpr Core::ErrorCode InvalidOwnerWindow{Core::ErrorDomain::UI, 2};
inline constexpr Core::ErrorCode InvalidNode{Core::ErrorDomain::UI, 3};
inline constexpr Core::ErrorCode WrongOwnerWindow{Core::ErrorDomain::UI, 4};
inline constexpr Core::ErrorCode WrongContext{Core::ErrorDomain::UI, 5};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::UI, 6};
inline constexpr Core::ErrorCode InvalidParent{Core::ErrorDomain::UI, 7};
inline constexpr Core::ErrorCode RootRequired{Core::ErrorDomain::UI, 8};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::UI, 9};
inline constexpr Core::ErrorCode InvalidLayout{Core::ErrorDomain::UI, 10};
inline constexpr Core::ErrorCode InvalidPointerPolicy{Core::ErrorDomain::UI, 11};
inline constexpr Core::ErrorCode InvalidRoutedPointerListener{Core::ErrorDomain::UI, 12};
inline constexpr Core::ErrorCode InvalidPointerInput{Core::ErrorDomain::UI, 13};
inline constexpr Core::ErrorCode PointerRouteAlreadyInProgress{Core::ErrorDomain::UI, 14};

} // namespace Tina::UI::UIErrorCode
