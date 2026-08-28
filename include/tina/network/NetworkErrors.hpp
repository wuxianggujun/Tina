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

} // namespace NetworkErrorCode

} // namespace Tina::Network
