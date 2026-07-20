#include <tina/physics2d/PhysicsIds.hpp>

#include <type_traits>

#if defined(B2_API) || defined(B2_VERSION) || defined(B2_IS_NULL) || defined(B2_NULL_INDEX)
#error "PhysicsIds.hpp leaked a backend dependency"
#endif

static_assert(!Tina::Physics2D::PhysicsBodyId{}.hasValue());
static_assert(!Tina::Physics2D::PhysicsShapeId{}.hasValue());
static_assert(!std::is_same_v<Tina::Physics2D::PhysicsBodyId, Tina::Physics2D::PhysicsShapeId>);
