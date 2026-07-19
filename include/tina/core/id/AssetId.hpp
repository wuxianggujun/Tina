#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <optional>
#include <string_view>

namespace Tina::Core {

class AssetId final {
  public:
    using Bytes = std::array<std::byte, 16>;
    using CanonicalText = std::array<char, 32>;

    constexpr AssetId() noexcept = default;

    [[nodiscard]] static constexpr std::optional<AssetId> fromBytes(Bytes bytes) noexcept
    {
        if (isZero(bytes))
        {
            return std::nullopt;
        }
        return AssetId(bytes);
    }

    [[nodiscard]] static constexpr std::optional<AssetId> parseCanonical(std::string_view text) noexcept
    {
        if (text.size() != 32U)
        {
            return std::nullopt;
        }

        Bytes bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const auto high = hexValue(text[index * 2U]);
            const auto low = hexValue(text[index * 2U + 1U]);
            if (!high || !low)
            {
                return std::nullopt;
            }
            bytes[index] = static_cast<std::byte>((*high << 4U) | *low);
        }
        return fromBytes(bytes);
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return !isZero(m_bytes);
    }
    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    [[nodiscard]] constexpr const Bytes& bytes() const noexcept
    {
        return m_bytes;
    }

    [[nodiscard]] constexpr CanonicalText canonicalText() const noexcept
    {
        constexpr char Digits[] = "0123456789abcdef";
        CanonicalText text{};
        for (std::size_t index = 0; index < m_bytes.size(); ++index)
        {
            const auto value = std::to_integer<unsigned int>(m_bytes[index]);
            text[index * 2U] = Digits[(value >> 4U) & 0x0FU];
            text[index * 2U + 1U] = Digits[value & 0x0FU];
        }
        return text;
    }

    auto operator<=>(const AssetId&) const = default;

  private:
    [[nodiscard]] static constexpr bool isZero(const Bytes& bytes) noexcept
    {
        for (const auto value : bytes)
        {
            if (value != std::byte{0})
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr std::optional<unsigned int> hexValue(char value) noexcept
    {
        if (value >= '0' && value <= '9')
        {
            return static_cast<unsigned int>(value - '0');
        }
        if (value >= 'a' && value <= 'f')
        {
            return static_cast<unsigned int>(value - 'a') + 10U;
        }
        return std::nullopt;
    }

    explicit constexpr AssetId(Bytes bytes) noexcept : m_bytes(bytes)
    {
    }

    Bytes m_bytes{};
};

} // namespace Tina::Core
