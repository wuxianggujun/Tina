#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <type_traits>

#if defined(B2_API) || defined(B2_VERSION) || defined(B2_IS_NULL) || defined(B2_NULL_INDEX)
#error "PhysicsWorld2D.hpp leaked a backend dependency"
#endif

static_assert(std::is_move_constructible_v<Tina::Physics2D::PhysicsWorld2D>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Physics2D::PhysicsWorld2D>);
static_assert(!std::is_copy_constructible_v<Tina::Physics2D::PhysicsWorld2D>);
