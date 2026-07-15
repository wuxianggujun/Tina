#pragma once

// Compatibility facade for the existing Tina codebase. New low-level code should include the
// focused headers under core/base, core/diagnostics, core/error, or core/time directly.

#include "base/Compiler.hpp"
#include "base/EnumFlags.hpp"
#include "base/Platform.hpp"
#include "base/ScopeExit.hpp"
#include "base/Types.hpp"
#include "diagnostics/Assert.hpp"

#include <type_traits>

namespace Tina {

template <bool Enabled, typename Value>
struct EnableIf {
};

template <typename Value>
struct EnableIf<true, Value> {
    using Type = Value;
};

template <typename Value>
inline constexpr bool is_enum_v = std::is_enum_v<Value>;

} // namespace Tina

// Existing engine code still uses EASTL smart-pointer aliases through Core.hpp. This dependency is
// isolated in Tina::CoreLegacy and can be removed incrementally without polluting the new Core target.
#include "Memory.hpp"
