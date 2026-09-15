#include <tina/ai/Blackboard.hpp>

#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Tina::AI {

struct Blackboard::Storage final {
    inline static constexpr Core::usize UnboundType = (std::numeric_limits<Core::usize>::max)();
    inline static constexpr Core::usize EmptyTable = 0;

    struct Slot final {
        Core::u32 key = 0;
        Value value{};
        Core::usize type = UnboundType;
        bool occupied = false;
        bool present = false;
    };

    struct Table final {
        std::pmr::memory_resource* resource = nullptr;
        Slot* data = nullptr;
        Core::usize size = 0;

        explicit Table(std::pmr::memory_resource& memory) noexcept : resource(&memory) {}
        Table(const Table&) = delete;
        Table& operator=(const Table&) = delete;

        Table(Table&& other) noexcept
            : resource(other.resource),
              data(std::exchange(other.data, nullptr)),
              size(std::exchange(other.size, 0)) {}

        Table& operator=(Table&& other) noexcept
        {
            if (this == &other) {
                return *this;
            }
            reset();
            resource = other.resource;
            data = std::exchange(other.data, nullptr);
            size = std::exchange(other.size, 0);
            return *this;
        }

        ~Table() { reset(); }

        void reset() noexcept
        {
            if (data == nullptr) {
                return;
            }
            std::destroy_n(data, size);
            resource->deallocate(data, size * sizeof(Slot), alignof(Slot));
            data = nullptr;
            size = 0;
        }

        [[nodiscard]] static Table make(std::pmr::memory_resource& memory, Core::usize count)
        {
            Table table(memory);
            if (count == 0) {
                return table;
            }
            if (count > (std::numeric_limits<Core::usize>::max)() / sizeof(Slot)) {
                throw std::length_error{"blackboard reserve exceeds addressable storage"};
            }
            auto* slots = static_cast<Slot*>(memory.allocate(count * sizeof(Slot), alignof(Slot)));
            Core::usize constructed = 0;
            try {
                for (; constructed < count; ++constructed) {
                    std::construct_at(slots + constructed);
                }
            } catch (...) {
                std::destroy_n(slots, constructed);
                memory.deallocate(slots, count * sizeof(Slot), alignof(Slot));
                throw;
            }
            table.data = slots;
            table.size = count;
            return table;
        }

        [[nodiscard]] Slot* begin() noexcept { return data; }
        [[nodiscard]] Slot* end() noexcept { return data + size; }
        [[nodiscard]] const Slot* begin() const noexcept { return data; }
        [[nodiscard]] const Slot* end() const noexcept { return data + size; }
        [[nodiscard]] Slot& operator[](Core::usize index) noexcept { return data[index]; }
        [[nodiscard]] const Slot& operator[](Core::usize index) const noexcept { return data[index]; }
        [[nodiscard]] bool empty() const noexcept { return size == 0; }
    };

    Storage(Core::usize initialReserve, std::pmr::memory_resource& memory)
        : resource(&memory), buckets(memory)
    {
        if (initialReserve == EmptyTable) {
            return;
        }
        growAt = initialReserve;
        buckets = Table::make(memory, bucketCountFor(growAt));
    }

    [[nodiscard]] static Core::usize bucketCountFor(Core::usize occupiedLimit)
    {
        if (occupiedLimit > (std::numeric_limits<Core::usize>::max)() / 2U) {
            throw std::length_error{"blackboard reserve exceeds addressable storage"};
        }
        Core::usize needed = occupiedLimit * 2U;
        Core::usize size = 2U;
        while (size < needed) {
            if (size > (std::numeric_limits<Core::usize>::max)() / 2U) {
                throw std::length_error{"blackboard reserve exceeds addressable storage"};
            }
            size *= 2U;
        }
        return size;
    }

    [[nodiscard]] static Core::usize mix(Core::u32 key) noexcept
    {
        Core::u64 hash = static_cast<Core::u64>(key) + 0x9e3779b97f4a7c15ULL;
        hash = (hash ^ (hash >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        hash = (hash ^ (hash >> 27U)) * 0x94d049bb133111ebULL;
        return static_cast<Core::usize>(hash ^ (hash >> 31U));
    }

    [[nodiscard]] Slot* lookup(Core::u32 key) noexcept
    {
        return const_cast<Slot*>(std::as_const(*this).lookup(key));
    }

    [[nodiscard]] const Slot* lookup(Core::u32 key) const noexcept
    {
        if (buckets.empty()) {
            return nullptr;
        }
        const Core::usize mask = buckets.size - 1U;
        Core::usize index = mix(key) & mask;
        for (Core::usize probe = 0; probe < buckets.size; ++probe) {
            const Slot& slot = buckets[index];
            if (!slot.occupied) {
                return nullptr;
            }
            if (slot.key == key) {
                return &slot;
            }
            index = (index + 1U) & mask;
        }
        return nullptr;
    }

    void grow()
    {
        const Core::usize nextSize = buckets.empty() ? 8U : bucketCountFor(buckets.size);
        Table next = Table::make(*resource, nextSize);
        const Core::usize mask = next.size - 1U;
        for (const Slot& slot : buckets) {
            if (!slot.occupied) {
                continue;
            }
            Core::usize index = mix(slot.key) & mask;
            while (next[index].occupied) {
                index = (index + 1U) & mask;
            }
            next[index] = slot;
        }
        buckets = std::move(next);
        growAt = buckets.size / 2U;
    }

    Slot& bind(Core::u32 key)
    {
        if (Slot* existing = lookup(key)) {
            return *existing;
        }
        if (boundCount >= growAt) {
            grow();
        }
        const Core::usize mask = buckets.size - 1U;
        Core::usize index = mix(key) & mask;
        while (buckets[index].occupied) {
            index = (index + 1U) & mask;
        }
        Slot& slot = buckets[index];
        slot.key = key;
        slot.occupied = true;
        ++boundCount;
        return slot;
    }

    std::pmr::memory_resource* resource;
    Table buckets;
    Core::usize boundCount = 0;
    Core::usize growAt = 0;
};

void Blackboard::destroyStorage(Storage* storage) noexcept
{
    if (storage == nullptr) {
        return;
    }
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

Blackboard::Blackboard(StorageOwner storage) noexcept : m_storage(std::move(storage)) {}

Blackboard::Blackboard(Blackboard&& other) noexcept
    : m_storage(std::move(other.m_storage)),
      m_count(std::exchange(other.m_count, 0)) {}

Core::Result<Blackboard> Blackboard::Create(BlackboardConfig config, std::pmr::memory_resource& resource)
{
    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 config.initialSlotReserve, resource), &destroyStorage};
        return Blackboard(std::move(storage));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AIErrorCode::AllocationFailed, "blackboard storage allocation failed");
    }
    catch (const std::length_error&)
    {
        return Core::failure(AIErrorCode::CapacityExceeded, "blackboard reserve exceeds addressable storage");
    }
    catch (const std::exception& exception)
    {
        return Core::failure(AIErrorCode::AllocationFailed, exception.what());
    }
}

Core::Status Blackboard::setValue(Core::u32 index, Value value)
{
    if (!m_storage || index == (std::numeric_limits<Core::u32>::max)()) {
        return Core::failure(AIErrorCode::InvalidKey, "blackboard requires a live owner and a valid key");
    }
    const bool finite = std::visit([](const auto& item) noexcept {
        using T = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::floating_point<T>) { return std::isfinite(item); }
        else if constexpr (std::same_as<T, Math::Vec2> || std::same_as<T, Math::Vec3>) { return Math::isFinite(item); }
        else { return true; }
    }, value);
    if (!finite) { return Core::failure(AIErrorCode::InvalidConfig, "blackboard numeric values must be finite"); }
    Storage::Slot* stored = nullptr;
    try {
        stored = &m_storage->bind(index);
    } catch (const std::bad_alloc&) {
        return Core::failure(AIErrorCode::AllocationFailed, "blackboard key allocation failed");
    } catch (const std::length_error&) {
        return Core::failure(AIErrorCode::CapacityExceeded, "blackboard key storage is exhausted");
    }
    auto& slot = *stored;
    if (slot.type != Storage::UnboundType && slot.type != value.index())
    {
        return Core::failure(AIErrorCode::TypeMismatch, "blackboard slot type cannot be changed");
    }
    slot.type = value.index();
    slot.value = value;
    if (!slot.present) { ++m_count; }
    slot.present = true;
    return Core::success();
}

Core::Result<Blackboard::Value> Blackboard::getValue(Core::u32 index) const
{
    if (!m_storage || index == (std::numeric_limits<Core::u32>::max)()) {
        return Core::failure(AIErrorCode::InvalidKey, "blackboard requires a live owner and a valid key");
    }
    const auto* found = m_storage->lookup(index);
    if (found == nullptr) {
        return Core::failure(AIErrorCode::MissingValue, "blackboard key has not been written");
    }
    if (!found->present) { return Core::failure(AIErrorCode::MissingValue, "blackboard slot has no value"); }
    return found->value;
}

bool Blackboard::containsValue(Core::u32 index, Core::usize type) const noexcept
{
    if (!m_storage) { return false; }
    const auto* found = m_storage->lookup(index);
    return found != nullptr && found->present && found->type == type;
}

Core::Status Blackboard::clearValue(Core::u32 index, Core::usize type)
{
    if (!m_storage || index == (std::numeric_limits<Core::u32>::max)()) {
        return Core::failure(AIErrorCode::InvalidKey, "blackboard requires a live owner and a valid key");
    }
    auto* found = m_storage->lookup(index);
    if (found == nullptr) { return Core::success(); }
    auto& slot = *found;
    if (slot.type != Storage::UnboundType && slot.type != type)
    {
        return Core::failure(AIErrorCode::TypeMismatch, "blackboard key type differs from the slot type");
    }
    if (slot.present) { --m_count; }
    slot.present = false;
    return Core::success();
}

void Blackboard::clearValues() noexcept
{
    if (m_storage) {
        for (auto& slot : m_storage->buckets) {
            slot.present = false;
        }
    }
    m_count = 0;
}

Core::usize Blackboard::boundSlotCount() const noexcept
{
    return m_storage ? m_storage->boundCount : 0;
}

} // namespace Tina::AI
