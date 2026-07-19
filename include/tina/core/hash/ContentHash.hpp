#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <optional>

namespace Tina::Core {

class ContentHash final {
  public:
    using Bytes = std::array<std::byte, 16>;

    constexpr ContentHash() noexcept = default;

    [[nodiscard]] static constexpr std::optional<ContentHash> fromBytes(Bytes bytes) noexcept
    {
        for (const auto value : bytes)
        {
            if (value != std::byte{0})
            {
                return ContentHash(bytes);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        for (const auto value : m_bytes)
        {
            if (value != std::byte{0})
            {
                return true;
            }
        }
        return false;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }
    [[nodiscard]] constexpr const Bytes& bytes() const noexcept
    {
        return m_bytes;
    }

    auto operator<=>(const ContentHash&) const = default;

  private:
    explicit constexpr ContentHash(Bytes bytes) noexcept : m_bytes(bytes)
    {
    }

    Bytes m_bytes{};
};

} // namespace Tina::Core
