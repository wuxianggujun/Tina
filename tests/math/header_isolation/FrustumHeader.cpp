#include <tina/math/Frustum.hpp>

#include <optional>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Math::Frustum>);
static_assert(Tina::Math::FrustumPlaneCount == 6U);
static_assert(sizeof(Tina::Math::Frustum) == 6U * sizeof(Tina::Math::Plane));

// Plane order is part of the published contract: callers index planes() by enum.
static_assert(static_cast<Tina::usize>(Tina::Math::FrustumPlane::Left) == 0U);
static_assert(static_cast<Tina::usize>(Tina::Math::FrustumPlane::Right) == 1U);
static_assert(static_cast<Tina::usize>(Tina::Math::FrustumPlane::Bottom) == 2U);
static_assert(static_cast<Tina::usize>(Tina::Math::FrustumPlane::Top) == 3U);
static_assert(static_cast<Tina::usize>(Tina::Math::FrustumPlane::Near) == 4U);
static_assert(static_cast<Tina::usize>(Tina::Math::FrustumPlane::Far) == 5U);

// Construction can fail on a degenerate basis, so it returns optional; the culling
// tests themselves cannot fail and return bool.
static_assert(std::is_same_v<
              decltype(Tina::Math::frustumFromPerspective(
                  Tina::Math::Vec3{}, Tina::Math::Vec3{}, Tina::Math::Vec3{},
                  0.0F, 0.0F, 0.0F, 0.0F)),
              std::optional<Tina::Math::Frustum>>);
static_assert(std::is_same_v<
              decltype(Tina::Math::frustumFromViewProjection(
                  Tina::Math::Mat4{}, Tina::Math::ClipDepthRange::ZeroToOne)),
              std::optional<Tina::Math::Frustum>>);
static_assert(std::is_same_v<
              decltype(Tina::Math::intersects(Tina::Math::Frustum{}, Tina::Math::Sphere{})),
              bool>);
static_assert(std::is_same_v<
              decltype(Tina::Math::intersects(Tina::Math::Frustum{}, Tina::Math::Aabb3{})),
              bool>);
static_assert(noexcept(Tina::Math::intersects(Tina::Math::Frustum{}, Tina::Math::Sphere{})));
static_assert(noexcept(Tina::Math::sphereIntersectsPerspectiveFrustum(
    Tina::Math::Vec3{}, 0.0F, Tina::Math::Vec3{}, Tina::Math::Vec3{}, Tina::Math::Vec3{},
    0.0F, 0.0F, 0.0F, 0.0F)));
