#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Network {

namespace NetworkErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Network, 1};
inline constexpr Core::ErrorCode SocketClosed{Core::ErrorDomain::Network, 2};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Network, 3};
inline constexpr Core::ErrorCode InvalidEndpoint{Core::ErrorDomain::Network, 4};
inline constexpr Core::ErrorCode InvalidDatagram{Core::ErrorDomain::Network, 5};
inline constexpr Core::ErrorCode DatagramTooLarge{Core::ErrorDomain::Network, 6};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Network, 7};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Network, 8};
inline constexpr Core::ErrorCode BackendFailure{Core::ErrorDomain::Network, 9};
inline constexpr Core::ErrorCode AddressUnavailable{Core::ErrorDomain::Network, 10};
inline constexpr Core::ErrorCode WouldBlock{Core::ErrorDomain::Network, 11};
inline constexpr Core::ErrorCode ConstructionFailed{Core::ErrorDomain::Network, 12};
inline constexpr Core::ErrorCode NotConnected{Core::ErrorDomain::Network, 13};
inline constexpr Core::ErrorCode ConnectionFailed{Core::ErrorDomain::Network, 14};
inline constexpr Core::ErrorCode ConnectionClosed{Core::ErrorDomain::Network, 15};
inline constexpr Core::ErrorCode PeerReset{Core::ErrorDomain::Network, 16};
inline constexpr Core::ErrorCode TlsHandshakeFailed{Core::ErrorDomain::Network, 17};
// The chain, or the hostname, did not verify. Kept distinct from a handshake
// failure so a caller can tell a trust problem from a protocol problem.
inline constexpr Core::ErrorCode TlsVerificationFailed{Core::ErrorDomain::Network, 18};
inline constexpr Core::ErrorCode TlsProtocolFailure{Core::ErrorDomain::Network, 19};
// A configuration asked to skip verification without the second explicit opt-in,
// or asked for it in a build where it is unavailable.
inline constexpr Core::ErrorCode TlsInsecureConfigurationRejected{
    Core::ErrorDomain::Network,
    20};
// The response was not valid HTTP/1.1. Distinct from a transport failure: the
// bytes arrived, they just did not parse.
inline constexpr Core::ErrorCode HttpMalformedResponse{Core::ErrorDomain::Network, 21};
// Headers or body exceeded the configured cap. Never silently truncated.
inline constexpr Core::ErrorCode HttpResponseTooLarge{Core::ErrorDomain::Network, 22};
inline constexpr Core::ErrorCode HttpTimeout{Core::ErrorDomain::Network, 23};
// The peer closed before the declared body length arrived.
inline constexpr Core::ErrorCode HttpIncompleteResponse{Core::ErrorDomain::Network, 24};
// The server did not accept the upgrade, or its accept token did not match.
inline constexpr Core::ErrorCode WebSocketHandshakeFailed{Core::ErrorDomain::Network, 25};
// A frame violated RFC 6455: a reserved bit, an unknown opcode, an unmasked
// server frame that should be masked, or a control frame that was fragmented.
inline constexpr Core::ErrorCode WebSocketProtocolError{Core::ErrorDomain::Network, 26};
inline constexpr Core::ErrorCode WebSocketMessageTooLarge{Core::ErrorDomain::Network, 27};
inline constexpr Core::ErrorCode WebSocketClosed{Core::ErrorDomain::Network, 28};
// The name could not be resolved. Distinct from a transport failure: nothing was
// connected yet.
inline constexpr Core::ErrorCode DnsResolutionFailed{Core::ErrorDomain::Network, 29};
// The query never ran because the resolver had no free slot or no worker.
inline constexpr Core::ErrorCode DnsQueryRejected{Core::ErrorDomain::Network, 30};
inline constexpr Core::ErrorCode DnsQueryPending{Core::ErrorDomain::Network, 31};
inline constexpr Core::ErrorCode InvalidQuery{Core::ErrorDomain::Network, 32};

} // namespace NetworkErrorCode

} // namespace Tina::Network
