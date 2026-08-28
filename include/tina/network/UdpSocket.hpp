#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/NetworkEndpoint.hpp>

#include <memory_resource>
#include <span>

namespace Tina::Network {

// Largest payload this slice will carry in one datagram. Well under the 65507
// theoretical IPv4 limit: anything above the path MTU fragments at the IP layer,
// where a single lost fragment discards the whole datagram.
inline constexpr Core::usize MaximumDatagramBytes = 1200;

struct UdpSocketConfig final {
    // Local address to bind. An unspecified address binds all interfaces; port 0
    // asks the OS for an ephemeral port, readable afterwards via localEndpoint().
    NetworkEndpoint localEndpoint{};

    // Receive slots preallocated at Create. Each slot holds one datagram up to
    // maximumDatagramBytes. receive() drains at most this many per call.
    Core::usize receiveQueueCapacity = 64;

    Core::usize maximumDatagramBytes = MaximumDatagramBytes;

    // Borrowed when non-null and must outlive the socket.
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct UdpSocketStatistics final {
    Core::usize lastReceivedDatagramCount = 0;
    Core::usize lastDiscardedDatagramCount = 0;

    Core::u64 receiveCallCount = 0;
    Core::u64 totalReceivedDatagramCount = 0;
    Core::u64 totalReceivedBytes = 0;
    Core::u64 totalSentDatagramCount = 0;
    Core::u64 totalSentBytes = 0;

    // Datagrams the transport handed over that could not be surfaced. Disjoint
    // from totalReceivedDatagramCount. A full receive queue does not discard --
    // it leaves datagrams buffered for the next call -- so it is not counted here.
    Core::u64 totalDiscardedDatagramCount = 0;
    // Subset of the above: the payload did not fit maximumDatagramBytes, so it
    // could not be distinguished from a truncated one.
    Core::u64 totalOversizedDatagramCount = 0;
};

// One received datagram. The payload is borrowed from socket storage and stays
// valid only until the next receive() or destruction.
struct ReceivedDatagram final {
    NetworkEndpoint sender{};
    std::span<const std::byte> payload{};
};

// Non-blocking UDP socket. Every method must be called from the thread that
// called Create.
//
// There is no worker thread and no completion callback: receive() drains what
// the OS has already buffered, so the caller decides when datagrams become
// visible. That keeps the fixed-capacity queue single-threaded and means a
// datagram never appears mid-frame.
class UdpSocket final {
  public:
    [[nodiscard]] static Core::Result<UdpSocket> Create(UdpSocketConfig config = {});

    ~UdpSocket() noexcept;

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    // Bound local address, with the resolved port when config requested 0.
    [[nodiscard]] Core::Result<NetworkEndpoint> localEndpoint() const noexcept;

    // Sends one datagram. Rejects an empty payload, a payload above
    // maximumDatagramBytes, an endpoint whose family differs from the bound
    // socket, and port 0. A full kernel send buffer returns WouldBlock, which is
    // transient: retry on a later frame.
    //
    // Success means handed to the OS -- not that the datagram arrived.
    [[nodiscard]] Core::Status send(
        const NetworkEndpoint& destination,
        std::span<const std::byte> payload);

    // Drains buffered datagrams into internal storage and reports them in arrival
    // order. Never blocks.
    //
    // Stops at receiveQueueCapacity; the remainder stays buffered in the transport
    // for the next call, so a full queue defers rather than discards. Oversized
    // datagrams are discarded and counted instead of truncated, since a truncated
    // datagram is indistinguishable from a complete one.
    //
    // One call is also bounded in syscalls, so a flood of unusable datagrams
    // cannot stretch it indefinitely.
    //
    // The returned span and its payloads are invalidated by the next receive().
    [[nodiscard]] Core::Result<std::span<const ReceivedDatagram>> receive();

    [[nodiscard]] UdpSocketStatistics statistics() const noexcept;

    [[nodiscard]] Core::usize receiveQueueCapacity() const noexcept;
    [[nodiscard]] Core::usize maximumDatagramBytes() const noexcept;

  private:
    struct Impl;

    explicit UdpSocket(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Network
