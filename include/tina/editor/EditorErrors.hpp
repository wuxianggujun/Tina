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

} // namespace Tina::Editor::EditorErrorCode
