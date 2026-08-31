#include <tina/network/DnsResolver.hpp>

#include "detail/NativeSocket.hpp"

#include <tina/network/NetworkErrors.hpp>
#include <tina/task/TaskErrors.hpp>

#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace Tina::Network {
namespace {

// A name longer than this cannot be a valid DNS name (RFC 1035 caps it at 253),
// so it is refused before a worker is spent on it.
constexpr Core::usize MaximumHostNameLength = 253;

} // namespace

// Shared with the worker through a shared_ptr, so a cancelled query's storage
// outlives the handle. The worker writes only into its own state and publishes
// with a single release store, which is the pattern AssetSystem already uses --
// no lock, and the owner never touches a field mid-write.
struct DnsRequestState final {
    enum class Outcome : Core::u8 {
        Running = 0,
        Succeeded = 1,
        Failed = 2,
    };

    std::string hostName;
    std::string portText;
    int addressFamily = AF_UNSPEC;
    Core::usize maximumAddresses = 0;

    // Worker-owned until outcome is published.
    std::vector<NetworkEndpoint> addresses;
    bool truncated = false;
    bool ownsTransportScope = false;

    std::atomic<Outcome> outcome{Outcome::Running};

    ~DnsRequestState() noexcept
    {
        if (ownsTransportScope) {
            Detail::TransportScope::release();
        }
    }
};

struct DnsResolver::Impl final {
    Impl(std::pmr::memory_resource& resource, Task::ITaskSystem& taskSystemIn)
        : taskSystem(&taskSystemIn)
        , slots(&resource)
    {
    }

    struct Slot final {
        std::shared_ptr<DnsRequestState> request;
        // Non-zero while the slot is live. Bumped on every reuse so a stale handle
        // cannot read a later query's answer.
        Core::u32 generation = 0;
        DnsQueryState state = DnsQueryState::Pending;
        // Set when the caller cancelled but the worker has not returned. The slot
        // cannot be reused until it does, because the worker still writes to the
        // shared state.
        bool abandoned = false;
        std::pmr::vector<NetworkEndpoint> addresses;

        explicit Slot(std::pmr::memory_resource* resource)
            : addresses(resource)
        {
        }
    };

    Task::ITaskSystem* taskSystem = nullptr;
    std::thread::id owner{};

    Core::usize maximumAddressesPerQuery = 0;
    Core::u32 nextGeneration = 1;

    std::pmr::vector<Slot> slots;

    DnsResolverStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    [[nodiscard]] Slot* findSlot(DnsQueryHandle handle) noexcept
    {
        if (!handle.hasValue() || handle.slot >= slots.size()) {
            return nullptr;
        }
        Slot& slot = slots[handle.slot];
        if (slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] const Slot* findSlot(DnsQueryHandle handle) const noexcept
    {
        if (!handle.hasValue() || handle.slot >= slots.size()) {
            return nullptr;
        }
        const Slot& slot = slots[handle.slot];
        if (slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    // A slot is reusable once it has no generation, or it was abandoned and its
    // worker has finished writing.
    [[nodiscard]] bool isSlotFree(const Slot& slot) const noexcept
    {
        if (slot.generation == 0) {
            return true;
        }
        if (!slot.abandoned) {
            return false;
        }
        return slot.request == nullptr
            || slot.request->outcome.load(std::memory_order_acquire)
            != DnsRequestState::Outcome::Running;
    }

    void clearSlot(Slot& slot) noexcept
    {
        slot.request.reset();
        slot.generation = 0;
        slot.state = DnsQueryState::Pending;
        slot.abandoned = false;
        slot.addresses.clear();
    }
};

namespace {

// Runs on an IO worker. Blocking, which is the whole reason it is here.
void performResolution(const std::shared_ptr<DnsRequestState>& request) noexcept
{
    bool ok = false;
    try {
        addrinfo hints{};
        hints.ai_family = request->addressFamily;
        // A stream socket type keeps getaddrinfo from returning one entry per
        // socket type for the same address.
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* rawResults = nullptr;
        const int status = ::getaddrinfo(
            request->hostName.c_str(),
            request->portText.c_str(),
            &hints,
            &rawResults);
        std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> results{rawResults, &::freeaddrinfo};

        if (status == 0 && results != nullptr) {
            for (const addrinfo* entry = results.get(); entry != nullptr; entry = entry->ai_next) {
                if (request->addresses.size() >= request->maximumAddresses) {
                    // More addresses than the caller reserved room for. Keeping the
                    // first N in system order is better than failing: a client only
                    // needs one reachable address.
                    request->truncated = true;
                    break;
                }
                if (entry->ai_addr == nullptr) {
                    continue;
                }

                sockaddr_storage storage{};
                const auto copyBytes = static_cast<Core::usize>(entry->ai_addrlen);
                if (copyBytes == 0 || copyBytes > sizeof(storage)) {
                    continue;
                }
                std::memcpy(&storage, entry->ai_addr, copyBytes);

                NetworkEndpoint endpoint{};
                if (Detail::fromNativeAddress(storage, endpoint)) {
                    request->addresses.push_back(endpoint);
                }
            }
            ok = !request->addresses.empty();
        }

    } catch (...) {
        // The worker must not let anything escape: pumpMain does not catch, and a
        // throw here would take down the whole task system.
        ok = false;
    }

    request->outcome.store(
        ok ? DnsRequestState::Outcome::Succeeded : DnsRequestState::Outcome::Failed,
        std::memory_order_release);
}

} // namespace

DnsResolver::DnsResolver(Impl* impl) noexcept
    : m_impl(impl)
{
}

DnsResolver::DnsResolver(DnsResolver&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

DnsResolver::~DnsResolver() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    // Workers may still hold shared state; the shared_ptr keeps both that state and its per-request
    // transport scope alive until getaddrinfo has returned.
    delete m_impl;
    m_impl = nullptr;
}

Core::Result<DnsResolver> DnsResolver::Create(DnsResolverConfig config)
{
    if (config.taskSystem == nullptr) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "DnsResolver requires a task system: getaddrinfo blocks");
    }
    if (config.queryCapacity == 0 || config.maximumAddressesPerQuery == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "DnsResolver capacities must be greater than zero");
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource, *config.taskSystem};
        impl->slots.reserve(config.queryCapacity);
        for (Core::usize index = 0; index < config.queryCapacity; ++index) {
            impl->slots.emplace_back(resource);
            impl->slots.back().addresses.reserve(config.maximumAddressesPerQuery);
        }
    } catch (const std::bad_alloc&) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "DnsResolver fixed storage allocation failed");
    } catch (...) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "DnsResolver construction failed");
    }

    impl->owner = std::this_thread::get_id();
    impl->maximumAddressesPerQuery = config.maximumAddressesPerQuery;

    return DnsResolver{impl};
}

Core::Result<DnsQueryHandle> DnsResolver::resolve(
    std::string_view hostName,
    Core::u16 port,
    DnsAddressFamily family)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "DnsResolver is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "DnsResolver must be used from its owner thread");
    }
    if (hostName.empty() || hostName.size() > MaximumHostNameLength) {
        return Core::failure(
            NetworkErrorCode::InvalidQuery,
            "DNS host name is empty or longer than a DNS name can be");
    }
    // A NUL would truncate the name inside getaddrinfo, resolving something other
    // than what was asked for.
    if (hostName.find('\0') != std::string_view::npos) {
        return Core::failure(
            NetworkErrorCode::InvalidQuery,
            "DNS host name contains an embedded NUL");
    }
    if (port == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidQuery,
            "DNS query requires a non-zero port");
    }

    Impl::Slot* target = nullptr;
    Core::u32 slotIndex = 0;
    for (Core::usize index = 0; index < m_impl->slots.size(); ++index) {
        if (m_impl->isSlotFree(m_impl->slots[index])) {
            target = &m_impl->slots[index];
            slotIndex = static_cast<Core::u32>(index);
            break;
        }
    }
    if (target == nullptr) {
        ++m_impl->stats.rejectedQueryCount;
        return Core::failure(
            NetworkErrorCode::DnsQueryRejected,
            "DnsResolver has no free query slot; retry on a later frame");
    }

    std::shared_ptr<DnsRequestState> request;
    Task::TaskCallable work;
    try {
        request = std::make_shared<DnsRequestState>();
        if (const Core::Status status = Detail::TransportScope::acquire(); !status) {
            return Core::failure(status.error());
        }
        request->ownsTransportScope = true;
        request->hostName.assign(hostName);
        request->portText = std::to_string(port);
        request->maximumAddresses = m_impl->maximumAddressesPerQuery;
        request->addresses.reserve(m_impl->maximumAddressesPerQuery);
        switch (family) {
        case DnsAddressFamily::V4Only:
            request->addressFamily = AF_INET;
            break;
        case DnsAddressFamily::V6Only:
            request->addressFamily = AF_INET6;
            break;
        case DnsAddressFamily::Unspecified:
            request->addressFamily = AF_UNSPEC;
            break;
        }

        work = [request]() noexcept { performResolution(request); };
    } catch (const std::bad_alloc&) {
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "DNS query state allocation failed");
    } catch (...) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "DNS query setup failed");
    }

    Core::Status scheduled = Core::success();
    try {
        scheduled = m_impl->taskSystem->scheduleIo(std::move(work));
    } catch (...) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "task system threw while scheduling a DNS query");
    }
    if (!scheduled) {
        // A full IO queue is transient, so it maps to the same rejection a full
        // slot table does: retry later. Anything else is a real failure.
        ++m_impl->stats.rejectedQueryCount;
        if (scheduled.error().code == Task::TaskErrorCode::QueueFull) {
            return Core::failure(
                NetworkErrorCode::DnsQueryRejected,
                "task system IO queue is full; retry on a later frame");
        }
        return Core::failure(std::move(scheduled.error()));
    }

    // Only claim the slot once the work is accepted, so a rejected schedule leaves
    // no half-live query behind.
    m_impl->clearSlot(*target);
    target->request = std::move(request);
    target->generation = m_impl->nextGeneration;
    target->state = DnsQueryState::Pending;

    // Generation zero marks a dead handle, so it is skipped on wrap.
    ++m_impl->nextGeneration;
    if (m_impl->nextGeneration == 0) {
        m_impl->nextGeneration = 1;
    }

    ++m_impl->stats.startedQueryCount;
    return DnsQueryHandle{slotIndex, target->generation};
}

Core::Result<Core::usize> DnsResolver::pump()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "DnsResolver is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "DnsResolver must be used from its owner thread");
    }

    ++m_impl->stats.pumpCallCount;

    Core::usize completed = 0;
    Core::usize pending = 0;

    for (auto& slot : m_impl->slots) {
        if (slot.generation == 0) {
            continue;
        }

        if (slot.abandoned) {
            // Nothing to deliver, but the slot stays claimed until the worker stops
            // writing to the shared state.
            if (slot.request != nullptr
                && slot.request->outcome.load(std::memory_order_acquire)
                    == DnsRequestState::Outcome::Running) {
                ++pending;
            }
            continue;
        }

        if (slot.state != DnsQueryState::Pending) {
            continue;
        }

        const auto outcome = slot.request != nullptr
            ? slot.request->outcome.load(std::memory_order_acquire)
            : DnsRequestState::Outcome::Failed;

        if (outcome == DnsRequestState::Outcome::Running) {
            ++pending;
            continue;
        }

        if (outcome == DnsRequestState::Outcome::Succeeded) {
            // Copy into the slot's own storage so the answer survives the shared
            // state being dropped.
            slot.addresses.assign(
                slot.request->addresses.begin(),
                slot.request->addresses.end());
            if (slot.request->truncated) {
                ++m_impl->stats.truncatedAnswerCount;
            }
            slot.state = DnsQueryState::Resolved;
            ++m_impl->stats.resolvedQueryCount;
        } else {
            slot.addresses.clear();
            slot.state = DnsQueryState::Failed;
            ++m_impl->stats.failedQueryCount;
        }

        // The worker is done, so the shared state can go now rather than at
        // release().
        slot.request.reset();
        ++completed;
    }

    m_impl->stats.pendingQueryCount = pending;
    return completed;
}

DnsQueryState DnsResolver::queryState(DnsQueryHandle handle) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return DnsQueryState::Cancelled;
    }
    const auto* slot = m_impl->findSlot(handle);
    if (slot == nullptr) {
        // A released or stale handle reads as Cancelled rather than Failed: nothing
        // went wrong with the query, the handle simply no longer names one.
        return DnsQueryState::Cancelled;
    }
    if (slot->abandoned) {
        return DnsQueryState::Cancelled;
    }
    return slot->state;
}

Core::Result<std::span<const NetworkEndpoint>> DnsResolver::addresses(
    DnsQueryHandle handle) const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "DnsResolver is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "DnsResolver must be used from its owner thread");
    }

    const auto* slot = m_impl->findSlot(handle);
    if (slot == nullptr || slot->abandoned) {
        return Core::failure(
            NetworkErrorCode::InvalidQuery,
            "DNS query handle is stale or was released");
    }
    if (slot->state == DnsQueryState::Pending) {
        return Core::failure(
            NetworkErrorCode::DnsQueryPending,
            "DNS query has not finished");
    }
    if (slot->state != DnsQueryState::Resolved) {
        return Core::failure(
            NetworkErrorCode::DnsResolutionFailed,
            "DNS query did not resolve");
    }
    return std::span<const NetworkEndpoint>{slot->addresses.data(), slot->addresses.size()};
}

void DnsResolver::cancel(DnsQueryHandle handle) noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return;
    }
    auto* slot = m_impl->findSlot(handle);
    if (slot == nullptr || slot->abandoned) {
        return;
    }

    const bool workerRunning = slot->request != nullptr
        && slot->request->outcome.load(std::memory_order_acquire)
        == DnsRequestState::Outcome::Running;

    ++m_impl->stats.cancelledQueryCount;
    slot->addresses.clear();

    if (workerRunning) {
        // getaddrinfo cannot be interrupted, so the slot is marked and reclaimed
        // once the worker publishes. Claiming it now would let the worker write
        // into a slot a later query owns.
        slot->abandoned = true;
        slot->state = DnsQueryState::Cancelled;
        return;
    }

    m_impl->clearSlot(*slot);
}

void DnsResolver::release(DnsQueryHandle handle) noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return;
    }
    auto* slot = m_impl->findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    if (slot->state == DnsQueryState::Pending && !slot->abandoned) {
        // Releasing an unfinished query is a cancellation; routing it here keeps the
        // worker-still-running rule in one place.
        cancel(handle);
        return;
    }
    m_impl->clearSlot(*slot);
}

DnsResolverStatistics DnsResolver::statistics() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->stats;
}

Core::usize DnsResolver::queryCapacity() const noexcept
{
    return m_impl != nullptr ? m_impl->slots.size() : 0;
}

} // namespace Tina::Network
