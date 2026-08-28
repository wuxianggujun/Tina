#include "ReadinessPoller.hpp"

#include <tina/network/NetworkErrors.hpp>

#include <limits>
#include <new>

namespace Tina::Network::Detail {
namespace {

inline constexpr Core::usize InvalidIndex = (std::numeric_limits<Core::usize>::max)();

// WSAPoll and poll agree on POLLRDNORM/POLLWRNORM, but plain POLLIN/POLLOUT are
// what POSIX code conventionally uses and WSAPoll accepts them too. Using the
// NORM spellings keeps one expression valid on both.
inline constexpr short PollReadFlags = POLLRDNORM;
inline constexpr short PollWriteFlags = POLLWRNORM;

} // namespace

ReadinessPoller::ReadinessPoller(
    Core::usize capacity,
    std::pmr::vector<Entry> entries,
    std::pmr::vector<NativePollFd> pollFds,
    std::pmr::vector<Core::usize> pollFdOwners,
    std::pmr::vector<ReadinessEvent> events) noexcept
    : m_capacity(capacity)
    , m_entries(std::move(entries))
    , m_pollFds(std::move(pollFds))
    , m_pollFdOwners(std::move(pollFdOwners))
    , m_events(std::move(events))
{
}

Core::Result<ReadinessPoller> ReadinessPoller::Create(
    Core::usize capacity,
    std::pmr::memory_resource& resource)
{
    if (capacity == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "ReadinessPoller capacity must be greater than zero");
    }
    // poll takes the descriptor count as an int on both platforms, so a capacity
    // beyond that range could not be submitted.
    if (capacity > static_cast<Core::usize>((std::numeric_limits<int>::max)())) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "ReadinessPoller capacity exceeds what the platform can poll");
    }

    try {
        std::pmr::vector<Entry> entries{&resource};
        std::pmr::vector<NativePollFd> pollFds{&resource};
        std::pmr::vector<Core::usize> pollFdOwners{&resource};
        std::pmr::vector<ReadinessEvent> events{&resource};

        entries.resize(capacity);
        // The three per-poll arrays are sized to capacity but used as a compacted
        // prefix, so their size never changes at runtime.
        pollFds.resize(capacity);
        pollFdOwners.resize(capacity);
        events.resize(capacity);

        return ReadinessPoller{
            capacity,
            std::move(entries),
            std::move(pollFds),
            std::move(pollFdOwners),
            std::move(events)};
    } catch (const std::bad_alloc&) {
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "ReadinessPoller fixed storage allocation failed");
    } catch (...) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "ReadinessPoller construction failed");
    }
}

Core::Result<Core::usize> ReadinessPoller::register_(
    NativeSocket socket,
    ReadinessInterest interest)
{
    if (!isInitialised()) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "ReadinessPoller is not initialised");
    }
    if (socket == InvalidNativeSocket) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "ReadinessPoller cannot register an invalid socket");
    }

    Core::usize slot = InvalidIndex;
    for (Core::usize index = 0; index < m_entries.size(); ++index) {
        if (!m_entries[index].occupied) {
            slot = index;
            break;
        }
    }
    if (slot == InvalidIndex) {
        return Core::failure(
            NetworkErrorCode::CapacityExceeded,
            "ReadinessPoller registration capacity is exhausted");
    }

    m_entries[slot] = Entry{
        .socket = socket,
        .interest = interest,
        .occupied = true};
    ++m_registeredCount;
    return slot;
}

void ReadinessPoller::release(Core::usize registrationIndex) noexcept
{
    if (registrationIndex >= m_entries.size()) {
        return;
    }
    if (!m_entries[registrationIndex].occupied) {
        return;
    }
    m_entries[registrationIndex] = Entry{};
    --m_registeredCount;
}

Core::Status ReadinessPoller::setInterest(
    Core::usize registrationIndex,
    ReadinessInterest interest) noexcept
{
    if (registrationIndex >= m_entries.size() || !m_entries[registrationIndex].occupied) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "ReadinessPoller registration index is not live");
    }
    m_entries[registrationIndex].interest = interest;
    return Core::success();
}

Core::Result<std::span<const ReadinessEvent>> ReadinessPoller::poll()
{
    if (!isInitialised()) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "ReadinessPoller is not initialised");
    }

    ++m_pollCallCount;

    // Compact the live, interested entries into the front of the pollfd array.
    // An entry with interest None is skipped: submitting it would make poll
    // report it as error-only noise every frame.
    Core::usize pollCount = 0;
    for (Core::usize index = 0; index < m_entries.size(); ++index) {
        const Entry& entry = m_entries[index];
        if (!entry.occupied || entry.interest == ReadinessInterest::None) {
            continue;
        }

        short events = 0;
        if (hasInterest(entry.interest, ReadinessInterest::Readable)) {
            events = static_cast<short>(events | PollReadFlags);
        }
        if (hasInterest(entry.interest, ReadinessInterest::Writable)) {
            events = static_cast<short>(events | PollWriteFlags);
        }

        m_pollFds[pollCount].fd = entry.socket;
        m_pollFds[pollCount].events = events;
        m_pollFds[pollCount].revents = 0;
        m_pollFdOwners[pollCount] = index;
        ++pollCount;
    }

    if (pollCount == 0) {
        // Nothing to ask about. Calling poll with zero descriptors and a zero
        // timeout is legal but pointless, and on Windows it is an error.
        return std::span<const ReadinessEvent>{m_events.data(), 0};
    }

#if defined(_WIN32)
    const int ready = ::WSAPoll(m_pollFds.data(), static_cast<ULONG>(pollCount), 0);
#else
    const int ready = ::poll(m_pollFds.data(), static_cast<nfds_t>(pollCount), 0);
#endif

    if (ready < 0) {
        const int error = lastSocketError();
        // A signal arriving mid-call means nothing happened yet; reporting an
        // error would make a harmless interruption look like a transport fault.
        if (isInterruptedError(error)) {
            return std::span<const ReadinessEvent>{m_events.data(), 0};
        }
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "ReadinessPoller poll failed");
    }
    if (ready == 0) {
        return std::span<const ReadinessEvent>{m_events.data(), 0};
    }

    Core::usize eventCount = 0;
    for (Core::usize slot = 0; slot < pollCount; ++slot) {
        const short revents = m_pollFds[slot].revents;
        if (revents == 0) {
            continue;
        }

        const bool readable = (revents & PollReadFlags) != 0;
        const bool writable = (revents & PollWriteFlags) != 0;
        // POLLHUP and POLLERR arrive unrequested. POLLNVAL means the descriptor
        // was closed behind our back, which is a caller bug but must not be
        // silently ignored -- surfacing it as errored lets the transport clean up.
        const bool errored = (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;

        if (!readable && !writable && !errored) {
            continue;
        }

        m_events[eventCount] = ReadinessEvent{
            .registrationIndex = m_pollFdOwners[slot],
            .readable = readable,
            .writable = writable,
            .errored = errored};
        ++eventCount;
    }

    return std::span<const ReadinessEvent>{m_events.data(), eventCount};
}

} // namespace Tina::Network::Detail
