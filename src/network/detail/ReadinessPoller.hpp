#pragma once

// Fixed-capacity readiness multiplexer. Private to src/network.
//
// One non-blocking readiness query covers every registered socket, so a transport
// advances all of its connections from the owner thread with a single syscall per
// pump. This is what removes the need for worker threads: TCP and TLS are slow
// because they wait, and waiting does not require a thread.
//
// Backed by WSAPoll on Windows and poll on POSIX. Both take the same pollfd
// shape, so the array is built once at Create and reused every pump.

#include "NativeSocket.hpp"

#include <tina/core/base/EnumFlags.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Network::Detail {

// What a registrant wants to hear about. A socket always hears about errors and
// hangup, so those are not requestable interests.
enum class ReadinessInterest : Core::u8 {
    None = 0,
    Readable = 1 << 0,
    Writable = 1 << 1,
    ReadableAndWritable = Readable | Writable,
};

TINA_ENUM_FLAG_OPERATORS(ReadinessInterest);

[[nodiscard]] constexpr bool hasInterest(
    ReadinessInterest value,
    ReadinessInterest probe) noexcept
{
    return hasAnyFlag(value, probe);
}

// What actually happened. readable/writable are advisory: a socket reported
// readable can still yield EWOULDBLOCK, so callers must handle that rather than
// trust the flag.
struct ReadinessEvent final {
    Core::usize registrationIndex = 0;
    bool readable = false;
    bool writable = false;
    // The socket failed or the peer hung up. Distinguishing the two portably is
    // not possible from poll flags alone -- the transport learns which by trying
    // the next operation.
    bool errored = false;
};

class ReadinessPoller final {
  public:
    [[nodiscard]] static Core::Result<ReadinessPoller> Create(
        Core::usize capacity,
        std::pmr::memory_resource& resource);

    ~ReadinessPoller() noexcept = default;

    ReadinessPoller(const ReadinessPoller&) = delete;
    ReadinessPoller& operator=(const ReadinessPoller&) = delete;
    ReadinessPoller(ReadinessPoller&&) noexcept = default;
    ReadinessPoller& operator=(ReadinessPoller&&) = delete;

    [[nodiscard]] bool isInitialised() const noexcept
    {
        return m_capacity != 0 && m_entries.size() == m_capacity;
    }

    // Claims a slot. The returned index is stable until release() and is what
    // ReadinessEvent reports back, so a transport can map an event to its own
    // connection record without a search.
    [[nodiscard]] Core::Result<Core::usize> register_(
        NativeSocket socket,
        ReadinessInterest interest);

    void release(Core::usize registrationIndex) noexcept;

    // Interest changes between pumps. A connection that has drained its send
    // buffer drops Writable so poll stops reporting it every frame.
    [[nodiscard]] Core::Status setInterest(
        Core::usize registrationIndex,
        ReadinessInterest interest) noexcept;

    // Performs one non-blocking readiness query and returns the events. Never
    // blocks. The returned span borrows internal storage and is invalidated by
    // the next poll() or by register_/release.
    //
    // An empty result is the normal idle answer, not an error.
    [[nodiscard]] Core::Result<std::span<const ReadinessEvent>> poll();

    [[nodiscard]] Core::usize capacity() const noexcept { return m_capacity; }
    [[nodiscard]] Core::usize registeredCount() const noexcept { return m_registeredCount; }
    [[nodiscard]] Core::u64 pollCallCount() const noexcept { return m_pollCallCount; }

  private:
    struct Entry final {
        NativeSocket socket = InvalidNativeSocket;
        ReadinessInterest interest = ReadinessInterest::None;
        bool occupied = false;
    };

#if defined(_WIN32)
    using NativePollFd = WSAPOLLFD;
#else
    using NativePollFd = pollfd;
#endif

    ReadinessPoller(
        Core::usize capacity,
        std::pmr::vector<Entry> entries,
        std::pmr::vector<NativePollFd> pollFds,
        std::pmr::vector<Core::usize> pollFdOwners,
        std::pmr::vector<ReadinessEvent> events) noexcept;

    Core::usize m_capacity = 0;
    Core::usize m_registeredCount = 0;
    Core::u64 m_pollCallCount = 0;

    std::pmr::vector<Entry> m_entries;
    // Compacted per poll: only occupied entries with a non-None interest take a
    // slot, so an idle transport does not pay for empty registrations.
    std::pmr::vector<NativePollFd> m_pollFds;
    // Maps a compacted pollFd slot back to its entry index.
    std::pmr::vector<Core::usize> m_pollFdOwners;
    std::pmr::vector<ReadinessEvent> m_events;
};

} // namespace Tina::Network::Detail
