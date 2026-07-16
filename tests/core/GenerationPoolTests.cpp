#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>

#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Tina::Tests {
namespace {

struct EntitySlotTag;
struct NodeSlotTag;

using EntityId = Core::GenerationId<EntitySlotTag>;
using EntityPool = Core::GenerationPool<int, EntitySlotTag>;

class ObservingSlotResource final : public std::pmr::memory_resource {
public:
    bool failAllocations = false;
    usize allocationCount = 0;
    usize deallocationCount = 0;
    usize lastAlignment = 0;

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (failAllocations) {
            throw std::bad_alloc{};
        }
        ++allocationCount;
        lastAlignment = alignment;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++deallocationCount;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

struct LifetimeValue final {
    explicit LifetimeValue(int* destructionCount, int number = 0) noexcept
        : destructions(destructionCount), value(number)
    {
    }

    ~LifetimeValue() noexcept
    {
        ++*destructions;
    }

    LifetimeValue(const LifetimeValue&) = delete;
    LifetimeValue& operator=(const LifetimeValue&) = delete;

    int* destructions = nullptr;
    int value = 0;
};

struct ThrowingValue final {
    explicit ThrowingValue(bool shouldThrow)
    {
        if (shouldThrow) {
            throw std::runtime_error("construction failed");
        }
    }

    ~ThrowingValue() noexcept = default;
};

struct alignas(64) OverAlignedValue final {
    int value = 0;
};

struct ReentrantValue final {
    template <typename Callback>
    explicit ReentrantValue(Callback&& callback)
    {
        std::forward<Callback>(callback)();
    }

    ~ReentrantValue() noexcept = default;
};

TEST(GenerationIdTest, IsInvalidByDefaultStronglyTypedAndOwnerAware)
{
    static_assert(!std::is_same_v<
        Core::GenerationId<EntitySlotTag>,
        Core::GenerationId<NodeSlotTag>>);
    static_assert(!std::is_convertible_v<
        Core::GenerationId<EntitySlotTag>,
        Core::GenerationId<NodeSlotTag>>);

    const EntityId invalid;
    EXPECT_FALSE(invalid.hasValue());
    EXPECT_EQ(invalid.index(), EntityId::InvalidIndex);
    EXPECT_EQ(invalid.generation(), 0U);
    EXPECT_FALSE(invalid.owner().hasValue());

    auto ownerSource = EntityPool::Create(1);
    ASSERT_TRUE(ownerSource);
    const auto liveId = ownerSource->tryEmplace(42);
    ASSERT_TRUE(liveId);
    EXPECT_TRUE(liveId->hasValue());
    EXPECT_TRUE(liveId->owner().hasValue());
    EXPECT_EQ(liveId->index(), 0U);
    EXPECT_EQ(liveId->generation(), 1U);
}

TEST(GenerationPoolTest, RejectsInvalidConfigurationAndAllocationFailure)
{
    const auto empty = EntityPool::Create(0);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, Core::CoreErrorCode::InvalidArgument);

    if constexpr (sizeof(usize) > sizeof(u32)) {
        const usize tooLarge = static_cast<usize>((std::numeric_limits<u32>::max)()) + 1;
        const auto excessiveCapacity = EntityPool::Create(tooLarge);
        ASSERT_FALSE(excessiveCapacity);
        EXPECT_EQ(excessiveCapacity.error().code, Core::CoreErrorCode::CapacityExceeded);
    }

    ObservingSlotResource resource;
    resource.failAllocations = true;
    const auto allocationFailure = EntityPool::Create(2, resource);
    ASSERT_FALSE(allocationFailure);
    EXPECT_EQ(allocationFailure.error().code, Core::CoreErrorCode::OutOfMemory);
}

TEST(GenerationPoolTest, UsesFixedStorageAndInvalidatesReusedSlots)
{
    ObservingSlotResource resource;
    auto result = EntityPool::Create(2, resource);
    ASSERT_TRUE(result);
    EntityPool pool = std::move(*result);
    const usize creationAllocations = resource.allocationCount;

    const auto first = pool.tryEmplace(10);
    const auto second = pool.tryEmplace(20);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(pool.activeCount(), 2U);
    EXPECT_EQ(pool.availableCount(), 0U);
    EXPECT_EQ(*pool.tryGet(*first), 10);
    EXPECT_EQ(*std::as_const(pool).tryGet(*second), 20);

    const auto exhausted = pool.tryEmplace(30);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_EQ(resource.allocationCount, creationAllocations);

    EXPECT_EQ(pool.erase(*first), Core::GenerationEraseResult::Erased);
    EXPECT_EQ(pool.tryGet(*first), nullptr);
    EXPECT_EQ(pool.erase(*first), Core::GenerationEraseResult::Stale);

    const auto reused = pool.tryEmplace(30);
    ASSERT_TRUE(reused);
    EXPECT_EQ(reused->index(), first->index());
    EXPECT_EQ(reused->generation(), first->generation() + 1);
    EXPECT_EQ(*pool.tryGet(*reused), 30);
    EXPECT_EQ(resource.allocationCount, creationAllocations);
}

TEST(GenerationPoolTest, RejectsCrossPoolWrongOwnerAndInvalidIds)
{
    auto firstResult = EntityPool::Create(1);
    auto secondResult = EntityPool::Create(1);
    ASSERT_TRUE(firstResult);
    ASSERT_TRUE(secondResult);
    EntityPool firstPool = std::move(*firstResult);
    EntityPool secondPool = std::move(*secondResult);
    ASSERT_NE(firstPool.owner(), secondPool.owner());

    const auto firstId = firstPool.tryEmplace(1);
    const auto secondId = secondPool.tryEmplace(2);
    ASSERT_TRUE(firstId);
    ASSERT_TRUE(secondId);
    ASSERT_EQ(firstId->index(), secondId->index());
    ASSERT_EQ(firstId->generation(), secondId->generation());

    EXPECT_EQ(firstPool.tryGet(*secondId), nullptr);
    EXPECT_EQ(firstPool.erase(*secondId), Core::GenerationEraseResult::WrongOwner);
    EXPECT_EQ(firstPool.erase({}), Core::GenerationEraseResult::InvalidId);

    EXPECT_TRUE(firstPool.contains(*firstId));
}

TEST(GenerationPoolTest, NeverReusesAnOwnerAfterPoolDestruction)
{
    Core::GenerationOwnerToken releasedOwner;
    {
        auto firstResult = EntityPool::Create(1);
        ASSERT_TRUE(firstResult);
        releasedOwner = firstResult->owner();
    }

    const auto laterResult = EntityPool::Create(1);
    ASSERT_TRUE(laterResult);
    EXPECT_NE(laterResult->owner(), releasedOwner);
    EXPECT_GT(laterResult->owner().value(), releasedOwner.value());
}

TEST(GenerationPoolTest, RollsBackAThrowingConstructionWithoutConsumingTheSlot)
{
    using Pool = Core::GenerationPool<ThrowingValue, EntitySlotTag>;
    auto result = Pool::Create(1);
    ASSERT_TRUE(result);
    Pool pool = std::move(*result);

    const auto failure = pool.tryEmplace(true);
    ASSERT_FALSE(failure);
    EXPECT_EQ(failure.error().code, Core::CoreErrorCode::Internal);
    EXPECT_EQ(pool.activeCount(), 0U);
    EXPECT_EQ(pool.availableCount(), 1U);

    const auto success = pool.tryEmplace(false);
    ASSERT_TRUE(success);
    EXPECT_EQ(success->index(), 0U);
    EXPECT_EQ(success->generation(), 1U);
}

TEST(GenerationPoolTest, ReservesTheSlotBeforeAReentrantValueConstruction)
{
    using Pool = Core::GenerationPool<ReentrantValue, EntitySlotTag>;
    using Id = typename Pool::Id;
    auto result = Pool::Create(2);
    ASSERT_TRUE(result);
    Pool pool = std::move(*result);
    std::optional<Id> nestedId;
    usize availableBeforeNested = 0;
    usize availableAfterNested = 0;

    const auto outerId = pool.tryEmplace([&] {
        availableBeforeNested = pool.availableCount();
        const auto nested = pool.tryEmplace([] {});
        if (nested) {
            nestedId = *nested;
        }
        availableAfterNested = pool.availableCount();
    });

    ASSERT_TRUE(outerId);
    ASSERT_TRUE(nestedId.has_value());
    EXPECT_NE(outerId->index(), nestedId->index());
    EXPECT_TRUE(pool.contains(*outerId));
    EXPECT_TRUE(pool.contains(*nestedId));
    EXPECT_EQ(pool.activeCount(), 2U);
    EXPECT_EQ(availableBeforeNested, 1U);
    EXPECT_EQ(availableAfterNested, 0U);
}

TEST(GenerationPoolTest, ClearAndDestructionDestroyEachLiveValueExactlyOnce)
{
    using Pool = Core::GenerationPool<LifetimeValue, EntitySlotTag>;
    int destructions = 0;
    EntityId stale;
    {
        auto result = Pool::Create(2);
        ASSERT_TRUE(result);
        Pool pool = std::move(*result);
        const auto first = pool.tryEmplace(&destructions, 1);
        const auto second = pool.tryEmplace(&destructions, 2);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        stale = *first;

        pool.clear();
        EXPECT_EQ(destructions, 2);
        EXPECT_EQ(pool.activeCount(), 0U);
        EXPECT_EQ(pool.availableCount(), 2U);
        EXPECT_EQ(pool.tryGet(stale), nullptr);

        ASSERT_TRUE(pool.tryEmplace(&destructions, 3));
    }
    EXPECT_EQ(destructions, 3);
}

TEST(GenerationPoolTest, MovePreservesHandlesAndBackingOwnership)
{
    ObservingSlotResource resource;
    {
        auto result = EntityPool::Create(1, resource);
        ASSERT_TRUE(result);
        EntityPool source = std::move(*result);
        const auto id = source.tryEmplace(99);
        ASSERT_TRUE(id);

        EntityPool destination = std::move(source);
        ASSERT_NE(destination.tryGet(*id), nullptr);
        EXPECT_EQ(*destination.tryGet(*id), 99);
        EXPECT_EQ(source.capacity(), 0U);
    }
    EXPECT_EQ(resource.allocationCount, 1U);
    EXPECT_EQ(resource.deallocationCount, 1U);
}

TEST(GenerationPoolTest, SupportsOverAlignedValuesAndDefinesWrapRetirement)
{
    using Pool = Core::GenerationPool<OverAlignedValue, EntitySlotTag>;
    ObservingSlotResource resource;
    auto result = Pool::Create(1, resource);
    ASSERT_TRUE(result);
    Pool pool = std::move(*result);
    const auto id = pool.tryEmplace(OverAlignedValue{.value = 7});
    ASSERT_TRUE(id);
    ASSERT_NE(pool.tryGet(*id), nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr>(pool.tryGet(*id)) % alignof(OverAlignedValue), 0U);
    EXPECT_GE(resource.lastAlignment, alignof(OverAlignedValue));

    constexpr u32 Maximum = (std::numeric_limits<u32>::max)();
    static_assert(Core::Detail::nextGeneration(Maximum - 1).value() == Maximum);
    static_assert(!Core::Detail::nextGeneration(Maximum).has_value());
}

} // namespace
} // namespace Tina::Tests
