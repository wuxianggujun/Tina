#include <tina/physics2d/PhysicsErrors.hpp>

#if defined(B2_API) || defined(B2_VERSION) || defined(B2_IS_NULL) || defined(B2_NULL_INDEX)
#error "PhysicsErrors.hpp leaked a backend dependency"
#endif

static_assert(
    Tina::Physics2D::Physics2DErrorCode::InvalidConfiguration.domain
    == Tina::Core::ErrorDomain::Physics2D);
static_assert(
    Tina::Physics2D::Physics2DErrorCode::WrongWorld.domain
    == Tina::Core::ErrorDomain::Physics2D);
