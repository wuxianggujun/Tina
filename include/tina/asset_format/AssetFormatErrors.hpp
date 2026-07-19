#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::AssetFormat::AssetFormatErrorCode {

inline constexpr Core::ErrorCode InvalidLimits{Core::ErrorDomain::Asset, 1};
inline constexpr Core::ErrorCode InvalidMagic{Core::ErrorDomain::Asset, 2};
inline constexpr Core::ErrorCode UnsupportedSchema{Core::ErrorDomain::Asset, 3};
inline constexpr Core::ErrorCode InvalidHeader{Core::ErrorDomain::Asset, 4};
inline constexpr Core::ErrorCode UnsupportedValue{Core::ErrorDomain::Asset, 5};
inline constexpr Core::ErrorCode SizeLimitExceeded{Core::ErrorDomain::Asset, 6};
inline constexpr Core::ErrorCode ArithmeticOverflow{Core::ErrorDomain::Asset, 7};
inline constexpr Core::ErrorCode InvalidLayout{Core::ErrorDomain::Asset, 8};
inline constexpr Core::ErrorCode InvalidIdentity{Core::ErrorDomain::Asset, 9};
inline constexpr Core::ErrorCode InvalidDependency{Core::ErrorDomain::Asset, 10};
inline constexpr Core::ErrorCode MissingDependency{Core::ErrorDomain::Asset, 11};
inline constexpr Core::ErrorCode DependencyTypeMismatch{Core::ErrorDomain::Asset, 12};
inline constexpr Core::ErrorCode ContentHashMismatch{Core::ErrorDomain::Asset, 17};

} // namespace Tina::AssetFormat::AssetFormatErrorCode
