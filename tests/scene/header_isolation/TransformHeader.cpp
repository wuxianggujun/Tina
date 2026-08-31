#include <tina/scene/Transform.hpp>

// Transform.hpp no longer defines its own vector or quaternion; it composes the
// shared Tina::Math types. These assertions fail if a second Vec3 reappears here.
static_assert(Tina::Scene::LocalTransform{}.position.z == 0.0F);
static_assert(Tina::Scene::LocalTransform{}.rotation.w == 1.0F);
static_assert(Tina::Scene::LocalTransform{}.scale == Tina::Math::Vec3{1.0F, 1.0F, 1.0F});
static_assert(Tina::Scene::WorldTransform{}.scale == Tina::Math::Vec3{1.0F, 1.0F, 1.0F});
