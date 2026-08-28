#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <compare>
#include <span>
#include <string_view>

namespace Tina::Network {

enum class IpFamily : Core::u8 {
    Unspecified = 0,
    V4 = 1,
    V6 = 2,
};

// Numeric transport address. This slice performs no name resolution, so an
// endpoint is always a literal address plus a port; there is no hostname field
// to accidentally leave unresolved.
//
// Addresses are stored in network byte order. V4 occupies the first four bytes.
class IpAddress final {
  public:
    static constexpr Core::usize V4ByteCount = 4;
    static constexpr Core::usize V6ByteCount = 16;
    // "xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255" plus terminator.
    static constexpr Core::usize MaximumTextLength = 46;

    constexpr IpAddress() noexcept = default;

    [[nodiscard]] static constexpr IpAddress v4(
        Core::u8 byte0,
        Core::u8 byte1,
        Core::u8 byte2,
        Core::u8 byte3) noexcept
    {
        IpAddress address;
        address.m_family = IpFamily::V4;
        address.m_bytes[0] = byte0;
        address.m_bytes[1] = byte1;
        address.m_bytes[2] = byte2;
        address.m_bytes[3] = byte3;
        return address;
    }

    [[nodiscard]] static constexpr IpAddress v4Loopback() noexcept
    {
        return v4(127, 0, 0, 1);
    }

    [[nodiscard]] static constexpr IpAddress v4Any() noexcept
    {
        return v4(0, 0, 0, 0);
    }

    [[nodiscard]] static constexpr IpAddress v6(
        const std::array<Core::u8, V6ByteCount>& bytes) noexcept
    {
        IpAddress address;
        address.m_family = IpFamily::V6;
        address.m_bytes = bytes;
        return address;
    }

    [[nodiscard]] static constexpr IpAddress v6Loopback() noexcept
    {
        std::array<Core::u8, V6ByteCount> bytes{};
        bytes[15] = 1;
        return v6(bytes);
    }

    [[nodiscard]] static constexpr IpAddress v6Any() noexcept
    {
        return v6(std::array<Core::u8, V6ByteCount>{});
    }

    // Parses a numeric literal. Accepts dotted-quad V4 and RFC 4291 V6 including
    // "::" compression and a trailing embedded V4 form. Rejects anything needing
    // a resolver, and rejects the bracketed "[::1]" form -- brackets belong to
    // URI authority syntax, not to an address.
    [[nodiscard]] static Core::Result<IpAddress> parse(std::string_view text);

    [[nodiscard]] constexpr IpFamily family() const noexcept { return m_family; }
    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_family != IpFamily::Unspecified;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] constexpr const std::array<Core::u8, V6ByteCount>& bytes() const noexcept
    {
        return m_bytes;
    }

    [[nodiscard]] constexpr Core::usize byteCount() const noexcept
    {
        return m_family == IpFamily::V4 ? V4ByteCount
            : m_family == IpFamily::V6  ? V6ByteCount
                                        : Core::usize{0};
    }

    [[nodiscard]] constexpr bool isLoopback() const noexcept
    {
        if (m_family == IpFamily::V4) {
            return m_bytes[0] == 127;
        }
        if (m_family != IpFamily::V6) {
            return false;
        }
        for (Core::usize index = 0; index < V6ByteCount - 1; ++index) {
            if (m_bytes[index] != 0) {
                return false;
            }
        }
        return m_bytes[V6ByteCount - 1] == 1;
    }

    [[nodiscard]] constexpr bool isUnspecifiedAddress() const noexcept
    {
        if (!hasValue()) {
            return false;
        }
        const Core::usize count = byteCount();
        for (Core::usize index = 0; index < count; ++index) {
            if (m_bytes[index] != 0) {
                return false;
            }
        }
        return true;
    }

    // Writes the canonical text form. Returns the written length, or 0 when the
    // buffer is too small or the address is unspecified. V6 output uses RFC 5952
    // lowercase with the longest run of zero groups compressed.
    [[nodiscard]] Core::usize format(std::span<char> out) const noexcept;

    [[nodiscard]] friend constexpr bool operator==(const IpAddress&, const IpAddress&) = default;

  private:
    IpFamily m_family = IpFamily::Unspecified;
    std::array<Core::u8, V6ByteCount> m_bytes{};
};

// Address plus port. Port is host byte order throughout the public surface.
struct NetworkEndpoint final {
    IpAddress address{};
    Core::u16 port = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept { return address.hasValue(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] friend constexpr bool operator==(
        const NetworkEndpoint&,
        const NetworkEndpoint&) = default;
};

} // namespace Tina::Network
