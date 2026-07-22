#include <tina/physics2d/PhysicsTypes.hpp>

#include <type_traits>

#if defined(B2_API) || defined(B2_VERSION) || defined(B2_IS_NULL) || defined(B2_NULL_INDEX)
#error "PhysicsTypes.hpp leaked a backend dependency"
#endif

static_assert(Tina::Physics2D::PhysicsWorld2DConfig::DefaultBodyCapacity > 0);
static_assert(Tina::Physics2D::PhysicsWorld2DConfig::DefaultShapeCapacity > 0);
static_assert(std::is_trivially_copyable_v<Tina::Physics2D::PhysicsBodyState2D>);
static_assert(std::is_trivially_copyable_v<Tina::Physics2D::PhysicsWorld2DConfig>);
