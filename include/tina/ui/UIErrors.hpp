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
inline constexpr Core::ErrorCode InvalidButtonAction{Core::ErrorDomain::UI, 15};
inline constexpr Core::ErrorCode InvalidText{Core::ErrorDomain::UI, 16};
inline constexpr Core::ErrorCode InvalidFont{Core::ErrorDomain::UI, 17};
// Reserved for atlas miss/stale glyph identity (find failures use InvalidNode
// when a glyph placement is absent; keep a dedicated code for future eviction).
inline constexpr Core::ErrorCode InvalidGlyph{Core::ErrorDomain::UI, 18};
inline constexpr Core::ErrorCode InvalidControlValue{Core::ErrorDomain::UI, 19};
inline constexpr Core::ErrorCode AccessibilityTreeMissing{Core::ErrorDomain::UI, 20};
inline constexpr Core::ErrorCode AccessibilityNodeStale{Core::ErrorDomain::UI, 21};
inline constexpr Core::ErrorCode InvalidTheme{Core::ErrorDomain::UI, 22};
inline constexpr Core::ErrorCode InvalidFocusScope{Core::ErrorDomain::UI, 23};
inline constexpr Core::ErrorCode InvalidFocusTarget{Core::ErrorDomain::UI, 24};
inline constexpr Core::ErrorCode InvalidAccessibilityAction{Core::ErrorDomain::UI, 25};

} // namespace Tina::UI::UIErrorCode
