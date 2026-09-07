#include <tina/ai/Blackboard.hpp>

#include <cmath>
#include <exception>
#include <new>
#include <utility>
#include <vector>

namespace Tina::AI {

struct Blackboard::Storage final {
    inline static constexpr Core::usize UnboundType = (std::numeric_limits<Core::usize>::max)();
    struct Slot final {
        Value value{};
        Core::usize type = UnboundType;
        bool present = false;
    };
    Storage(Core::usize capacity, std::pmr::memory_resource& memory)
        : resource(&memory), slots(capacity, &memory) {}
    std::pmr::memory_resource* resource;
    std::pmr::vector<Slot> slots;
};

void Blackboard::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

Blackboard::Blackboard(Core::usize capacity, StorageOwner storage) noexcept
    : m_storage(std::move(storage)), m_capacity(capacity) {}

Blackboard::Blackboard(Blackboard&& other) noexcept
    : m_storage(std::move(other.m_storage)), m_capacity(std::exchange(other.m_capacity, 0)),
      m_count(std::exchange(other.m_count, 0)) {}

Core::Result<Blackboard> Blackboard::Create(BlackboardConfig config, std::pmr::memory_resource& resource)
{
    constexpr Core::usize maximumSlots = 65536;
    if (config.slotCapacity == 0 || config.slotCapacity > maximumSlots)
    {
        return Core::failure(AIErrorCode::CapacityExceeded, "blackboard capacity must be in [1, 65536]");
    }
    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 config.slotCapacity, resource), &destroyStorage};
        return Blackboard(config.slotCapacity, std::move(storage));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AIErrorCode::AllocationFailed, "blackboard fixed storage allocation failed");
    }
    catch (const std::exception& exception)
    {
        return Core::failure(AIErrorCode::AllocationFailed, exception.what());
    }
}

Core::Status Blackboard::setValue(Core::u32 index, Value value)
{
    if (index >= m_capacity) { return Core::failure(AIErrorCode::InvalidKey, "blackboard key is outside slot capacity"); }
    const bool finite = std::visit([](const auto& item) noexcept {
        using T = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::floating_point<T>) { return std::isfinite(item); }
        else if constexpr (std::same_as<T, Math::Vec2> || std::same_as<T, Math::Vec3>) { return Math::isFinite(item); }
        else { return true; }
    }, value);
    if (!finite) { return Core::failure(AIErrorCode::InvalidConfig, "blackboard numeric values must be finite"); }
    auto& slot = m_storage->slots[index];
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
    if (index >= m_capacity) { return Core::failure(AIErrorCode::InvalidKey, "blackboard key is outside slot capacity"); }
    const auto& slot = m_storage->slots[index];
    if (!slot.present) { return Core::failure(AIErrorCode::MissingValue, "blackboard slot has no value"); }
    return slot.value;
}

bool Blackboard::containsValue(Core::u32 index, Core::usize type) const noexcept
{
    return index < m_capacity && m_storage->slots[index].present && m_storage->slots[index].type == type;
}

Core::Status Blackboard::clearValue(Core::u32 index, Core::usize type)
{
    if (index >= m_capacity) { return Core::failure(AIErrorCode::InvalidKey, "blackboard key is outside slot capacity"); }
    auto& slot = m_storage->slots[index];
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
    if (m_storage) { for (auto& slot : m_storage->slots) { slot.present = false; } }
    m_count = 0;
}

} // namespace Tina::AI
