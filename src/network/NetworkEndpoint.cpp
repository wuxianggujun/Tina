#include <tina/network/NetworkEndpoint.hpp>

#include <tina/network/NetworkErrors.hpp>

namespace Tina::Network {
namespace {

[[nodiscard]] constexpr bool isDecimalDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr int hexDigitValue(char value) noexcept
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return (value - 'a') + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return (value - 'A') + 10;
    }
    return -1;
}

// Parses one dotted-quad octet. Rejects leading zeros because "010" is octal in
// some resolvers and decimal in others -- a real source of address-spoofing
// ambiguity, so it is refused rather than guessed.
[[nodiscard]] bool parseOctet(std::string_view text, Core::u8& out) noexcept
{
    if (text.empty() || text.size() > 3) {
        return false;
    }
    if (text.size() > 1 && text.front() == '0') {
        return false;
    }
    Core::u32 value = 0;
    for (const char digit : text) {
        if (!isDecimalDigit(digit)) {
            return false;
        }
        value = (value * 10U) + static_cast<Core::u32>(digit - '0');
    }
    if (value > 255U) {
        return false;
    }
    out = static_cast<Core::u8>(value);
    return true;
}

[[nodiscard]] bool parseV4Into(std::string_view text, Core::u8* out) noexcept
{
    Core::usize octetIndex = 0;
    Core::usize cursor = 0;
    while (octetIndex < IpAddress::V4ByteCount) {
        const Core::usize dot = text.find('.', cursor);
        const bool isLast = octetIndex == IpAddress::V4ByteCount - 1;
        if (isLast != (dot == std::string_view::npos)) {
            return false;
        }
        const Core::usize end = isLast ? text.size() : dot;
        if (!parseOctet(text.substr(cursor, end - cursor), out[octetIndex])) {
            return false;
        }
        cursor = end + 1;
        ++octetIndex;
    }
    return true;
}

constexpr Core::usize V6GroupCount = 8;

[[nodiscard]] bool parseV6Into(std::string_view text, std::array<Core::u8, 16>& out) noexcept
{
    if (text.empty()) {
        return false;
    }

    std::array<Core::u16, V6GroupCount> groups{};
    Core::usize groupCount = 0;
    // Index in groups where "::" elided zero groups, or npos when absent.
    Core::usize compressionIndex = std::string_view::npos;
    Core::usize cursor = 0;

    if (text.starts_with("::")) {
        compressionIndex = 0;
        cursor = 2;
        if (cursor == text.size()) {
            out = {};
            return true;
        }
    } else if (text.starts_with(':')) {
        // A single leading colon is never valid.
        return false;
    }

    while (cursor < text.size()) {
        if (text[cursor] == ':') {
            if (compressionIndex != std::string_view::npos) {
                // Only one "::" may appear.
                return false;
            }
            compressionIndex = groupCount;
            ++cursor;
            if (cursor == text.size()) {
                break;
            }
            if (text[cursor] == ':') {
                return false;
            }
            continue;
        }

        const Core::usize segmentEnd = text.find(':', cursor);
        const Core::usize segmentLength = (segmentEnd == std::string_view::npos)
            ? text.size() - cursor
            : segmentEnd - cursor;
        const std::string_view segment = text.substr(cursor, segmentLength);

        // A trailing embedded V4 form consumes the final two groups.
        if (segment.find('.') != std::string_view::npos) {
            if (segmentEnd != std::string_view::npos) {
                return false;
            }
            if (groupCount + 2 > V6GroupCount) {
                return false;
            }
            std::array<Core::u8, IpAddress::V4ByteCount> v4Bytes{};
            if (!parseV4Into(segment, v4Bytes.data())) {
                return false;
            }
            groups[groupCount] = static_cast<Core::u16>(
                (static_cast<Core::u16>(v4Bytes[0]) << 8) | v4Bytes[1]);
            groups[groupCount + 1] = static_cast<Core::u16>(
                (static_cast<Core::u16>(v4Bytes[2]) << 8) | v4Bytes[3]);
            groupCount += 2;
            cursor = text.size();
            break;
        }

        if (segment.empty() || segment.size() > 4) {
            return false;
        }
        if (groupCount >= V6GroupCount) {
            return false;
        }
        Core::u32 value = 0;
        for (const char digit : segment) {
            const int nibble = hexDigitValue(digit);
            if (nibble < 0) {
                return false;
            }
            value = (value << 4) | static_cast<Core::u32>(nibble);
        }
        groups[groupCount] = static_cast<Core::u16>(value);
        ++groupCount;

        if (segmentEnd == std::string_view::npos) {
            cursor = text.size();
            break;
        }
        cursor = segmentEnd + 1;
        if (cursor == text.size()) {
            // A trailing single colon is only legal as part of "::".
            return false;
        }
    }

    if (compressionIndex == std::string_view::npos) {
        if (groupCount != V6GroupCount) {
            return false;
        }
    } else {
        // "::" must stand for at least one elided group.
        if (groupCount >= V6GroupCount) {
            return false;
        }
    }

    std::array<Core::u16, V6GroupCount> expanded{};
    if (compressionIndex == std::string_view::npos) {
        expanded = groups;
    } else {
        const Core::usize tailCount = groupCount - compressionIndex;
        for (Core::usize index = 0; index < compressionIndex; ++index) {
            expanded[index] = groups[index];
        }
        for (Core::usize index = 0; index < tailCount; ++index) {
            expanded[V6GroupCount - tailCount + index] = groups[compressionIndex + index];
        }
    }

    for (Core::usize index = 0; index < V6GroupCount; ++index) {
        out[index * 2] = static_cast<Core::u8>((expanded[index] >> 8) & 0xFFU);
        out[(index * 2) + 1] = static_cast<Core::u8>(expanded[index] & 0xFFU);
    }
    return true;
}

[[nodiscard]] Core::usize writeDecimal(std::span<char> out, Core::usize cursor, Core::u8 value) noexcept
{
    if (value >= 100) {
        if (cursor + 3 > out.size()) {
            return 0;
        }
        out[cursor] = static_cast<char>('0' + (value / 100));
        out[cursor + 1] = static_cast<char>('0' + ((value / 10) % 10));
        out[cursor + 2] = static_cast<char>('0' + (value % 10));
        return 3;
    }
    if (value >= 10) {
        if (cursor + 2 > out.size()) {
            return 0;
        }
        out[cursor] = static_cast<char>('0' + (value / 10));
        out[cursor + 1] = static_cast<char>('0' + (value % 10));
        return 2;
    }
    if (cursor + 1 > out.size()) {
        return 0;
    }
    out[cursor] = static_cast<char>('0' + value);
    return 1;
}

} // namespace

Core::Result<IpAddress> IpAddress::parse(std::string_view text)
{
    if (text.empty() || text.size() > MaximumTextLength - 1) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "IP address text is empty or too long");
    }

    // Only V6 uses colons, and only V4 is pure dotted-quad. Deciding on ':'
    // avoids the guesswork that leads to a bracketed or hostname form slipping
    // through.
    if (text.find(':') != std::string_view::npos) {
        std::array<Core::u8, V6ByteCount> bytes{};
        if (!parseV6Into(text, bytes)) {
            return Core::failure(
                NetworkErrorCode::InvalidEndpoint,
                "IP address is not a valid IPv6 literal");
        }
        return IpAddress::v6(bytes);
    }

    std::array<Core::u8, V4ByteCount> bytes{};
    if (!parseV4Into(text, bytes.data())) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "IP address is not a valid IPv4 literal");
    }
    return IpAddress::v4(bytes[0], bytes[1], bytes[2], bytes[3]);
}

Core::usize IpAddress::format(std::span<char> out) const noexcept
{
    if (m_family == IpFamily::V4) {
        Core::usize cursor = 0;
        for (Core::usize index = 0; index < V4ByteCount; ++index) {
            if (index != 0) {
                if (cursor + 1 > out.size()) {
                    return 0;
                }
                out[cursor] = '.';
                ++cursor;
            }
            const Core::usize written = writeDecimal(out, cursor, m_bytes[index]);
            if (written == 0) {
                return 0;
            }
            cursor += written;
        }
        return cursor;
    }

    if (m_family != IpFamily::V6) {
        return 0;
    }

    std::array<Core::u16, V6GroupCount> groups{};
    for (Core::usize index = 0; index < V6GroupCount; ++index) {
        groups[index] = static_cast<Core::u16>(
            (static_cast<Core::u16>(m_bytes[index * 2]) << 8) | m_bytes[(index * 2) + 1]);
    }

    // RFC 5952: compress the longest run of zero groups, and only when it covers
    // more than one group. Ties resolve to the leftmost run.
    Core::usize bestStart = V6GroupCount;
    Core::usize bestLength = 0;
    Core::usize runStart = 0;
    Core::usize runLength = 0;
    for (Core::usize index = 0; index < V6GroupCount; ++index) {
        if (groups[index] == 0) {
            if (runLength == 0) {
                runStart = index;
            }
            ++runLength;
            if (runLength > bestLength) {
                bestLength = runLength;
                bestStart = runStart;
            }
        } else {
            runLength = 0;
        }
    }
    if (bestLength < 2) {
        bestStart = V6GroupCount;
        bestLength = 0;
    }

    Core::usize cursor = 0;
    for (Core::usize index = 0; index < V6GroupCount;) {
        if (index == bestStart) {
            if (cursor + 2 > out.size()) {
                return 0;
            }
            out[cursor] = ':';
            out[cursor + 1] = ':';
            cursor += 2;
            index += bestLength;
            continue;
        }

        // A separator is needed unless we are at the start, or immediately after
        // the "::" that already supplied one.
        if (index != 0 && !(bestStart != V6GroupCount && index == bestStart + bestLength)) {
            if (cursor + 1 > out.size()) {
                return 0;
            }
            out[cursor] = ':';
            ++cursor;
        }

        const Core::u16 group = groups[index];
        bool wroteDigit = false;
        for (int shift = 12; shift >= 0; shift -= 4) {
            const auto nibble = static_cast<Core::u8>((group >> shift) & 0xFU);
            if (nibble == 0 && !wroteDigit && shift != 0) {
                continue;
            }
            if (cursor + 1 > out.size()) {
                return 0;
            }
            out[cursor] = nibble < 10
                ? static_cast<char>('0' + nibble)
                : static_cast<char>('a' + (nibble - 10));
            ++cursor;
            wroteDigit = true;
        }
        ++index;
    }
    return cursor;
}

} // namespace Tina::Network
