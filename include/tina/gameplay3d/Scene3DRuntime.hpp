#pragma once

#include <tina/asset/AssetSystem.hpp>
#include <tina/gameplay3d/World3DSceneIndex.hpp>
#include <tina/scene/AnimationEvents3D.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#if defined(TINA_HAS_PHYSICS3D)
#include <tina/gameplay3d/Scene3DPhysicsBridge.hpp>
#endif

#include <memory>

namespace Tina::Gameplay3D {

struct Scene3DRuntimeConfig final {
    Core::usize animatorCapacity = 256;
    Core::usize animationEventCapacity = 1024;
    Core::usize contactEventCapacity = 4096;
    float fixedDeltaSeconds = 1.0F / 60.0F;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct Scene3DAnimationEvent final {
    Core::u32 stableNodeId = 0;
    Scene::EntityId entity{};
    Scene::AnimationEventCrossing3D crossing{};
};

struct Scene3DPlayerInput final {
    // World X/Z direction, clamped to unit length. Jump is latched until one step.
    float moveX = 0.0F;
    float moveZ = 0.0F;
    bool jumpPressed = false;
};

struct Scene3DRuntimeStats final {
    Core::usize animatedCount = 0;
    Core::usize skinnedCount = 0;
    Core::u64 fixedSteps = 0;
    Core::u64 physicsSteps = 0;
    bool faulted = false;
};

// Event spans expire at the next fixedUpdate/shutdown. A fixedUpdate failure
// after simulation starts quarantines this owner; rebuild the isolated instance.
struct Scene3DStepResult final {
    std::span<const Scene3DAnimationEvent> animationEvents{};
    Core::u64 droppedAnimationEvents = 0;
#if defined(TINA_HAS_PHYSICS3D)
    std::span<const Physics3D::PhysicsContactEvent3D> contacts{};
    Core::u64 droppedContacts = 0;
#endif
};

// Owner-thread gameplay state for one prefab instance. World and AssetSystem
// remain product-owned. This owner retains mesh/clip leases, copied CPU poses,
// stable IDs and optional bodies; it never submits GPU work or changes documents.
// Moving World preserves this binding; replacing or destroying its entities does not.
class Scene3DRuntime final {
  public:
    Scene3DRuntime() noexcept;
    ~Scene3DRuntime() noexcept;
    Scene3DRuntime(const Scene3DRuntime&) = delete;
    Scene3DRuntime& operator=(const Scene3DRuntime&) = delete;
    Scene3DRuntime(Scene3DRuntime&&) = delete;
    Scene3DRuntime& operator=(Scene3DRuntime&&) = delete;

    // Dependencies must already be resident. Missing/wrong-kind dependencies or
    // authored physics without an enabled world fail before publication.
    [[nodiscard]] Core::Status build(Scene::World& world,
        const AssetFormat::PrefabPayloadView& prefab, std::span<const Scene::EntityId> entities,
        Asset::AssetSystem& assets, Scene3DRuntimeConfig config = {}
#if defined(TINA_HAS_PHYSICS3D)
        , Physics3D::PhysicsWorld3D* physics = nullptr
#endif
        );
    [[nodiscard]] Core::Result<Scene3DStepResult> fixedUpdate(Scene::World& world);
    [[nodiscard]] Core::Status setPlayerInput(Scene3DPlayerInput input) noexcept;
    // Pause/focus loss must cancel a jump that has not reached a fixed step yet.
    [[nodiscard]] Core::Status clearPlayerInput() noexcept;
    [[nodiscard]] Scene::EntityId playerEntity() const noexcept;
    [[nodiscard]] Core::Status shutdown() noexcept;
    [[nodiscard]] Scene::SkinnedPose3DProvider poseProvider() noexcept;
    [[nodiscard]] std::span<const float> pose(Scene::EntityId entity) const noexcept;
    [[nodiscard]] const World3DSceneIndex* index() const noexcept;
    [[nodiscard]] Scene3DRuntimeStats stats() const noexcept;
#if defined(TINA_HAS_PHYSICS3D)
    [[nodiscard]] Scene3DPhysicsBridge* physicsBridge() noexcept;
#endif

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::Gameplay3D
