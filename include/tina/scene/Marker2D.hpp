#pragma once

namespace Tina::Scene {

// Authored identity for a transform-only marker (for example a spawn point).
// No render, physics or resource payload is implied. World2D capture rejects
// combining this tag with payload-bearing components instead of losing its kind.
struct Marker2D final {
};

} // namespace Tina::Scene
