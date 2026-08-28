#include "WebSocketHandshake.hpp"

#include <cstring>

namespace Tina::Network::Detail {
namespace {

constexpr std::string_view Base64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] constexpr Core::u32 rotateLeft(Core::u32 value, int bits) noexcept
{
    return (value << bits) | (value >> (32 - bits));
}

[[nodiscard]] int base64Value(char symbol) noexcept
{
    if (symbol >= 'A' && symbol <= 'Z') {
        return symbol - 'A';
    }
    if (symbol >= 'a' && symbol <= 'z') {
        return (symbol - 'a') + 26;
    }
    if (symbol >= '0' && symbol <= '9') {
        return (symbol - '0') + 52;
    }
    if (symbol == '+') {
        return 62;
    }
    if (symbol == '/') {
        return 63;
    }
    return -1;
}

} // namespace

Sha1Digest sha1(std::span<const std::byte> input) noexcept
{
    Core::u32 state[5] = {
        0x67452301U,
        0xEFCDAB89U,
        0x98BADCFEU,
        0x10325476U,
        0xC3D2E1F0U};

    // Length in bits, which is what the padding block records.
    const Core::u64 messageBits = static_cast<Core::u64>(input.size()) * 8U;

    // Processes one 64-byte block.
    const auto processBlock = [&state](const Core::u8* block) noexcept {
        Core::u32 words[80] = {};
        for (int index = 0; index < 16; ++index) {
            words[index] = (static_cast<Core::u32>(block[index * 4]) << 24)
                | (static_cast<Core::u32>(block[(index * 4) + 1]) << 16)
                | (static_cast<Core::u32>(block[(index * 4) + 2]) << 8)
                | static_cast<Core::u32>(block[(index * 4) + 3]);
        }
        for (int index = 16; index < 80; ++index) {
            words[index] = rotateLeft(
                words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16],
                1);
        }

        Core::u32 a = state[0];
        Core::u32 b = state[1];
        Core::u32 c = state[2];
        Core::u32 d = state[3];
        Core::u32 e = state[4];

        for (int index = 0; index < 80; ++index) {
            Core::u32 f = 0;
            Core::u32 k = 0;
            if (index < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (index < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (index < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }

            const Core::u32 temp = rotateLeft(a, 5) + f + e + k + words[index];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    };

    const auto* bytes = reinterpret_cast<const Core::u8*>(input.data());
    Core::usize offset = 0;
    while (offset + 64 <= input.size()) {
        processBlock(bytes + offset);
        offset += 64;
    }

    // Padding: a 0x80 byte, zeros, then the length. Needs two blocks when the
    // remainder leaves no room for the 8-byte length.
    std::array<Core::u8, 128> tail{};
    const Core::usize remaining = input.size() - offset;
    if (remaining != 0) {
        std::memcpy(tail.data(), bytes + offset, remaining);
    }
    tail[remaining] = 0x80U;

    const Core::usize tailBlocks = (remaining + 1 + 8 > 64) ? 2 : 1;
    const Core::usize tailBytes = tailBlocks * 64;
    for (int index = 0; index < 8; ++index) {
        tail[tailBytes - 1 - static_cast<Core::usize>(index)] =
            static_cast<Core::u8>((messageBits >> (index * 8)) & 0xFFU);
    }

    processBlock(tail.data());
    if (tailBlocks == 2) {
        processBlock(tail.data() + 64);
    }

    Sha1Digest digest{};
    for (int index = 0; index < 5; ++index) {
        digest[index * 4] = static_cast<Core::u8>((state[index] >> 24) & 0xFFU);
        digest[(index * 4) + 1] = static_cast<Core::u8>((state[index] >> 16) & 0xFFU);
        digest[(index * 4) + 2] = static_cast<Core::u8>((state[index] >> 8) & 0xFFU);
        digest[(index * 4) + 3] = static_cast<Core::u8>(state[index] & 0xFFU);
    }
    return digest;
}

std::string base64Encode(std::span<const std::byte> input)
{
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    const auto* bytes = reinterpret_cast<const Core::u8*>(input.data());
    Core::usize offset = 0;
    while (offset + 3 <= input.size()) {
        const Core::u32 triple = (static_cast<Core::u32>(bytes[offset]) << 16)
            | (static_cast<Core::u32>(bytes[offset + 1]) << 8)
            | static_cast<Core::u32>(bytes[offset + 2]);
        out.push_back(Base64Alphabet[(triple >> 18) & 0x3FU]);
        out.push_back(Base64Alphabet[(triple >> 12) & 0x3FU]);
        out.push_back(Base64Alphabet[(triple >> 6) & 0x3FU]);
        out.push_back(Base64Alphabet[triple & 0x3FU]);
        offset += 3;
    }

    const Core::usize remaining = input.size() - offset;
    if (remaining == 1) {
        const Core::u32 value = static_cast<Core::u32>(bytes[offset]) << 16;
        out.push_back(Base64Alphabet[(value >> 18) & 0x3FU]);
        out.push_back(Base64Alphabet[(value >> 12) & 0x3FU]);
        out.append("==");
    } else if (remaining == 2) {
        const Core::u32 value = (static_cast<Core::u32>(bytes[offset]) << 16)
            | (static_cast<Core::u32>(bytes[offset + 1]) << 8);
        out.push_back(Base64Alphabet[(value >> 18) & 0x3FU]);
        out.push_back(Base64Alphabet[(value >> 12) & 0x3FU]);
        out.push_back(Base64Alphabet[(value >> 6) & 0x3FU]);
        out.push_back('=');
    }
    return out;
}

bool base64Decode(std::string_view text, std::string& out)
{
    if (text.size() % 4 != 0) {
        return false;
    }
    out.clear();
    out.reserve((text.size() / 4) * 3);

    for (Core::usize offset = 0; offset < text.size(); offset += 4) {
        const bool lastGroup = (offset + 4 == text.size());

        int values[4] = {0, 0, 0, 0};
        int padding = 0;
        for (int index = 0; index < 4; ++index) {
            const char symbol = text[offset + static_cast<Core::usize>(index)];
            if (symbol == '=') {
                // Padding is only legal in the final group, and only in the last
                // two positions. Anywhere else means the text is not canonical.
                if (!lastGroup || index < 2) {
                    return false;
                }
                ++padding;
                continue;
            }
            if (padding != 0) {
                // A real symbol after padding.
                return false;
            }
            const int value = base64Value(symbol);
            if (value < 0) {
                return false;
            }
            values[index] = value;
        }

        const Core::u32 triple = (static_cast<Core::u32>(values[0]) << 18)
            | (static_cast<Core::u32>(values[1]) << 12)
            | (static_cast<Core::u32>(values[2]) << 6)
            | static_cast<Core::u32>(values[3]);

        out.push_back(static_cast<char>((triple >> 16) & 0xFFU));
        if (padding < 2) {
            out.push_back(static_cast<char>((triple >> 8) & 0xFFU));
        }
        if (padding < 1) {
            out.push_back(static_cast<char>(triple & 0xFFU));
        }
    }
    return true;
}

std::string computeWebSocketAccept(std::string_view clientKey)
{
    std::string combined;
    combined.reserve(clientKey.size() + WebSocketHandshakeGuid.size());
    combined.append(clientKey);
    combined.append(WebSocketHandshakeGuid);

    const auto digest = sha1(std::as_bytes(std::span{combined.data(), combined.size()}));
    return base64Encode(std::as_bytes(std::span{digest.data(), digest.size()}));
}

} // namespace Tina::Network::Detail
