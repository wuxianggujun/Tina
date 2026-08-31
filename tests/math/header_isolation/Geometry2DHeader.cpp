#include <tina/math/Geometry2D.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Math::Aabb2>);
static_assert(std::is_trivially_copyable_v<Tina::Math::Rect>);
static_assert(sizeof(Tina::Math::Aabb2) == 16);
static_assert(sizeof(Tina::Math::Rect) == 16);

// Aabb2 stores opposite corners; Rect stores position plus extent. Same size, so
// only the field semantics distinguish them — hence the explicit conversions.
static_assert(Tina::Math::toAabb2(Tina::Math::Rect{1.0F, 2.0F, 3.0F, 4.0F})
              == Tina::Math::Aabb2{{1.0F, 2.0F}, {4.0F, 6.0F}});
static_assert(Tina::Math::toRect(Tina::Math::Aabb2{{1.0F, 2.0F}, {4.0F, 6.0F}})
              == Tina::Math::Rect{1.0F, 2.0F, 3.0F, 4.0F});

static_assert(Tina::Math::center(Tina::Math::Aabb2{{0.0F, 0.0F}, {2.0F, 4.0F}})
              == Tina::Math::Vec2{1.0F, 2.0F});
static_assert(Tina::Math::area(Tina::Math::Aabb2{{0.0F, 0.0F}, {2.0F, 4.0F}}) == 8.0F);

// Boundary counts as inside, and touching boxes intersect.
static_assert(Tina::Math::contains(Tina::Math::Aabb2{{0.0F, 0.0F}, {1.0F, 1.0F}},
                                   Tina::Math::Vec2{1.0F, 1.0F}));
static_assert(Tina::Math::intersects(Tina::Math::Aabb2{{0.0F, 0.0F}, {1.0F, 1.0F}},
                                     Tina::Math::Aabb2{{1.0F, 0.0F}, {2.0F, 1.0F}}));
static_assert(!Tina::Math::intersects(Tina::Math::Aabb2{{0.0F, 0.0F}, {1.0F, 1.0F}},
                                      Tina::Math::Aabb2{{1.5F, 0.0F}, {2.0F, 1.0F}}));

// fromPoints normalizes swapped corners so a drag rectangle is always valid.
static_assert(Tina::Math::fromPoints(Tina::Math::Vec2{3.0F, 4.0F}, Tina::Math::Vec2{1.0F, 2.0F})
              == Tina::Math::Aabb2{{1.0F, 2.0F}, {3.0F, 4.0F}});
