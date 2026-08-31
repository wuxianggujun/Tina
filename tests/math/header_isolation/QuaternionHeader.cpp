#include <tina/math/Quaternion.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Math::Quaternion>);
static_assert(sizeof(Tina::Math::Quaternion) == 16);

// Default construction is identity, not zero: a default-constructed transform has
// to mean "no rotation".
static_assert(Tina::Math::Quaternion{} == Tina::Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F});
static_assert(Tina::Math::lengthSquared(Tina::Math::Quaternion{}) == 1.0F);

// Composition and rotation stay constant-evaluable.
static_assert(Tina::Math::Quaternion{} * Tina::Math::Quaternion{} == Tina::Math::Quaternion{});
static_assert(Tina::Math::conjugate(Tina::Math::Quaternion{0.5F, 0.5F, 0.5F, 0.5F})
              == Tina::Math::Quaternion{-0.5F, -0.5F, -0.5F, 0.5F});
static_assert(Tina::Math::rotate(Tina::Math::Quaternion{}, Tina::Math::Vec3{1.0F, 2.0F, 3.0F})
              == Tina::Math::Vec3{1.0F, 2.0F, 3.0F});
