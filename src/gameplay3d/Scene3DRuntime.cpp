#include <tina/gameplay3d/Scene3DRuntime.hpp>

#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/scene/Animator3D.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <thread>
#include <utility>

namespace Tina::Gameplay3D {
namespace {
void saturatingAdd(Core::u64& total, Core::u64 value) noexcept
{
    total += (std::min)(value, (std::numeric_limits<Core::u64>::max)() - total);
}
}

struct Scene3DRuntime::Impl final {
    struct PoseBinding final {
        Scene::EntityId entity{};
        Core::u32 stableNodeId = 0;
        Asset::AssetLease meshLease;
        Asset::AssetLease clipLease;
        std::optional<Scene::Animator3D> animator;
        std::vector<float> bindPalette;
    };
    World3DSceneIndex index;
    Scene3DRuntimeConfig config;
    Scene3DRuntimeStats stats;
    std::vector<PoseBinding> poses;
    std::vector<Scene3DAnimationEvent> events;
    std::thread::id ownerThread = std::this_thread::get_id();
    Scene::EntityId player{};
    Scene3DPlayerInput playerInput{};
    float moveSpeed = 0.0F;
    float jumpSpeed = 0.0F;
#if defined(TINA_HAS_PHYSICS3D)
    Physics3D::PhysicsWorld3D* physicsWorld = nullptr;
    std::vector<Physics3D::PhysicsContactEvent3D> contacts;
    std::unique_ptr<Scene3DPhysicsBridge> physics;
#endif
};

Scene3DRuntime::Scene3DRuntime() noexcept = default;
Scene3DRuntime::~Scene3DRuntime() noexcept
{
    if (m_impl && m_impl->ownerThread != std::this_thread::get_id()) std::terminate();
}

Core::Status Scene3DRuntime::build(Scene::World& world, const AssetFormat::PrefabPayloadView& prefab,
    std::span<const Scene::EntityId> entities, Asset::AssetSystem& assets, Scene3DRuntimeConfig config
#if defined(TINA_HAS_PHYSICS3D)
    , Physics3D::PhysicsWorld3D* physics
#endif
    )
try
{
    if (m_impl || config.animatorCapacity == 0 || config.animatorCapacity > AssetFormat::PrefabWire::MaxNodes ||
        config.animationEventCapacity == 0 || config.animationEventCapacity > 1048576 ||
        config.contactEventCapacity == 0 || config.contactEventCapacity > 1048576 ||
        !std::isfinite(config.fixedDeltaSeconds) || config.fixedDeltaSeconds <= 0.0F || config.fixedDeltaSeconds > 0.1F)
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D runtime is already built or has invalid configuration");
    if (!assets.store().onOwnerThread())
        return Core::failure(Asset::AssetErrorCode::WrongOwnerThread, "Scene3D build requires the asset owner thread");
    if (config.memoryResource == nullptr) config.memoryResource = std::pmr::get_default_resource();
    auto candidate = std::make_unique<Impl>();
    candidate->config = config;
    if (auto status = candidate->index.build(world, prefab, entities); !status) return status;
    const bool hasPhysics = std::any_of(prefab.nodes.begin(), prefab.nodes.end(), [](const auto& node) {
        return node.physics.has_value();
    });
#if defined(TINA_HAS_PHYSICS3D)
    if (hasPhysics && physics == nullptr)
#else
    if (hasPhysics)
#endif
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D authored physics requires an enabled Physics3D world");
    candidate->poses.reserve((std::min)(config.animatorCapacity, entities.size()));
    candidate->events.reserve(config.animationEventCapacity);
    for (Core::usize index = 0; index < entities.size(); ++index)
    {
        const auto& authored = prefab.nodes[index];
        if (authored.physics && authored.physics->playerControlled) {
            if (candidate->player || authored.physics->type != AssetFormat::PrefabPhysicsBody3D::Character)
                return Core::failure(Scene::SceneErrorCode::InvalidComponent,
                                     "Scene3D permits at most one player-controlled Character");
            candidate->player = entities[index];
            candidate->moveSpeed = authored.physics->moveSpeedMetersPerSecond;
            candidate->jumpSpeed = authored.physics->jumpSpeedMetersPerSecond;
        }
        if (authored.nodeKind != AssetFormat::PrefabNodeKind::SkinnedMesh3D) continue;
        if (candidate->poses.size() == config.animatorCapacity)
            return Core::failure(Scene::SceneErrorCode::CapacityExceeded, "Scene3D skinned pose budget exceeded");
        const auto* renderer = world.skinnedMeshRenderer3D(entities[index]);
        if (renderer == nullptr || !renderer->mesh || !renderer->material)
            return Core::failure(Scene::SceneErrorCode::InvalidComponent, "prefab skinned node has no matching renderer");
        auto meshLease = assets.acquire(renderer->mesh);
        if (!meshLease) return Core::failure(std::move(meshLease.error()));
        if (meshLease->assetId() != authored.meshId)
            return Core::failure(Asset::AssetErrorCode::CatalogEntryMismatch, "Scene3D renderer differs from authored mesh identity");
        auto mesh = Asset::parseSkinnedMeshFromCooked(*meshLease->get());
        if (!mesh) return Core::failure(std::move(mesh.error()));
        candidate->poses.emplace_back();
        auto& pose = candidate->poses.back();
        pose.entity = entities[index];
        pose.stableNodeId = authored.stableNodeId;
        pose.meshLease = std::move(*meshLease);
        if (authored.animation)
        {
            const auto clipHandle = assets.find(authored.animation->clipId);
            if (!clipHandle) return Core::failure(Asset::AssetErrorCode::AssetNotReady, "Scene3D animation clip is not resident");
            auto lease = assets.acquire(*clipHandle);
            if (!lease) return Core::failure(std::move(lease.error()));
            auto clip = Asset::parseAnimationClip3DFromCooked(*lease->get());
            if (!clip) return Core::failure(std::move(clip.error()));
            auto animator = Scene::Animator3D::Create(*mesh, *clip, *config.memoryResource);
            if (!animator) return Core::failure(std::move(animator.error()));
            if (auto status = animator->setPlaybackSpeed(authored.animation->playbackSpeed); !status) return status;
            if (!authored.animation->autoPlay) animator->pause();
            pose.animator.emplace(std::move(*animator));
            pose.clipLease = std::move(*lease);
            ++candidate->stats.animatedCount;
        } else
        {
            auto skeleton = Animation3D::Skeleton3D::Create(*mesh, *config.memoryResource);
            if (!skeleton) return Core::failure(std::move(skeleton.error()));
            auto bindPose = Animation3D::Pose3D::Create(skeleton->jointCount(), *config.memoryResource);
            if (!bindPose) return Core::failure(std::move(bindPose.error()));
            if (auto status = skeleton->writeBindPose(*bindPose); !status) return status;
            pose.bindPalette.resize(static_cast<Core::usize>(skeleton->jointCount()) * 16);
            if (auto status = skeleton->composeSkinningMatrices(*bindPose, pose.bindPalette); !status) return status;
        }
    }
#if defined(TINA_HAS_PHYSICS3D)
    if (physics != nullptr)
    {
        const auto step = physics->fixedDeltaSeconds();
        if (!step) return Core::failure(step.error());
        if (*step != config.fixedDeltaSeconds)
            return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D and Physics3D fixed steps must match");
        candidate->contacts.resize(config.contactEventCapacity);
        candidate->physics = std::make_unique<Scene3DPhysicsBridge>();
        if (auto status = candidate->physics->build(world, prefab, entities, *physics); !status) return status;
        candidate->physicsWorld = physics;
    }
#endif
    candidate->stats.skinnedCount = candidate->poses.size();
    m_impl = std::move(candidate);
    return Core::success();
} catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Scene3D runtime allocation failed");
}

Core::Result<Scene3DStepResult> Scene3DRuntime::fixedUpdate(Scene::World& world)
{
    if (!m_impl || m_impl->stats.faulted)
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D runtime is closed or faulted");
    if (m_impl->ownerThread != std::this_thread::get_id())
        return Core::failure(Asset::AssetErrorCode::WrongOwnerThread, "Scene3D update requires its owner thread");
    // Entity IDs retain their owner and generation when World is moved.
    for (const auto& entry : m_impl->index.entries())
        if (!world.contains(entry.entity))
            return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D instance was structurally changed");
    auto quarantine = Core::makeScopeExit([&]() noexcept { m_impl->stats.faulted = true; });
    Scene3DStepResult result;
    m_impl->events.clear();
#if defined(TINA_HAS_PHYSICS3D)
    if (m_impl->physics)
    {
        if (m_impl->player) {
            const auto& input = m_impl->playerInput;
            if (auto status = m_impl->physicsWorld->setCharacterInput(m_impl->physics->body(m_impl->player),
                    {.horizontalVelocityMetersPerSecond = {input.moveX * m_impl->moveSpeed, 0.0F, input.moveZ * m_impl->moveSpeed},
                     .jumpSpeedMetersPerSecond = input.jumpPressed ? m_impl->jumpSpeed : 0.0F}); !status)
                return Core::failure(std::move(status.error()));
            m_impl->playerInput.jumpPressed = false;
        }
        if (auto status = m_impl->physics->fixedUpdate(world); !status) return Core::failure(std::move(status.error()));
        saturatingAdd(m_impl->stats.physicsSteps, 1);
        auto contacts = m_impl->physicsWorld->readContactEvents(m_impl->contacts);
        if (!contacts) return Core::failure(std::move(contacts.error()));
        if (contacts->remainingCount != 0)
            return Core::failure(Scene::SceneErrorCode::CapacityExceeded, "Scene3D contact read budget is smaller than the physical event batch");
        result.contacts = std::span(m_impl->contacts).first(contacts->writtenCount);
        result.droppedContacts = contacts->droppedCount;
    }
#endif
    for (auto& entry : m_impl->poses)
    {
        if (!entry.animator) continue;
        auto update = entry.animator->update(Core::Duration{m_impl->config.fixedDeltaSeconds});
        if (!update) return Core::failure(std::move(update.error()));
        saturatingAdd(result.droppedAnimationEvents, update->droppedEvents);
        for (const auto& event : update->crossedEvents)
        {
            if (m_impl->events.size() == m_impl->config.animationEventCapacity)
                saturatingAdd(result.droppedAnimationEvents, 1);
            else m_impl->events.push_back({entry.stableNodeId, entry.entity, event});
        }
    }
    result.animationEvents = m_impl->events;
    saturatingAdd(m_impl->stats.fixedSteps, 1);
    quarantine.release();
    return result;
}

Core::Status Scene3DRuntime::setPlayerInput(Scene3DPlayerInput input) noexcept
{
    if (!m_impl || m_impl->stats.faulted)
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D input requires a live runtime");
    if (m_impl->ownerThread != std::this_thread::get_id())
        return Core::failure(Asset::AssetErrorCode::WrongOwnerThread, "Scene3D input requires its owner thread");
    if (!std::isfinite(input.moveX) || !std::isfinite(input.moveZ))
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D player input must be finite");
    const double length = std::hypot(static_cast<double>(input.moveX), static_cast<double>(input.moveZ));
    if (length > 1.0) {
        input.moveX = static_cast<float>(input.moveX / length);
        input.moveZ = static_cast<float>(input.moveZ / length);
    }
    input.jumpPressed = input.jumpPressed || m_impl->playerInput.jumpPressed;
    m_impl->playerInput = input;
    return Core::success();
}

Scene::EntityId Scene3DRuntime::playerEntity() const noexcept
{
    return m_impl && m_impl->ownerThread == std::this_thread::get_id() ? m_impl->player : Scene::EntityId{};
}

Core::Status Scene3DRuntime::clearPlayerInput() noexcept
{
    if (auto status = setPlayerInput({}); !status) return status;
    m_impl->playerInput = {};
    return Core::success();
}

Core::Status Scene3DRuntime::shutdown() noexcept
{
    if (!m_impl) return Core::success();
    if (m_impl->ownerThread != std::this_thread::get_id())
        return Core::failure(Asset::AssetErrorCode::WrongOwnerThread, "Scene3D shutdown requires its owner thread");
#if defined(TINA_HAS_PHYSICS3D)
    if (m_impl->physics)
        if (auto status = m_impl->physics->shutdown(); !status) return status;
#endif
    m_impl.reset();
    return Core::success();
}

std::span<const float> Scene3DRuntime::pose(Scene::EntityId entity) const noexcept
{
    if (m_impl && !m_impl->stats.faulted && m_impl->ownerThread == std::this_thread::get_id())
        for (const auto& pose : m_impl->poses)
            if (pose.entity == entity)
                return pose.animator ? pose.animator->skinningMatrices() : std::span<const float>(pose.bindPalette);
    return {};
}
Scene::SkinnedPose3DProvider Scene3DRuntime::poseProvider() noexcept
{
    return {.userData = this, .resolve = [](void* runtime, Scene::EntityId entity) noexcept {
        return static_cast<Scene3DRuntime*>(runtime)->pose(entity);
    }};
}
const World3DSceneIndex* Scene3DRuntime::index() const noexcept { return m_impl ? &m_impl->index : nullptr; }
Scene3DRuntimeStats Scene3DRuntime::stats() const noexcept { return m_impl ? m_impl->stats : Scene3DRuntimeStats{}; }
#if defined(TINA_HAS_PHYSICS3D)
Scene3DPhysicsBridge* Scene3DRuntime::physicsBridge() noexcept { return m_impl ? m_impl->physics.get() : nullptr; }
#endif

} // namespace Tina::Gameplay3D
