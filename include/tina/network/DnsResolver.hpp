#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/NetworkEndpoint.hpp>
#include <tina/task/TaskSystem.hpp>

#include <memory_resource>
#include <span>
#include <string_view>

namespace Tina::Network {

enum class DnsQueryState : Core::u8 {
    Pending,
    Resolved,
    Failed,
    Cancelled,
};

// Generation-safe handle to one in-flight query. Reusing a slot bumps the
// generation, so a stale handle resolves to nothing rather than to someone
// else's answer.
struct DnsQueryHandle final {
    Core::u32 slot = 0;
    Core::u32 generation = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] friend constexpr bool operator==(
        const DnsQueryHandle&,
        const DnsQueryHandle&) = default;
};

// Which families to ask for. Unspecified accepts whatever the system returns,
// which is what a client that will connect to any of them should use.
enum class DnsAddressFamily : Core::u8 {
    Unspecified,
    V4Only,
    V6Only,
};

struct DnsResolverConfig final {
    // Required and borrowed. Must outlive the resolver, and must have at least one
    // IO worker: getaddrinfo blocks, so there is no way to run it on the owner
    // thread without stalling the frame.
    Task::ITaskSystem* taskSystem = nullptr;

    // Queries that may be in flight at once. Each holds a fixed result slot, so
    // this bounds the resolver's whole footprint.
    Core::usize queryCapacity = 8;

    // Addresses kept per query. A name with more is truncated to this many rather
    // than failing: a client only needs one reachable address, and the order the
    // system returned is preserved.
    Core::usize maximumAddressesPerQuery = 4;

    std::pmr::memory_resource* memoryResource = nullptr;
};

struct DnsResolverStatistics final {
    Core::usize pendingQueryCount = 0;

    Core::u64 pumpCallCount = 0;
    Core::u64 startedQueryCount = 0;
    Core::u64 resolvedQueryCount = 0;
    Core::u64 failedQueryCount = 0;
    Core::u64 cancelledQueryCount = 0;
    // Queries refused because every slot was busy. Not a failure of a query --
    // no query was created -- so it is counted apart from failedQueryCount.
    Core::u64 rejectedQueryCount = 0;
    // Answers that carried more addresses than maximumAddressesPerQuery.
    Core::u64 truncatedAnswerCount = 0;
};

// Resolves host names to endpoints without blocking the owner thread.
//
// This is the one place in Tina::Network that uses a worker thread. Every other
// transport is driven by a non-blocking readiness poll, but getaddrinfo has no
// portable non-blocking form, so the blocking call is pushed to an IO worker and
// the answer is collected by pump(). See ADR 0033 section 13.
//
// Every method must be called from the thread that called Create.
class DnsResolver final {
  public:
    [[nodiscard]] static Core::Result<DnsResolver> Create(DnsResolverConfig config);

    ~DnsResolver() noexcept;

    DnsResolver(const DnsResolver&) = delete;
    DnsResolver& operator=(const DnsResolver&) = delete;
    DnsResolver(DnsResolver&& other) noexcept;
    DnsResolver& operator=(DnsResolver&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    // Starts a query. The name is copied, so the caller's buffer need not outlive
    // the call. A numeric literal resolves without touching the network, which is
    // why a caller can pass either and not special-case it.
    //
    // Returns DnsQueryRejected when no slot is free; that is transient, so retry
    // on a later frame.
    [[nodiscard]] Core::Result<DnsQueryHandle> resolve(
        std::string_view hostName,
        Core::u16 port,
        DnsAddressFamily family = DnsAddressFamily::Unspecified);

    // Collects finished queries. Never blocks. Returns how many reached a terminal
    // state during this call.
    [[nodiscard]] Core::Result<Core::usize> pump();

    [[nodiscard]] DnsQueryState queryState(DnsQueryHandle handle) const noexcept;

    // Resolved addresses in the order the system returned them. Valid only while
    // the query is Resolved and until release() or destruction. Empty otherwise.
    [[nodiscard]] Core::Result<std::span<const NetworkEndpoint>> addresses(
        DnsQueryHandle handle) const noexcept;

    // Stops delivering the answer and frees the slot. The blocking call may still
    // be running on a worker; this cannot interrupt it, so the slot is only reused
    // once that call returns.
    void cancel(DnsQueryHandle handle) noexcept;

    // Frees a finished query's slot. A resolved query holds its slot until this is
    // called, so an answer is never overwritten before it is read.
    void release(DnsQueryHandle handle) noexcept;

    [[nodiscard]] DnsResolverStatistics statistics() const noexcept;

    [[nodiscard]] Core::usize queryCapacity() const noexcept;

  private:
    struct Impl;

    explicit DnsResolver(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Network
