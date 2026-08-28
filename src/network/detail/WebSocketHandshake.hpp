#pragma once

// Handshake primitives for RFC 6455. Private to src/network.
//
// SHA-1 lives here rather than in Core on purpose. RFC 6455 uses it to prove the
// server understood the handshake, not to protect anything -- the input is a
// public nonce concatenated with a published constant. SHA-1 is broken for real
// hashing, so exposing it as a Core utility would invite exactly the use it must
// not have. Core already provides ContentHash for hashing that matters.

#include <tina/core/base/Types.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>

namespace Tina::Network::Detail {

inline constexpr Core::usize Sha1DigestBytes = 20;

using Sha1Digest = std::array<Core::u8, Sha1DigestBytes>;

[[nodiscard]] Sha1Digest sha1(std::span<const std::byte> input) noexcept;

[[nodiscard]] std::string base64Encode(std::span<const std::byte> input);

// Returns false on a length that is not a multiple of four, a character outside
// the alphabet, or padding in the middle.
[[nodiscard]] bool base64Decode(std::string_view text, std::string& out);

// The fixed GUID from RFC 6455 section 1.3. Appended to the client key before
// hashing so a cached or confused peer cannot produce a valid accept token.
inline constexpr std::string_view WebSocketHandshakeGuid =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// base64(sha1(clientKey + GUID)), which is what a server must echo in
// Sec-WebSocket-Accept.
[[nodiscard]] std::string computeWebSocketAccept(std::string_view clientKey);

} // namespace Tina::Network::Detail
