#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationId.hpp>

#include <exception>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Tina::Core {

enum class GenerationEraseResult : u8 {
    Erased,
    InvalidId,
    WrongOwner,
    OutOfRange,
    Stale,
};

// Owner-aware stable slot storage. reserve() grows in separate blocks, never by
// relocating live values. tryEmplace() itself does not grow: realtime owners can
// preallocate, while ordinary registries reserve more before accepting new work.
// The resource must outlive the pool. Mutation is single-owner; resolved pointers
// survive reserve() and moves, but not erase(), clear(), or destruction.
template <typename Value, typename Tag>
class GenerationPool final {
public:
    using Id = GenerationId<Tag>;

    static_assert(
        std::is_nothrow_destructible_v<Value>,
        "GenerationPool values must have noexcept destructors");

    [[nodiscard]] static Result<GenerationPool> Create(
        usize capacity,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
    {
        if (capacity > Id::InvalidIndex) {
            return failure(
                CoreErrorCode::CapacityExceeded,
                "GenerationPool capacity exceeds the 32-bit index range");
        }
        if (capacity > (std::numeric_limits<usize>::max)() / sizeof(Slot)) {
            return failure(
                CoreErrorCode::CapacityExceeded,
                "GenerationPool slot storage size overflowed");
        }
        const std::optional<GenerationOwnerToken> owner = GenerationOwnerToken::createUnique();
        if (!owner.has_value()) {
            return failure(
                CoreErrorCode::CapacityExceeded,
                "GenerationPool owner token space is exhausted");
        }

        try {
            return GenerationPool(capacity, *owner, resource);
        } catch (const std::bad_alloc&) {
            return failure(
                CoreErrorCode::OutOfMemory,
                "GenerationPool slot allocation failed");
        } catch (const std::exception& exception) {
            return failure(CoreErrorCode::Internal, std::string_view(exception.what()));
        } catch (...) {
            return failure(
                CoreErrorCode::Internal,
                "GenerationPool slot allocation failed with an unknown exception");
        }
    }

    ~GenerationPool() noexcept
    {
        destroyOccupiedValues();
        releaseSlots();
    }

    GenerationPool(const GenerationPool&) = delete;
    GenerationPool& operator=(const GenerationPool&) = delete;

    GenerationPool(GenerationPool&& other) noexcept
        : m_resource(std::exchange(other.m_resource, nullptr)),
          m_slots(std::exchange(other.m_slots, nullptr)),
          m_firstCapacity(std::exchange(other.m_firstCapacity, 0)),
          m_extraBlocks(std::exchange(other.m_extraBlocks, nullptr)),
          m_capacity(std::exchange(other.m_capacity, 0)),
          m_owner(std::exchange(other.m_owner, {})),
          m_freeHead(std::exchange(other.m_freeHead, Id::InvalidIndex)),
          m_activeCount(std::exchange(other.m_activeCount, 0)),
          m_constructingCount(std::exchange(other.m_constructingCount, 0)),
          m_retiredCount(std::exchange(other.m_retiredCount, 0))
    {
    }

    GenerationPool& operator=(GenerationPool&&) = delete;

    // At least minimumCapacity slots, with amortized growth after the first block.
    // Failure preserves every value, handle, count, and the old reusable slots.
    [[nodiscard]] Status reserve(usize minimumCapacity)
    {
        if (!m_owner.hasValue()) {
            return failure(CoreErrorCode::InvalidArgument, "cannot reserve a moved-from GenerationPool");
        }
        if (minimumCapacity <= m_capacity) {
            return success();
        }
        constexpr usize maximumByBytes = (std::numeric_limits<usize>::max)() / sizeof(Slot);
        constexpr usize maximum = maximumByBytes < Id::InvalidIndex ? maximumByBytes : Id::InvalidIndex;
        if (minimumCapacity > maximum) {
            return failure(CoreErrorCode::CapacityExceeded, "GenerationPool slot index or storage size overflowed");
        }
        const usize geometric = m_capacity == 0 ? minimumCapacity
            : (m_capacity > maximum - m_capacity ? maximum : m_capacity * 2);
        const usize nextCapacity = minimumCapacity > geometric ? minimumCapacity : geometric;
        const usize count = nextCapacity - m_capacity;
        SlotBlock* block = nullptr;
        try {
            if (m_slots == nullptr) {
                Slot* slots = allocateSlots(count, 0, m_freeHead);
                m_slots = slots;
                m_firstCapacity = count;
            } else {
                block = std::pmr::polymorphic_allocator<SlotBlock>{m_resource}.template new_object<SlotBlock>();
                block->slots = allocateSlots(count, m_capacity, m_freeHead);
                block->firstIndex = m_capacity;
                block->count = count;
                block->previous = m_extraBlocks;
                m_extraBlocks = block;
            }
        } catch (const std::bad_alloc&) {
            if (block != nullptr) {
                std::pmr::polymorphic_allocator<SlotBlock>{m_resource}.delete_object(block);
            }
            return failure(CoreErrorCode::OutOfMemory, "GenerationPool growth allocation failed");
        } catch (const std::exception& exception) {
            if (block != nullptr) {
                std::pmr::polymorphic_allocator<SlotBlock>{m_resource}.delete_object(block);
            }
            return failure(CoreErrorCode::Internal, std::string_view(exception.what()));
        } catch (...) {
            if (block != nullptr) {
                std::pmr::polymorphic_allocator<SlotBlock>{m_resource}.delete_object(block);
            }
            return failure(CoreErrorCode::Internal, "GenerationPool growth allocation threw an unknown exception");
        }
        m_freeHead = static_cast<u32>(m_capacity);
        m_capacity = nextCapacity;
        return success();
    }

    template <typename... Arguments>
    [[nodiscard]] Result<Id> tryEmplace(Arguments&&... arguments)
    {
        if (m_freeHead == Id::InvalidIndex) {
            return failure(
                CoreErrorCode::CapacityExceeded,
                "GenerationPool has no reusable slots");
        }

        const u32 index = m_freeHead;
        Slot& slot = slotAt(index);
        m_freeHead = slot.nextFree;
        slot.nextFree = Id::InvalidIndex;
        slot.state = SlotState::Constructing;
        ++m_constructingCount;
        try {
            std::construct_at(constructionPointer(slot), std::forward<Arguments>(arguments)...);
        } catch (const std::bad_alloc&) {
            rollbackConstruction(index, slot);
            return failure(
                CoreErrorCode::OutOfMemory,
                "GenerationPool value construction ran out of memory");
        } catch (const std::exception& exception) {
            rollbackConstruction(index, slot);
            return failure(CoreErrorCode::Internal, std::string_view(exception.what()));
        } catch (...) {
            rollbackConstruction(index, slot);
            return failure(
                CoreErrorCode::Internal,
                "GenerationPool value construction failed with an unknown exception");
        }

        slot.state = SlotState::Occupied;
        --m_constructingCount;
        ++m_activeCount;
        return Id::createForPool(m_owner, index, slot.generation);
    }

    [[nodiscard]] Value* tryGet(Id id) noexcept
    {
        Slot* slot = tryFindOccupiedSlot(id);
        return slot == nullptr ? nullptr : valuePointer(*slot);
    }

    [[nodiscard]] const Value* tryGet(Id id) const noexcept
    {
        const Slot* slot = tryFindOccupiedSlot(id);
        return slot == nullptr ? nullptr : valuePointer(*slot);
    }

    [[nodiscard]] bool contains(Id id) const noexcept
    {
        return tryFindOccupiedSlot(id) != nullptr;
    }

    [[nodiscard]] GenerationEraseResult erase(Id id) noexcept
    {
        const GenerationEraseResult validation = validateForErase(id);
        if (validation != GenerationEraseResult::Erased) {
            return validation;
        }

        Slot& slot = slotAt(id.index());
        std::destroy_at(valuePointer(slot));
        --m_activeCount;

        const std::optional<u32> next = Detail::nextGeneration(slot.generation);
        if (!next.has_value()) {
            slot.state = SlotState::Retired;
            slot.nextFree = Id::InvalidIndex;
            ++m_retiredCount;
            return GenerationEraseResult::Erased;
        }

        slot.generation = *next;
        slot.state = SlotState::Free;
        slot.nextFree = m_freeHead;
        m_freeHead = id.index();
        return GenerationEraseResult::Erased;
    }

    void clear() noexcept
    {
        for (usize index = 0; index < m_capacity; ++index) {
            Slot& slot = slotAt(index);
            if (slot.state != SlotState::Occupied) {
                continue;
            }

            std::destroy_at(valuePointer(slot));
            const std::optional<u32> next = Detail::nextGeneration(slot.generation);
            if (!next.has_value()) {
                slot.state = SlotState::Retired;
                ++m_retiredCount;
            } else {
                slot.generation = *next;
                slot.state = SlotState::Free;
            }
        }
        m_activeCount = 0;
        rebuildFreeList();
    }

    [[nodiscard]] GenerationOwnerToken owner() const noexcept { return m_owner; }
    [[nodiscard]] usize activeCount() const noexcept { return m_activeCount; }
    [[nodiscard]] usize retiredCount() const noexcept { return m_retiredCount; }
    [[nodiscard]] usize capacity() const noexcept { return m_capacity; }
    [[nodiscard]] usize availableCount() const noexcept
    {
        return capacity() - activeCount() - m_constructingCount - retiredCount();
    }

private:
    enum class SlotState : u8 {
        Free,
        Constructing,
        Occupied,
        Retired,
    };

    struct Slot final {
        alignas(Value) std::byte storage[sizeof(Value)];
        u32 generation = 1;
        u32 nextFree = Id::InvalidIndex;
        SlotState state = SlotState::Free;
    };

    struct SlotBlock final {
        Slot* slots = nullptr;
        usize firstIndex = 0;
        usize count = 0;
        SlotBlock* previous = nullptr;
    };

    GenerationPool(
        usize capacity,
        GenerationOwnerToken owner,
        std::pmr::memory_resource& resource)
        : m_resource(&resource), m_firstCapacity(capacity), m_capacity(capacity), m_owner(owner),
          m_freeHead(capacity == 0 ? Id::InvalidIndex : 0)
    {
        if (capacity != 0) {
            m_slots = allocateSlots(capacity, 0, Id::InvalidIndex);
        }
    }

    [[nodiscard]] Slot* allocateSlots(usize count, usize firstIndex, u32 tail)
    {
        static_assert(std::is_nothrow_default_constructible_v<Slot>);
        auto* slots = static_cast<Slot*>(m_resource->allocate(count * sizeof(Slot), alignof(Slot)));
        for (usize index = 0; index < count; ++index) {
            std::construct_at(slots + index);
            slots[index].nextFree = index + 1 < count ? static_cast<u32>(firstIndex + index + 1) : tail;
        }
        return slots;
    }

    [[nodiscard]] Slot& slotAt(usize index) noexcept
    {
        return const_cast<Slot&>(std::as_const(*this).slotAt(index));
    }

    [[nodiscard]] const Slot& slotAt(usize index) const noexcept
    {
        if (index < m_firstCapacity) {
            return m_slots[index];
        }
        const SlotBlock* block = m_extraBlocks;
        while (index < block->firstIndex) {
            block = block->previous;
        }
        return block->slots[index - block->firstIndex];
    }

    [[nodiscard]] static Value* valuePointer(Slot& slot) noexcept
    {
        return std::launder(reinterpret_cast<Value*>(slot.storage));
    }

    [[nodiscard]] static Value* constructionPointer(Slot& slot) noexcept
    {
        return reinterpret_cast<Value*>(slot.storage);
    }

    [[nodiscard]] static const Value* valuePointer(const Slot& slot) noexcept
    {
        return std::launder(reinterpret_cast<const Value*>(slot.storage));
    }

    [[nodiscard]] Slot* tryFindOccupiedSlot(Id id) noexcept
    {
        if (!id.hasValue() || id.owner() != m_owner || id.index() >= m_capacity) {
            return nullptr;
        }
        Slot& slot = slotAt(id.index());
        if (slot.state != SlotState::Occupied || slot.generation != id.generation()) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] const Slot* tryFindOccupiedSlot(Id id) const noexcept
    {
        if (!id.hasValue() || id.owner() != m_owner || id.index() >= m_capacity) {
            return nullptr;
        }
        const Slot& slot = slotAt(id.index());
        if (slot.state != SlotState::Occupied || slot.generation != id.generation()) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] GenerationEraseResult validateForErase(Id id) const noexcept
    {
        if (!id.hasValue()) {
            return GenerationEraseResult::InvalidId;
        }
        if (id.owner() != m_owner) {
            return GenerationEraseResult::WrongOwner;
        }
        if (id.index() >= m_capacity) {
            return GenerationEraseResult::OutOfRange;
        }
        const Slot& slot = slotAt(id.index());
        if (slot.state != SlotState::Occupied || slot.generation != id.generation()) {
            return GenerationEraseResult::Stale;
        }
        return GenerationEraseResult::Erased;
    }

    void rebuildFreeList() noexcept
    {
        m_freeHead = Id::InvalidIndex;
        for (usize reverseIndex = m_capacity; reverseIndex > 0; --reverseIndex) {
            const u32 index = static_cast<u32>(reverseIndex - 1);
            Slot& slot = slotAt(index);
            if (slot.state == SlotState::Free) {
                slot.nextFree = m_freeHead;
                m_freeHead = index;
            } else {
                slot.nextFree = Id::InvalidIndex;
            }
        }
    }

    void rollbackConstruction(u32 index, Slot& slot) noexcept
    {
        --m_constructingCount;
        slot.state = SlotState::Free;
        slot.nextFree = m_freeHead;
        m_freeHead = index;
    }

    void destroyOccupiedValues() noexcept
    {
        for (usize index = 0; index < m_capacity; ++index) {
            Slot& slot = slotAt(index);
            if (slot.state == SlotState::Occupied) {
                std::destroy_at(valuePointer(slot));
                slot.state = SlotState::Free;
            }
        }
        m_activeCount = 0;
    }

    void releaseSlots() noexcept
    {
        while (m_extraBlocks != nullptr) {
            SlotBlock* block = m_extraBlocks;
            m_extraBlocks = block->previous;
            std::destroy_n(block->slots, block->count);
            m_resource->deallocate(block->slots, block->count * sizeof(Slot), alignof(Slot));
            std::pmr::polymorphic_allocator<SlotBlock>{m_resource}.delete_object(block);
        }
        if (m_slots != nullptr) {
            std::destroy_n(m_slots, m_firstCapacity);
            m_resource->deallocate(m_slots, m_firstCapacity * sizeof(Slot), alignof(Slot));
        }
        m_resource = nullptr;
        m_slots = nullptr;
        m_firstCapacity = 0;
        m_capacity = 0;
    }

    std::pmr::memory_resource* m_resource = nullptr;
    Slot* m_slots = nullptr;
    usize m_firstCapacity = 0;
    SlotBlock* m_extraBlocks = nullptr;
    usize m_capacity = 0;
    GenerationOwnerToken m_owner{};
    u32 m_freeHead = Id::InvalidIndex;
    usize m_activeCount = 0;
    usize m_constructingCount = 0;
    usize m_retiredCount = 0;
};

} // namespace Tina::Core
