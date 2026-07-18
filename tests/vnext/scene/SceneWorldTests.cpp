#include <tina/scene/World.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <thread>

namespace Tina::Scene {
namespace {

enum class AllocationFailureKind : u8 {
    BadAlloc,
    RuntimeError,
};

class FailAfterSuccessfulAllocationsResource final : public std::pmr::memory_resource {
public:
    explicit FailAfterSuccessfulAllocationsResource(
        usize allowedAllocations,
        AllocationFailureKind failureKind = AllocationFailureKind::BadAlloc) noexcept
        : m_allowedAllocations(allowedAllocations),
          m_failureKind(failureKind)
    {
    }

    [[nodiscard]] usize outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (m_successfulAllocations >= m_allowedAllocations) {
            if (m_failureKind == AllocationFailureKind::RuntimeError) {
                throw std::runtime_error{"injected allocation failure"};
            }
            throw std::bad_alloc{};
        }
        void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_successfulAllocations;
        ++m_outstandingAllocations;
        return result;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --m_outstandingAllocations;
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allowedAllocations = 0;
    usize m_successfulAllocations = 0;
    usize m_outstandingAllocations = 0;
    AllocationFailureKind m_failureKind = AllocationFailureKind::BadAlloc;
};

[[nodiscard]] World makeWorld(usize capacity = 64)
{
    auto world = World::Create(WorldConfig{capacity});
    EXPECT_TRUE(world.has_value()) << world.error().message;
    return std::move(*world);
}

[[nodiscard]] LocalTransform translated(float x, float y, float z = 0.0F)
{
    LocalTransform transform;
    transform.position = {x, y, z};
    return transform;
}

[[nodiscard]] Quaternion rotationAroundZ(float radians) noexcept
{
    const float halfAngle = radians * 0.5F;
    return {0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
}

TEST(SceneWorldTest, RejectsInvalidCapacityBeforeAllocating)
{
    EXPECT_FALSE(World::Create(WorldConfig{0}));
    EXPECT_FALSE(World::Create(WorldConfig{WorldConfig::MaxEntityCapacity + 1}));
}

TEST(SceneWorldTest, RollsBackAllPmrAllocationsWhenCreateFails)
{
    FailAfterSuccessfulAllocationsResource resource(1);
    const auto result = World::Create(WorldConfig{8}, resource);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(SceneWorldTest, MapsUnexpectedConstructionFailureToStableSceneError)
{
    FailAfterSuccessfulAllocationsResource resource(
        1, AllocationFailureKind::RuntimeError);
    const auto result = World::Create(WorldConfig{8}, resource);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, SceneErrorCode::ConstructionFailed);
    EXPECT_EQ(result.error().message, "injected allocation failure");
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(SceneWorldTest, CreatesGenerationOwnedEntitiesAndRejectsStaleOrForeignIds)
{
    World first = makeWorld(2);
    World second = makeWorld(2);
    const EntityId firstEntity = first.createEntity().value();
    const EntityId foreignEntity = second.createEntity().value();

    EXPECT_TRUE(first.contains(firstEntity));
    EXPECT_FALSE(second.contains(firstEntity));
    EXPECT_EQ(
        first.setParent(firstEntity, foreignEntity).error().code,
        SceneErrorCode::WrongWorld);
    EXPECT_TRUE(first.setLocalTransform(firstEntity, translated(1.0F, 2.0F)));

    ASSERT_TRUE(first.destroyEntity(firstEntity));
    EXPECT_FALSE(first.contains(firstEntity));
    EXPECT_EQ(
        first.setLocalTransform(firstEntity, translated(3.0F, 4.0F)).error().code,
        SceneErrorCode::StaleEntity);

    const EntityId reused = first.createEntity().value();
    EXPECT_NE(reused, firstEntity);
    EXPECT_EQ(reused.owner(), firstEntity.owner());
    EXPECT_EQ(reused.index(), firstEntity.index());
    EXPECT_NE(reused.generation(), firstEntity.generation());
}

TEST(SceneWorldTest, ComposesParentScaleAndTranslationWithoutRecursion)
{
    World world = makeWorld();
    LocalTransform rootLocal = translated(10.0F, 0.0F);
    rootLocal.scale = {2.0F, 2.0F, 1.0F};
    const EntityId root = world.createEntity(rootLocal).value();
    const EntityId child = world.createEntity(translated(2.0F, 3.0F)).value();
    const EntityId grandchild = world.createEntity(translated(1.0F, 0.0F)).value();

    ASSERT_TRUE(world.setParent(child, root, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setParent(grandchild, child, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.updateWorldTransforms());

    const WorldTransform* childWorld = world.worldTransform(child);
    const WorldTransform* grandchildWorld = world.worldTransform(grandchild);
    ASSERT_NE(childWorld, nullptr);
    ASSERT_NE(grandchildWorld, nullptr);
    EXPECT_FLOAT_EQ(childWorld->position.x, 14.0F);
    EXPECT_FLOAT_EQ(childWorld->position.y, 6.0F);
    EXPECT_FLOAT_EQ(grandchildWorld->position.x, 16.0F);
    EXPECT_FLOAT_EQ(grandchildWorld->position.y, 6.0F);
    EXPECT_FLOAT_EQ(grandchildWorld->scale.x, 2.0F);
}

TEST(SceneWorldTest, RejectsSelfParentAndAncestorCyclesAtomically)
{
    World world = makeWorld();
    const EntityId root = world.createEntity().value();
    const EntityId child = world.createEntity().value();
    const EntityId grandchild = world.createEntity().value();
    ASSERT_TRUE(world.setParent(child, root, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setParent(grandchild, child, ReparentMode::KeepLocal));

    EXPECT_EQ(world.setParent(root, root).error().code, SceneErrorCode::HierarchyCycle);
    EXPECT_EQ(
        world.setParent(root, grandchild).error().code,
        SceneErrorCode::HierarchyCycle);
    EXPECT_EQ(world.parent(root), EntityId{});
    EXPECT_EQ(world.parent(child), root);
    EXPECT_EQ(world.parent(grandchild), child);
}

TEST(SceneWorldTest, ReparentDetachesOldParentAndUpdatesNewHierarchy)
{
    World world = makeWorld();
    const EntityId firstParent = world.createEntity().value();
    const EntityId secondParent = world.createEntity().value();
    const EntityId child = world.createEntity().value();

    ASSERT_TRUE(world.setParent(child, firstParent, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setParent(child, secondParent, ReparentMode::KeepLocal));
    EXPECT_EQ(world.parent(child), secondParent);
    ASSERT_TRUE(world.updateWorldTransforms());
}

TEST(SceneWorldTest, ReparentDefaultsToKeepWorldAndKeepLocalIsExplicit)
{
    World world = makeWorld();
    LocalTransform parentLocal = translated(10.0F, 4.0F);
    parentLocal.scale = {2.0F, 2.0F, 2.0F};
    const EntityId parent = world.createEntity(parentLocal).value();
    const EntityId child = world.createEntity(translated(2.0F, 3.0F)).value();
    ASSERT_TRUE(world.updateWorldTransforms());

    const WorldTransform preserved = *world.worldTransform(child);
    ASSERT_TRUE(world.setParent(child, parent));
    ASSERT_TRUE(world.updateWorldTransforms());
    EXPECT_EQ(*world.worldTransform(child), preserved);
    EXPECT_FLOAT_EQ(world.localTransform(child)->position.x, -4.0F);
    EXPECT_FLOAT_EQ(world.localTransform(child)->position.y, -0.5F);

    ASSERT_TRUE(world.setParent(child, std::nullopt, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.updateWorldTransforms());
    EXPECT_FLOAT_EQ(world.worldTransform(child)->position.x, -4.0F);
    EXPECT_FLOAT_EQ(world.worldTransform(child)->position.y, -0.5F);
}

TEST(SceneWorldTest, DestroyEntityKeepsChildrenAndTheirWorldTransforms)
{
    World world = makeWorld();
    const EntityId root = world.createEntity(translated(10.0F, 0.0F)).value();
    const EntityId child = world.createEntity(translated(2.0F, 0.0F)).value();
    const EntityId grandchild = world.createEntity(translated(1.0F, 0.0F)).value();
    ASSERT_TRUE(world.setParent(child, root, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setParent(grandchild, child, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.updateWorldTransforms());
    const WorldTransform childBefore = *world.worldTransform(child);
    const WorldTransform grandchildBefore = *world.worldTransform(grandchild);

    ASSERT_TRUE(world.destroyEntity(root));
    EXPECT_FALSE(world.contains(root));
    EXPECT_TRUE(world.contains(child));
    EXPECT_TRUE(world.contains(grandchild));
    EXPECT_EQ(world.parent(child), EntityId{});
    EXPECT_EQ(world.parent(grandchild), child);
    ASSERT_TRUE(world.updateWorldTransforms());
    EXPECT_EQ(*world.worldTransform(child), childBefore);
    EXPECT_EQ(*world.worldTransform(grandchild), grandchildBefore);
}

TEST(SceneWorldTest, DestroyRemovesOnlyTheRequestedSubtree)
{
    World world = makeWorld();
    const EntityId root = world.createEntity().value();
    const EntityId child = world.createEntity().value();
    const EntityId grandchild = world.createEntity().value();
    const EntityId sibling = world.createEntity().value();
    ASSERT_TRUE(world.setParent(child, root, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setParent(grandchild, child, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setParent(sibling, root, ReparentMode::KeepLocal));

    ASSERT_TRUE(world.destroySubtree(child));
    EXPECT_FALSE(world.contains(child));
    EXPECT_FALSE(world.contains(grandchild));
    EXPECT_TRUE(world.contains(root));
    EXPECT_TRUE(world.contains(sibling));
    EXPECT_EQ(world.entityCount(), 2U);
    ASSERT_TRUE(world.updateWorldTransforms());
}

TEST(SceneWorldTest, RejectsNonFiniteAndZeroQuaternionTransforms)
{
    World world = makeWorld();
    LocalTransform invalid = translated(0.0F, 0.0F);
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(world.createEntity(invalid));

    invalid = {};
    invalid.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(world.createEntity(invalid));
}

TEST(SceneWorldTest, NormalizesQuaternionCompositionAcrossADeepHierarchy)
{
    constexpr usize depth = 2'048;
    World world = makeWorld(depth);
    EntityId parent;
    EntityId deepest;
    LocalTransform local;
    local.rotation = rotationAroundZ(0.001F);
    for (usize index = 0; index < depth; ++index) {
        deepest = world.createEntity(local).value();
        if (parent.hasValue()) {
            ASSERT_TRUE(world.setParent(deepest, parent, ReparentMode::KeepLocal));
        }
        parent = deepest;
    }

    ASSERT_TRUE(world.updateWorldTransforms());
    const Quaternion rotation = world.worldTransform(deepest)->rotation;
    const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y
        + rotation.z * rotation.z + rotation.w * rotation.w;
    EXPECT_NEAR(lengthSquared, 1.0F, 1.0e-5F);
}

TEST(SceneWorldTest, RejectsWorldTransformOverflowWithoutPublishingPartialResults)
{
    World world = makeWorld();
    LocalTransform parentLocal;
    parentLocal.scale = {
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)()};
    const EntityId parent = world.createEntity(parentLocal).value();
    const EntityId child = world.createEntity(translated(2.0F, 0.0F)).value();
    const WorldTransform previouslyPublished = *world.worldTransform(child);
    ASSERT_TRUE(world.setParent(child, parent, ReparentMode::KeepLocal));

    const Core::Status status = world.updateWorldTransforms();
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::TransformOverflow);
    EXPECT_EQ(*world.worldTransform(child), previouslyPublished);
}

TEST(SceneWorldTest, RejectsShearThatCannotBeRepresentedAsTrs)
{
    World world = makeWorld();
    LocalTransform parentLocal;
    parentLocal.scale = {2.0F, 3.0F, 1.0F};
    LocalTransform childLocal;
    childLocal.rotation = rotationAroundZ(0.5F);
    const EntityId parent = world.createEntity(parentLocal).value();
    const EntityId child = world.createEntity(childLocal).value();
    ASSERT_TRUE(world.setParent(child, parent, ReparentMode::KeepLocal));

    const Core::Status status = world.updateWorldTransforms();
    ASSERT_FALSE(status);
    EXPECT_EQ(
        status.error().code,
        SceneErrorCode::UnsupportedTransformComposition);
}

TEST(SceneWorldTest, RejectsMutationFromAnotherThread)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    Core::ErrorCode observed{};
    std::thread worker([&] {
        const Core::Status status = world.setLocalTransform(
            entity, translated(5.0F, 6.0F));
        if (!status) {
            observed = status.error().code;
        }
    });
    worker.join();

    EXPECT_EQ(observed, SceneErrorCode::WrongOwnerThread);
    EXPECT_EQ(world.localTransform(entity)->position, Vec3{});
}

TEST(SceneWorldTest, OwnerThreadAlsoGuardsBorrowedReadAccess)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    bool contains = true;
    usize count = 1;
    usize capacity = 1;
    const LocalTransform* local = nullptr;
    const WorldTransform* transform = nullptr;
    std::thread worker([&] {
        contains = world.contains(entity);
        count = world.entityCount();
        capacity = world.entityCapacity();
        local = world.localTransform(entity);
        transform = world.worldTransform(entity);
    });
    worker.join();

    EXPECT_FALSE(contains);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(capacity, 0U);
    EXPECT_EQ(local, nullptr);
    EXPECT_EQ(transform, nullptr);
}

TEST(SceneWorldTest, PropagatesDeepHierarchyWithoutUsingTheCallStack)
{
    constexpr usize depth = 20'000;
    World world = makeWorld(depth);
    EntityId parent;
    EntityId deepest;
    for (usize index = 0; index < depth; ++index) {
        deepest = world.createEntity(translated(1.0F, 0.0F)).value();
        if (parent.hasValue()) {
            ASSERT_TRUE(world.setParent(deepest, parent, ReparentMode::KeepLocal));
        }
        parent = deepest;
    }

    ASSERT_TRUE(world.updateWorldTransforms());
    const WorldTransform* result = world.worldTransform(deepest);
    ASSERT_NE(result, nullptr);
    EXPECT_FLOAT_EQ(result->position.x, static_cast<float>(depth));
}

TEST(SceneWorldTest, FixedCapacityRejectsAdditionalEntities)
{
    World world = makeWorld(2);
    ASSERT_TRUE(world.createEntity());
    ASSERT_TRUE(world.createEntity());
    const auto result = world.createEntity();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, SceneErrorCode::CapacityExceeded);
}

TEST(SceneWorldTest, DestroysWideSubtreeWithLinearLiveBookkeeping)
{
    constexpr usize childCount = 20'000;
    World world = makeWorld(childCount + 1);
    const EntityId root = world.createEntity().value();
    for (usize index = 0; index < childCount; ++index) {
        const EntityId child = world.createEntity().value();
        ASSERT_TRUE(world.setParent(child, root, ReparentMode::KeepLocal));
    }

    ASSERT_TRUE(world.destroySubtree(root));
    EXPECT_EQ(world.entityCount(), 0U);
}

} // namespace
} // namespace Tina::Scene
