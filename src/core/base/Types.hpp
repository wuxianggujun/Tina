#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Core {

// Legacy-only fixed path-buffer capacity. New Core APIs use UTF-8 std::filesystem paths.
inline constexpr u32 MaxPathLength = 260U;

} // namespace Tina::Core
