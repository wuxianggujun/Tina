#pragma once

#include <tina/ai/AIErrors.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/math/Vec.hpp>

#include <concepts>
#include <limits>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <variant>

namespace Tina::AI {

template <typename T>
concept BlackboardValueType = std::same_as<T, bool> || std::same_as<T, Core::i64> ||
    std::same_as<T, Core::u64> || std::same_as<T, float> || std::same_as<T, double> ||
    std::same_as<T, Math::Vec2> || std::same_as<T, Math::Vec3>;

template <BlackboardValueType T>
struct BlackboardKey final {
    Core::u32 slot = (std::numeric_limits<Core::u32>::max)();
    friend constexpr bool operator==(const BlackboardKey&, const BlackboardKey&) noexcept = default;
};

struct BlackboardConfig final {
    Core::usize slotCapacity = 64;
};

// An O(1), instance-local typed slot table. The first successful write fixes a
// slot's type for its lifetime; clearing a value does not permit retyping it.
// No strings, any, engine service lookup, hidden allocation, or borrowed values.
class Blackboard final {
public:
    [[nodiscard]] static Core::Result<Blackboard> Create(
        BlackboardConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
    ~Blackboard() noexcept = default;
    Blackboard(const Blackboard&) = delete;
    Blackboard& operator=(const Blackboard&) = delete;
    Blackboard(Blackboard&& other) noexcept;
    Blackboard& operator=(Blackboard&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_capacity != 0; }
    [[nodiscard]] Core::usize capacity() const noexcept { return m_capacity; }
    [[nodiscard]] Core::usize valueCount() const noexcept { return m_count; }

    template <BlackboardValueType T>
    [[nodiscard]] Core::Status set(BlackboardKey<T> key, std::type_identity_t<T> value)
    {
        return setValue(key.slot, Value{value});
    }

    template <BlackboardValueType T>
    [[nodiscard]] Core::Result<T> get(BlackboardKey<T> key) const
    {
        auto value = getValue(key.slot);
        if (!value) { return Core::failure(std::move(value.error())); }
        if (const auto* typed = std::get_if<T>(&*value)) { return *typed; }
        return Core::failure(AIErrorCode::TypeMismatch, "blackboard key type differs from the slot type");
    }

    template <BlackboardValueType T>
    [[nodiscard]] bool contains(BlackboardKey<T> key) const noexcept
    {
        return containsValue(key.slot, Value{T{}}.index());
    }

    template <BlackboardValueType T>
    [[nodiscard]] Core::Status clear(BlackboardKey<T> key)
    {
        return clearValue(key.slot, Value{T{}}.index());
    }

    void clearValues() noexcept;

private:
    using Value = std::variant<bool, Core::i64, Core::u64, float, double, Math::Vec2, Math::Vec3>;
    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    Blackboard(Core::usize capacity, StorageOwner storage) noexcept;
    [[nodiscard]] Core::Status setValue(Core::u32 slot, Value value);
    [[nodiscard]] Core::Result<Value> getValue(Core::u32 slot) const;
    [[nodiscard]] bool containsValue(Core::u32 slot, Core::usize type) const noexcept;
    [[nodiscard]] Core::Status clearValue(Core::u32 slot, Core::usize type);

    StorageOwner m_storage{nullptr, &destroyStorage};
    Core::usize m_capacity = 0;
    Core::usize m_count = 0;
};

} // namespace Tina::AI
