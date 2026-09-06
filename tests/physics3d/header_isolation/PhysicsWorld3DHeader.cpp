#include <tina/physics3d/PhysicsWorld3D.hpp>

#include <type_traits>

static_assert(std::is_nothrow_move_constructible_v<Tina::Physics3D::PhysicsWorld3D>);
static_assert(!std::is_copy_constructible_v<Tina::Physics3D::PhysicsWorld3D>);
#ifdef JPH_VERSION_ID
#error Physics3D public headers must not expose backend headers
#endif
