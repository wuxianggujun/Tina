#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Editor::EditorErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Editor, 1};
inline constexpr Core::ErrorCode DocumentCapacityExceeded{Core::ErrorDomain::Editor, 2};
inline constexpr Core::ErrorCode HistoryCapacityExceeded{Core::ErrorDomain::Editor, 3};
inline constexpr Core::ErrorCode UndoUnavailable{Core::ErrorDomain::Editor, 4};
inline constexpr Core::ErrorCode RedoUnavailable{Core::ErrorDomain::Editor, 5};
inline constexpr Core::ErrorCode EntityNotFound{Core::ErrorDomain::Editor, 6};
inline constexpr Core::ErrorCode LayerNotFound{Core::ErrorDomain::Editor, 7};
inline constexpr Core::ErrorCode ObjectNotFound{Core::ErrorDomain::Editor, 8};
inline constexpr Core::ErrorCode InvalidAuthoringOperation{Core::ErrorDomain::Editor, 9};
inline constexpr Core::ErrorCode FrameNotFound{Core::ErrorDomain::Editor, 10};
inline constexpr Core::ErrorCode ProjectAssetCapacityExceeded{Core::ErrorDomain::Editor, 11};
inline constexpr Core::ErrorCode DocumentTabCapacityExceeded{Core::ErrorDomain::Editor, 12};
inline constexpr Core::ErrorCode DocumentTabNotFound{Core::ErrorDomain::Editor, 13};
inline constexpr Core::ErrorCode DirtyDocumentRequiresConfirmation{Core::ErrorDomain::Editor, 14};
inline constexpr Core::ErrorCode PinnedDocumentCannotClose{Core::ErrorDomain::Editor, 15};
inline constexpr Core::ErrorCode ProjectAssetNotFound{Core::ErrorDomain::Editor, 16};
inline constexpr Core::ErrorCode UnknownGameplayArchetype{Core::ErrorDomain::Editor, 17};
inline constexpr Core::ErrorCode DuplicateGameplayArchetype{Core::ErrorDomain::Editor, 18};
inline constexpr Core::ErrorCode ComponentAlreadyPresent{Core::ErrorDomain::Editor, 19};
inline constexpr Core::ErrorCode ComponentNotFound{Core::ErrorDomain::Editor, 20};

} // namespace Tina::Editor::EditorErrorCode
