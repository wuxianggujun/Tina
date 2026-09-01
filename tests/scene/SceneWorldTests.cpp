#include <tina/scene/World.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <new>
#include <ranges>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

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

[[nodiscard]] Math::Quaternion rotationAroundZ(float radians) noexcept
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
    const Math::Quaternion rotation = world.worldTransform(deepest)->rotation;
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
    EXPECT_EQ(world.localTransform(entity)->position, Math::Vec3{});
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

TEST(SceneWorldTest, StoresValidatedRuntimeMetadataAtomically)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMetadata(entity, EntityMetadataDesc{
        .name = "player",
        .tag = 11,
        .layer = 3,
        .group = 7,
    }));

    ASSERT_NE(world.metadata(entity), nullptr);
    EXPECT_EQ(world.runtimeName(entity), "player");
    EXPECT_EQ(world.tag(entity), 11U);
    EXPECT_EQ(world.layer(entity), 3U);
    EXPECT_EQ(world.group(entity), 7U);

    const std::string tooLong(EntityNameMaximumBytes + 1U, 'x');
    const Core::Status rejected = world.setRuntimeName(entity, tooLong);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(world.runtimeName(entity), "player");

    const std::string invalidUtf8{"\xC3\x28", 2};
    EXPECT_EQ(
        world.setRuntimeName(entity, invalidUtf8).error().code,
        SceneErrorCode::InvalidMetadata);
    EXPECT_EQ(world.runtimeName(entity), "player");
}

TEST(SceneWorldTest, ReadOnlyTypedViewFiltersTheClosedComponentSet)
{
    static_assert(WorldReadableComponent<Camera2D>);
    static_assert(!WorldReadableComponent<int>);

    World world = makeWorld();
    const EntityId camera = world.createEntity().value();
    const EntityId plain = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(camera, Camera2D{}));
    ASSERT_TRUE(world.setTag(camera, 42));

    usize visits = 0;
    world.query<EntityMetadata, LocalTransform, Camera2D>().each(
        [&](EntityId entity,
            const EntityMetadata& metadata,
            const LocalTransform&,
            const Camera2D&) {
            ++visits;
            EXPECT_EQ(entity, camera);
            EXPECT_EQ(metadata.tag, 42U);
        });

    EXPECT_EQ(visits, 1U);
    EXPECT_TRUE(world.view<Camera2D>().contains(camera));
    EXPECT_FALSE(world.view<Camera2D>().contains(plain));
    EXPECT_EQ(world.get<Camera2D>(plain), nullptr);
}

// Metadata lives in EntityRecord, and GenerationPool reuses slots. If destruction
// did not actually reset the record, a new entity would inherit the previous
// occupant's name and tag -- a silent identity leak across a recycled slot.
TEST(SceneWorldTest, MetadataResetsWhenASlotIsReused)
{
    World world = makeWorld(2);
    const EntityId first = world.createEntity().value();
    ASSERT_TRUE(world.setMetadata(first, EntityMetadataDesc{
        .name = "leaky",
        .tag = 9,
        .layer = 4,
        .group = 5,
    }));
    ASSERT_TRUE(world.destroyEntity(first));

    const EntityId reused = world.createEntity().value();
    // Same storage slot, different generation.
    ASSERT_EQ(reused.index(), first.index());
    ASSERT_NE(reused, first);

    ASSERT_NE(world.metadata(reused), nullptr);
    EXPECT_TRUE(world.runtimeName(reused).empty());
    EXPECT_EQ(world.tag(reused), NoEntityTag);
    EXPECT_EQ(world.layer(reused), DefaultEntityLayer);
    EXPECT_EQ(world.group(reused), NoEntityGroup);
    // The stale id is rejected rather than aliasing the recycled slot.
    EXPECT_EQ(world.metadata(first), nullptr);
}

// setMetadata replaces all four fields as one transaction. A rejected name must not
// leave tag/layer/group half-applied.
TEST(SceneWorldTest, RejectedMetadataReplacementLeavesEveryFieldUnchanged)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMetadata(entity, EntityMetadataDesc{
        .name = "keep",
        .tag = 1,
        .layer = 2,
        .group = 3,
    }));

    const std::string tooLong(EntityNameMaximumBytes + 1U, 'x');
    const Core::Status rejectedLength = world.setMetadata(entity, EntityMetadataDesc{
        .name = tooLong,
        .tag = 77,
        .layer = 88,
        .group = 99,
    });
    ASSERT_FALSE(rejectedLength);
    EXPECT_EQ(rejectedLength.error().code, SceneErrorCode::CapacityExceeded);

    const std::string invalidUtf8{"\xC3\x28", 2};
    const Core::Status rejectedUtf8 = world.setMetadata(entity, EntityMetadataDesc{
        .name = invalidUtf8,
        .tag = 77,
        .layer = 88,
        .group = 99,
    });
    ASSERT_FALSE(rejectedUtf8);
    EXPECT_EQ(rejectedUtf8.error().code, SceneErrorCode::InvalidMetadata);

    // None of the four fields moved.
    EXPECT_EQ(world.runtimeName(entity), "keep");
    EXPECT_EQ(world.tag(entity), 1U);
    EXPECT_EQ(world.layer(entity), 2U);
    EXPECT_EQ(world.group(entity), 3U);
}

TEST(SceneWorldTest, RuntimeNameAcceptsTheByteLimitAndMultiByteUtf8)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();

    // The advertised limit must be usable, not merely close to failing.
    const std::string atLimit(EntityNameMaximumBytes, 'n');
    ASSERT_TRUE(world.setRuntimeName(entity, atLimit));
    EXPECT_EQ(world.runtimeName(entity), atLimit);

    // Multi-byte UTF-8 is bounded in bytes, not code points.
    ASSERT_TRUE(world.setRuntimeName(entity, "玩家一号"));
    EXPECT_EQ(world.runtimeName(entity), "玩家一号");

    // clearRuntimeName and an empty name are the same thing.
    ASSERT_TRUE(world.clearRuntimeName(entity));
    EXPECT_TRUE(world.runtimeName(entity).empty());
    ASSERT_TRUE(world.setRuntimeName(entity, "named"));
    ASSERT_TRUE(world.setRuntimeName(entity, {}));
    EXPECT_TRUE(world.runtimeName(entity).empty());
}

// The view advertises forward_iterator_tag. Asserting the concept keeps that from
// silently degrading into something the ranges algorithms will not accept.
TEST(SceneWorldTest, TypedViewSatisfiesTheForwardRangeContract)
{
    using View = WorldView<LocalTransform>;
    static_assert(std::forward_iterator<View::Iterator>);
    static_assert(std::ranges::forward_range<View>);
    static_assert(std::same_as<std::ranges::range_value_t<View>, EntityId>);

    World world = makeWorld();
    const EntityId first = world.createEntity().value();
    const EntityId second = world.createEntity().value();

    // Multi-pass: a forward range yields the same sequence when traversed twice.
    const View view = world.view<LocalTransform>();
    std::vector<EntityId> firstPass;
    for (const EntityId entity : view) {
        firstPass.push_back(entity);
    }
    std::vector<EntityId> secondPass;
    for (const EntityId entity : view) {
        secondPass.push_back(entity);
    }
    EXPECT_EQ(firstPass, secondPass);
    ASSERT_EQ(firstPass.size(), 2U);
    EXPECT_NE(std::ranges::find(firstPass, first), firstPass.end());
    EXPECT_NE(std::ranges::find(firstPass, second), firstPass.end());
}

TEST(SceneWorldTest, TypedViewRequiresEveryRequestedComponent)
{
    World world = makeWorld();
    const EntityId all = world.createEntity().value();
    const EntityId partial = world.createEntity().value();
    const EntityId none = world.createEntity().value();

    ASSERT_TRUE(world.setCamera2D(all, Camera2D{}));
    ASSERT_TRUE(world.setSpriteRenderer2D(all, SpriteRenderer2D{}));
    ASSERT_TRUE(world.setCamera2D(partial, Camera2D{}));
    ASSERT_TRUE(world.updateWorldTransforms());

    usize visits = 0;
    world.view<Camera2D, SpriteRenderer2D, WorldTransform>().each(
        [&](EntityId entity, const Camera2D&, const SpriteRenderer2D&, const WorldTransform&) {
            ++visits;
            EXPECT_EQ(entity, all);
        });
    EXPECT_EQ(visits, 1U);

    const auto intersection = world.view<Camera2D, SpriteRenderer2D>();
    EXPECT_TRUE(intersection.contains(all));
    EXPECT_FALSE(intersection.contains(partial));
    EXPECT_FALSE(intersection.contains(none));
    EXPECT_FALSE(intersection.empty());

    // Clearing one component drops the entity from the intersection. Bound to a
    // local first: the comma in the template argument list would otherwise be read
    // as a macro argument separator.
    ASSERT_TRUE(world.clearSpriteRenderer2D(all));
    const auto afterClear = world.view<Camera2D, SpriteRenderer2D>();
    EXPECT_TRUE(afterClear.empty());
}

TEST(SceneWorldTest, MetadataFilteredQueryNarrowsByValue)
{
    World world = makeWorld();
    const EntityId enemyA = world.createEntity().value();
    const EntityId enemyB = world.createEntity().value();
    const EntityId ally = world.createEntity().value();
    const EntityId untagged = world.createEntity().value();

    constexpr EntityTag EnemyTag = 7;
    constexpr EntityTag AllyTag = 8;
    for (const EntityId entity : {enemyA, enemyB, ally, untagged}) {
        ASSERT_TRUE(world.setSpriteRenderer2D(entity, SpriteRenderer2D{}));
    }
    ASSERT_TRUE(world.setMetadata(enemyA, EntityMetadataDesc{.tag = EnemyTag, .layer = 1, .group = 2}));
    ASSERT_TRUE(world.setMetadata(enemyB, EntityMetadataDesc{.tag = EnemyTag, .layer = 5, .group = 2}));
    ASSERT_TRUE(world.setMetadata(ally, EntityMetadataDesc{.tag = AllyTag, .layer = 1, .group = 2}));

    const auto collect = [](auto&& view) {
        std::vector<EntityId> found;
        for (const EntityId entity : view) {
            found.push_back(entity);
        }
        return found;
    };

    // Single dimension.
    const std::vector<EntityId> enemies =
        collect(world.viewWhere<SpriteRenderer2D>(EntityMetadataFilter{.tag = EnemyTag}));
    ASSERT_EQ(enemies.size(), 2U);
    EXPECT_NE(std::ranges::find(enemies, enemyA), enemies.end());
    EXPECT_NE(std::ranges::find(enemies, enemyB), enemies.end());

    // Two dimensions combine with AND.
    const std::vector<EntityId> layerOneEnemies = collect(
        world.viewWhere<SpriteRenderer2D>(EntityMetadataFilter{.tag = EnemyTag, .layer = 1}));
    ASSERT_EQ(layerOneEnemies.size(), 1U);
    EXPECT_EQ(layerOneEnemies.front(), enemyA);

    // A filter matching nothing yields an empty range rather than an error.
    EXPECT_TRUE(world.viewWhere<SpriteRenderer2D>(EntityMetadataFilter{.tag = 999}).empty());

    // contains() honours the filter too, which is why filtering is declarative
    // rather than an if inside a caller-supplied each() lambda.
    const auto enemyView = world.viewWhere<SpriteRenderer2D>(EntityMetadataFilter{.tag = EnemyTag});
    EXPECT_TRUE(enemyView.contains(enemyA));
    EXPECT_FALSE(enemyView.contains(ally));
    EXPECT_FALSE(enemyView.contains(untagged));

    // The component requirement still applies on top of the filter.
    ASSERT_TRUE(world.clearSpriteRenderer2D(enemyA));
    EXPECT_EQ(collect(world.viewWhere<SpriteRenderer2D>(
                          EntityMetadataFilter{.tag = EnemyTag}))
                  .size(),
              1U);
}

// Filtering on 0 must mean "this value", not "no filter". Zero is the default for
// all three dimensions, so conflating the two would make the defaults unqueryable.
TEST(SceneWorldTest, FilteringOnZeroDiffersFromNotFiltering)
{
    World world = makeWorld();
    const EntityId defaulted = world.createEntity().value();
    const EntityId tagged = world.createEntity().value();
    ASSERT_TRUE(world.setTag(tagged, 3));

    const auto noTag =
        world.viewWhere<LocalTransform>(EntityMetadataFilter{.tag = NoEntityTag});
    EXPECT_TRUE(noTag.contains(defaulted));
    EXPECT_FALSE(noTag.contains(tagged));

    // An unset filter field accepts both.
    const auto unfiltered = world.viewWhere<LocalTransform>(EntityMetadataFilter{});
    EXPECT_TRUE(unfiltered.contains(defaulted));
    EXPECT_TRUE(unfiltered.contains(tagged));
    EXPECT_TRUE(unfiltered.filter().filtersNothing());
}

// EntityMetadata is a readable component like any other, so a filtered query can
// still read the values it filtered on.
TEST(SceneWorldTest, FilteredQueryStillExposesMetadataToEach)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(entity, Camera2D{}));
    ASSERT_TRUE(world.setMetadata(entity, EntityMetadataDesc{
        .name = "main-camera",
        .tag = 21,
        .layer = 6,
    }));

    usize visits = 0;
    world.queryWhere<EntityMetadata, Camera2D>(EntityMetadataFilter{.layer = 6})
        .each([&](EntityId visited, const EntityMetadata& metadata, const Camera2D&) {
            ++visits;
            EXPECT_EQ(visited, entity);
            EXPECT_EQ(metadata.name.view(), "main-camera");
            EXPECT_EQ(metadata.tag, 21U);
        });
    EXPECT_EQ(visits, 1U);
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
