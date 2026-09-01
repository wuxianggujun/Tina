#include <tina/animation3d/IkSolver3D.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Animation3D::TwoBoneIkDesc>);
// Full extension is refused by default: at exactly maximum reach the law-of-cosines
// direction is numerically unstable and the joint jitters.
static_assert(Tina::Animation3D::TwoBoneIkDesc{}.maximumReachFraction < 1.0F);
