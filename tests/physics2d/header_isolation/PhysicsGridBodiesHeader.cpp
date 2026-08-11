#include <tina/physics2d/PhysicsGridBodies.hpp>

#include <type_traits>

#if defined(B2_API) || defined(B2_VERSION) || defined(B2_IS_NULL) || defined(B2_NULL_INDEX)
#error "PhysicsGridBodies.hpp leaked a backend dependency"
#endif

static_assert(std::is_trivially_copyable_v<Tina::Physics2D::PhysicsGridSolidRect2D>);
static_assert(std::is_trivially_copyable_v<Tina::Physics2D::PhysicsGridColliderMaterial2D>);
static_assert(std::is_trivially_copyable_v<Tina::Physics2D::PhysicsGridBodyCreateResult2D>);
