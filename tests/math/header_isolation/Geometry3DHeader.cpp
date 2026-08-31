#include <tina/math/Geometry3D.hpp>

#include <optional>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Math::Aabb3>);
static_assert(std::is_trivially_copyable_v<Tina::Math::Sphere>);
static_assert(std::is_trivially_copyable_v<Tina::Math::Plane>);
static_assert(std::is_trivially_copyable_v<Tina::Math::Ray>);
static_assert(sizeof(Tina::Math::Aabb3) == 24);
static_assert(sizeof(Tina::Math::Sphere) == 16);
static_assert(sizeof(Tina::Math::Plane) == 16);
static_assert(sizeof(Tina::Math::Ray) == 24);

// Queries report absence through optional/bool rather than Core::Result: a miss is
// a normal outcome in a per-frame culling loop, and Core::Error allocates.
static_assert(std::is_same_v<
              decltype(Tina::Math::raycast(Tina::Math::Ray{}, Tina::Math::Aabb3{})),
              std::optional<Tina::Math::RayHit>>);
static_assert(std::is_same_v<
              decltype(Tina::Math::raycast(Tina::Math::Ray{}, Tina::Math::Sphere{})),
              std::optional<Tina::Math::RayHit>>);
static_assert(std::is_same_v<
              decltype(Tina::Math::raycast(Tina::Math::Ray{}, Tina::Math::Plane{})),
              std::optional<Tina::Math::RayHit>>);
static_assert(std::is_same_v<
              decltype(Tina::Math::makeRay(Tina::Math::Vec3{}, Tina::Math::Vec3{})),
              std::optional<Tina::Math::Ray>>);
static_assert(noexcept(Tina::Math::raycast(Tina::Math::Ray{}, Tina::Math::Sphere{})));

// Plane is constant-normal form: dot(normal, point) + distance.
static_assert(Tina::Math::signedDistance(Tina::Math::Plane{{0.0F, 1.0F, 0.0F}, -2.0F},
                                         Tina::Math::Vec3{5.0F, 3.0F, 7.0F})
              == 1.0F);

static_assert(Tina::Math::center(Tina::Math::Aabb3{{0.0F, 0.0F, 0.0F}, {2.0F, 4.0F, 6.0F}})
              == Tina::Math::Vec3{1.0F, 2.0F, 3.0F});
static_assert(Tina::Math::contains(Tina::Math::Sphere{{0.0F, 0.0F, 0.0F}, 2.0F},
                                   Tina::Math::Vec3{0.0F, 2.0F, 0.0F}));
// Closest point of the unit box to (3,0,0) is (1,0,0), so the gap is exactly 2.
static_assert(Tina::Math::intersects(Tina::Math::Sphere{{3.0F, 0.0F, 0.0F}, 2.0F},
                                     Tina::Math::Aabb3{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}}));
static_assert(!Tina::Math::intersects(Tina::Math::Sphere{{3.0F, 0.0F, 0.0F}, 1.5F},
                                      Tina::Math::Aabb3{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}}));
