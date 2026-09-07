#include <tina/physics3d/PhysicsTypes.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Physics3D::PhysicsBodyState3D>);
static_assert(std::is_trivially_copyable_v<Tina::Physics3D::PhysicsOriginShift3D>);
