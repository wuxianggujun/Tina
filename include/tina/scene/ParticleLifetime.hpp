#pragma once

#include <tina/core/time/MonotonicClock.hpp>

namespace Tina::Scene {

// Shared by the 2D and 3D particle systems: each draws one per-particle lifetime
// from this inclusive range. Declared here rather than inside either system so the
// two cannot define the same name in Tina::Scene with two different definitions.
struct ParticleLifetimeRange final {
    Core::Duration minimum{1.0};
    Core::Duration maximum{1.0};
};

} // namespace Tina::Scene
