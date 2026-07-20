#pragma once

#include <tina/core/id/GenerationId.hpp>

namespace Tina::Physics2D {

namespace Detail {
struct PhysicsBodyRegistryTag final {
};

struct PhysicsShapeRegistryTag final {
};
} // namespace Detail

// Runtime-only identities. PhysicsWorld2D validates both the world owner and
// generation before resolving either handle.
using PhysicsBodyId = Core::GenerationId<Detail::PhysicsBodyRegistryTag>;
using PhysicsShapeId = Core::GenerationId<Detail::PhysicsShapeRegistryTag>;

} // namespace Tina::Physics2D
