#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>

#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Core {

namespace PackageWire {
inline constexpr u32 Magic = 0x4B435054U; // TPCK, little-endian
inline constexpr u32 SchemaVersion = 2;
inline constexpr usize HeaderBytes = 64;
inline constexpr usize EntryBytes = 48;
inline constexpr usize DataAlignment = 16;
} // namespace PackageWire

struct PackageOpenConfig final {
    // Input-validation byte budget, not a file-count/path-length limit. Zero disables the budget.
    u64 maxMetadataBytes = 64ULL * 1024ULL * 1024ULL;
};

struct PackageWriteEntry final {
    std::string_view path;
    std::span<const std::byte> bytes;
};

struct PackageWriteConfig final {
    bool createParents = true;
};

struct PackageFileInfo final {
    // Borrowed until the last reader/view of this immutable package is released.
    std::string_view path;
    u64 size = 0;
    ContentHash contentHash;
};

// Shared immutable-storage counters. A concurrent snapshot is observational,
// not a transaction. Bytes count digest work, not heap/resident/physical IO.
struct PackageReadStatistics final {
    u64 payloadValidationPasses = 0;
    u64 payloadValidationBytes = 0;
    u64 payloadValidationCacheHits = 0;
    u64 payloadValidationFailures = 0;
};

namespace Detail { struct PackageStorage; }

// Copyable pin, not a borrowed window. Remains valid after the reader is moved/destroyed,
// or the package path is atomically replaced. Empty files still have a valid pin.
class PackageFileView final {
  public:
    [[nodiscard]] explicit operator bool() const noexcept { return m_storage != nullptr; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return m_storage ? m_bytes : std::span<const std::byte>{}; }

  private:
    friend class PackageReader;
    std::shared_ptr<const Detail::PackageStorage> m_storage;
    std::span<const std::byte> m_bytes;
};

// Immutable, shared-lifetime package. File-backed packages map once (virtual address space,
// not a full heap copy); pages are faulted in by the OS on demand. Concurrent const reads are
// safe. Publishing must replace, never truncate/modify an already-open package in place.
class PackageReader final {
  public:
    [[nodiscard]] static Result<PackageReader> Open(std::string_view utf8Path,
                                                   PackageOpenConfig config = {});
    [[nodiscard]] static Result<PackageReader> FromMemory(std::vector<std::byte> bytes,
                                                         PackageOpenConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept { return m_storage != nullptr; }
    [[nodiscard]] usize fileCount() const noexcept;
    [[nodiscard]] std::optional<PackageFileInfo> entry(usize index) const noexcept;
    [[nodiscard]] bool hasFile(std::string_view path) const noexcept;
    [[nodiscard]] std::optional<u64> getFileSize(std::string_view path) const noexcept;
    [[nodiscard]] PackageReadStatistics statistics() const noexcept;

    // Exact, case-sensitive canonical UTF-8 paths. Lookup performs no allocation. Both read
    // APIs require the entry digest to be verified. The immutable storage caches
    // success/failure once per entry, with concurrent first reads single-flight.
    // A newly opened/replaced package never inherits another storage's result.
    // maxBytes=0 means no caller-imposed byte budget; every call checks its budget.
    [[nodiscard]] Result<PackageFileView> viewFile(std::string_view path, u64 maxBytes = 0) const;
    [[nodiscard]] Result<std::pmr::vector<std::byte>> readFile(
        std::string_view path, std::pmr::memory_resource* memoryResource, u64 maxBytes = 0) const;

  private:
    [[nodiscard]] std::optional<usize> find(std::string_view path) const noexcept;
    std::shared_ptr<const Detail::PackageStorage> m_storage;
};

// Synchronous, deterministic and atomic. Borrows input buffers only for this call; writes
// metadata and payload spans directly without retaining/copying all payloads into a second
// archive-sized buffer. Duplicate/unsafe paths fail before touching the destination.
[[nodiscard]] Status writePackageFile(std::string_view utf8Path,
                                       std::span<const PackageWriteEntry> entries,
                                       PackageWriteConfig config = {});

} // namespace Tina::Core
