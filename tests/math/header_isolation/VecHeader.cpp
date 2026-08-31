#include <tina/math/Vec.hpp>

#include <type_traits>

// Trivially copyable, exactly-packed value types: these travel inside Scene/Render
// aggregates and cooked payloads, where an unexpected size or padding byte changes
// a wire layout.
static_assert(std::is_trivially_copyable_v<Tina::Math::Vec2>);
static_assert(std::is_trivially_copyable_v<Tina::Math::Vec3>);
static_assert(std::is_trivially_copyable_v<Tina::Math::Vec4>);
static_assert(sizeof(Tina::Math::Vec2) == 8);
static_assert(sizeof(Tina::Math::Vec3) == 12);
static_assert(sizeof(Tina::Math::Vec4) == 16);

// Default construction is the zero vector; authored components depend on it.
static_assert(Tina::Math::Vec3{} == Tina::Math::Vec3{0.0F, 0.0F, 0.0F});

// The core products remain usable in constant expressions.
static_assert(Tina::Math::dot(Tina::Math::Vec3{1.0F, 2.0F, 3.0F},
                              Tina::Math::Vec3{4.0F, 5.0F, 6.0F})
              == 32.0F);
static_assert(Tina::Math::cross(Tina::Math::Vec3{1.0F, 0.0F, 0.0F},
                                Tina::Math::Vec3{0.0F, 1.0F, 0.0F})
              == Tina::Math::Vec3{0.0F, 0.0F, 1.0F});
