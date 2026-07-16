#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>
#include <limits>
#include <optional>

namespace Tina::Core {

template <typename Value, typename Tag>
class GenerationPool;

class GenerationOwnerToken final {
public:
    constexpr GenerationOwnerToken() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept { return m_value != 0; }
    [[nodiscard]] constexpr u32 value() const noexcept { return m_value; }
    explicit constexpr operator bool() const noexcept { return hasValue(); }

    auto operator<=>(const GenerationOwnerToken&) const = default;

private:
    template <typename Value, typename Tag>
    friend class GenerationPool;

    [[nodiscard]] static std::optional<GenerationOwnerToken> createUnique() noexcept;

    explicit constexpr GenerationOwnerToken(u32 value) noexcept
        : m_value(value)
    {
    }

    u32 m_value = 0;
};

// Runtime-only identity. Tag prevents cross-type use, generation rejects slot reuse,
// and owner rejects accidental use with another live registry of the same Tag.
template <typename Tag>
class GenerationId final {
public:
    inline static constexpr u32 InvalidIndex = (std::numeric_limits<u32>::max)();

    constexpr GenerationId() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_owner.hasValue() && m_index != InvalidIndex && m_generation != 0;
    }

    [[nodiscard]] constexpr GenerationOwnerToken owner() const noexcept { return m_owner; }
    [[nodiscard]] constexpr u32 index() const noexcept { return m_index; }
    [[nodiscard]] constexpr u32 generation() const noexcept { return m_generation; }
    explicit constexpr operator bool() const noexcept { return hasValue(); }

    auto operator<=>(const GenerationId&) const = default;

private:
    template <typename Value, typename OtherTag>
    friend class GenerationPool;

    [[nodiscard]] static constexpr GenerationId createForPool(
        GenerationOwnerToken owner,
        u32 index,
        u32 generation) noexcept
    {
        return GenerationId(owner, index, generation);
    }

    constexpr GenerationId(
        GenerationOwnerToken owner,
        u32 index,
        u32 generation) noexcept
        : m_owner(owner), m_index(index), m_generation(generation)
    {
    }

    GenerationOwnerToken m_owner{};
    u32 m_index = InvalidIndex;
    u32 m_generation = 0;
};

namespace Detail {

[[nodiscard]] constexpr std::optional<u32> nextGeneration(u32 generation) noexcept
{
    if (generation == 0 || generation == (std::numeric_limits<u32>::max)()) {
        return std::nullopt;
    }
    return generation + 1;
}

} // namespace Detail

} // namespace Tina::Core
