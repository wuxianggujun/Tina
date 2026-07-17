#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

// Whether a committed, effectively visible node may become the target of a
// pointer hit. Ignore only excludes the node itself; targetable descendants
// remain eligible and the ignored node stays in the committed route ancestry.
enum class UIPointerHitPolicy : u8 {
    Ignore,
    Targetable,
};

} // namespace Tina::UI
