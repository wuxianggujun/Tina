#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Save::SaveErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Save, 1};
inline constexpr Core::ErrorCode InvalidSlot{Core::ErrorDomain::Save, 2};
inline constexpr Core::ErrorCode InvalidMetadata{Core::ErrorDomain::Save, 3};
inline constexpr Core::ErrorCode PayloadTooLarge{Core::ErrorDomain::Save, 4};
inline constexpr Core::ErrorCode SlotNotFound{Core::ErrorDomain::Save, 5};
inline constexpr Core::ErrorCode CorruptData{Core::ErrorDomain::Save, 6};
inline constexpr Core::ErrorCode UnsupportedSchema{Core::ErrorDomain::Save, 7};
inline constexpr Core::ErrorCode WrongGameId{Core::ErrorDomain::Save, 8};
inline constexpr Core::ErrorCode Busy{Core::ErrorDomain::Save, 9};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Save, 10};
inline constexpr Core::ErrorCode AsyncUnavailable{Core::ErrorDomain::Save, 11};
inline constexpr Core::ErrorCode OperationNotReady{Core::ErrorDomain::Save, 12};
inline constexpr Core::ErrorCode OperationAlreadyTaken{Core::ErrorDomain::Save, 13};
inline constexpr Core::ErrorCode InvalidOperation{Core::ErrorDomain::Save, 14};
inline constexpr Core::ErrorCode RevisionOverflow{Core::ErrorDomain::Save, 15};
inline constexpr Core::ErrorCode BackupUnavailable{Core::ErrorDomain::Save, 16};
inline constexpr Core::ErrorCode InvalidMigrationVersion{Core::ErrorDomain::Save, 17};
inline constexpr Core::ErrorCode DuplicateMigrationStep{Core::ErrorDomain::Save, 18};
inline constexpr Core::ErrorCode MigrationPathMissing{Core::ErrorDomain::Save, 19};
inline constexpr Core::ErrorCode MigrationFailed{Core::ErrorDomain::Save, 20};

} // namespace Tina::Save::SaveErrorCode
