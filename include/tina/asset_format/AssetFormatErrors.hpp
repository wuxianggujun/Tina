#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::AssetFormat::AssetFormatErrorCode {

inline constexpr Core::ErrorCode InvalidLimits{Core::ErrorDomain::AssetFormat, 1};
inline constexpr Core::ErrorCode InvalidMagic{Core::ErrorDomain::AssetFormat, 2};
inline constexpr Core::ErrorCode UnsupportedSchema{Core::ErrorDomain::AssetFormat, 3};
inline constexpr Core::ErrorCode InvalidHeader{Core::ErrorDomain::AssetFormat, 4};
inline constexpr Core::ErrorCode UnsupportedValue{Core::ErrorDomain::AssetFormat, 5};
inline constexpr Core::ErrorCode SizeLimitExceeded{Core::ErrorDomain::AssetFormat, 6};
inline constexpr Core::ErrorCode ArithmeticOverflow{Core::ErrorDomain::AssetFormat, 7};
inline constexpr Core::ErrorCode InvalidLayout{Core::ErrorDomain::AssetFormat, 8};
inline constexpr Core::ErrorCode InvalidIdentity{Core::ErrorDomain::AssetFormat, 9};
inline constexpr Core::ErrorCode InvalidDependency{Core::ErrorDomain::AssetFormat, 10};
inline constexpr Core::ErrorCode MissingDependency{Core::ErrorDomain::AssetFormat, 11};
inline constexpr Core::ErrorCode DependencyTypeMismatch{Core::ErrorDomain::AssetFormat, 12};
// Value 17 is historical: it sat in a hole of the Asset domain when both
// modules shared that domain. It is not reused; new AssetFormat codes continue
// from 13, skipping nothing else.
inline constexpr Core::ErrorCode ContentHashMismatch{Core::ErrorDomain::AssetFormat, 17};

} // namespace Tina::AssetFormat::AssetFormatErrorCode
