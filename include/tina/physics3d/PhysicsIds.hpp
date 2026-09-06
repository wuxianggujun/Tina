#pragma once

#include <tina/core/id/GenerationId.hpp>

namespace Tina::Physics3D {

namespace Detail {
struct PhysicsBodyRegistryTag final {};
} // namespace Detail

// Runtime-only identity, checked against both its world owner and generation.
using PhysicsBodyId = Core::GenerationId<Detail::PhysicsBodyRegistryTag>;

} // namespace Tina::Physics3D
