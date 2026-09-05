#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Localization::LocalizationErrorCode {

// Structural defects in the ingested table that are not covered by a more specific code
// (no entries at all, an entry count that cannot be addressed by a slot index, ...).
inline constexpr Core::ErrorCode InvalidData{Core::ErrorDomain::Localization, 1};
// The locale tag is empty, too long, or not the accepted BCP 47 subset.
inline constexpr Core::ErrorCode InvalidLocaleTag{Core::ErrorDomain::Localization, 2};
// Entries are not strictly ascending by key hash. Duplicates land here too: a duplicate key
// makes lookup order-dependent, so it fails closed instead of resolving to whichever came first.
inline constexpr Core::ErrorCode UnsortedTable{Core::ErrorDomain::Localization, 3};
// An entry's [textOffset, textOffset + textLength) does not lie inside the text blob, or the
// absent-translation encoding was used with a non-zero length.
inline constexpr Core::ErrorCode InvalidTextRange{Core::ErrorDomain::Localization, 4};
// An entry's text slice is not strict UTF-8, or contains U+0000.
inline constexpr Core::ErrorCode InvalidTextEncoding{Core::ErrorDomain::Localization, 5};
// The configured fixed capacity cannot hold the table, or the requested capacity itself is
// outside the supported range. Never resolved by allocating more.
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Localization, 6};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Localization, 7};
// The id is default-constructed, or its catalog identity is not this catalog's. A stale id from
// a destroyed catalog also lands here: identities are never recycled.
inline constexpr Core::ErrorCode InvalidTextId{Core::ErrorDomain::Localization, 8};
// The key exists in the table but this locale carries no translation for it. Distinct from
// InvalidTextId (unknown key) and from a present-but-empty string, which resolves successfully.
inline constexpr Core::ErrorCode MissingText{Core::ErrorDomain::Localization, 9};
// The process-wide catalog identity space is exhausted, so a new catalog could not be given an
// identity that is distinguishable from every previous one.
inline constexpr Core::ErrorCode IdentityExhausted{Core::ErrorDomain::Localization, 10};

} // namespace Tina::Localization::LocalizationErrorCode
