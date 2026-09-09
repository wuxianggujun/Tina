#pragma once

#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/physics3d/PhysicsWorld3D.hpp>
#include <tina/scene/World.hpp>

#include <memory>
#include <span>

namespace Tina::Gameplay3D {

// Owns only the bodies it creates. The physical world must outlive this bridge.
// Authored dimensions are meters: physics nodes require unit world scale. Put
// scaled render geometry below a physics node instead of changing its collider.
class Scene3DPhysicsBridge final {
  public:
    Scene3DPhysicsBridge() noexcept;
    ~Scene3DPhysicsBridge() noexcept;
    Scene3DPhysicsBridge(const Scene3DPhysicsBridge&) = delete;
    Scene3DPhysicsBridge& operator=(const Scene3DPhysicsBridge&) = delete;
    [[nodiscard]] Core::Status build(Scene::World& world, const AssetFormat::PrefabPayloadView& prefab,
        std::span<const Scene::EntityId> entities, Physics3D::PhysicsWorld3D& physics);
    // Scene -> static/kinematic; step; dynamic/character -> Scene; publish TRS.
    // A failure after solver entry quarantines this bridge until shutdown.
    [[nodiscard]] Core::Status fixedUpdate(Scene::World& world);
    [[nodiscard]] Physics3D::PhysicsBodyId body(Scene::EntityId entity) const noexcept;
    [[nodiscard]] Scene::EntityId entity(Physics3D::PhysicsBodyId body) const noexcept;
    [[nodiscard]] Core::Status shutdown() noexcept;
  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::Gameplay3D
