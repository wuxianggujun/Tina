#include <tina/core/io/PackageFile.hpp>

#include "PathUtil.hpp"

#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/core/trace/Trace.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Tina::Core {
namespace Detail {

struct PackageStorage final {
    std::span<const std::byte> bytes;
    std::vector<std::byte> embedded;
    usize count = 0;
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
#else
    int file = -1;
#endif
    void* view = nullptr;

    ~PackageStorage()
    {
#if defined(_WIN32)
        if (view != nullptr) ::UnmapViewOfFile(view);
        if (mapping != nullptr) ::CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file);
#else
        if (view != nullptr) ::munmap(view, bytes.size());
        if (file != -1) ::close(file);
#endif
    }
};

} // namespace Detail
namespace {

constexpr u64 MaxU64 = (std::numeric_limits<u64>::max)();
constexpr u64 MaxSize = (std::numeric_limits<usize>::max)();

template <typename Integer = u64>
Integer readLe(std::span<const std::byte> bytes, usize offset) noexcept
{
    // memcpy supports unaligned wire bytes and compiles to a word load on little-endian
    // targets; reconstructing eight bytes per field inflated every binary-search step.
    Integer value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    if constexpr (std::endian::native == std::endian::big) value = std::byteswap(value);
    return value;
}

void writeLe(std::span<std::byte> bytes, usize offset, u64 value, usize count = 8) noexcept
{
    for (usize i = 0; i < count; ++i) bytes[offset + i] = std::byte((value >> (i * 8)) & 0xffU);
}

ContentHash readHash(std::span<const std::byte> bytes, usize offset) noexcept
{
    ContentHash::Bytes hash{};
    std::memcpy(hash.data(), bytes.data() + offset, hash.size());
    return ContentHash::fromBytes(hash).value_or(ContentHash{});
}

u64 aligned(u64 value) noexcept
{
    constexpr u64 mask = PackageWire::DataAlignment - 1;
    return (value + mask) & ~mask;
}

bool validVirtualPath(std::string_view path) noexcept
{
    if (path.empty() || !isStrictUtf8WithoutNul(path) || path.front() == '/' || path.back() == '/' ||
        path.find_first_of("\\:") != std::string_view::npos) return false;
    while (!path.empty())
    {
        const auto slash = path.find('/');
        const auto component = path.substr(0, slash);
        if (component.empty() || component == "." || component == "..") return false;
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return true;
}

Error nativeError(std::string_view operation, int code)
{
    const std::error_code native(code, std::system_category());
    const auto errorCode = native == std::errc::no_such_file_or_directory ? CoreErrorCode::NotFound :
                           native == std::errc::permission_denied ? CoreErrorCode::PermissionDenied : CoreErrorCode::Io;
    Error error{errorCode, operation};
    error.setNativeCode(code);
    error.addContext("native", native.message());
    return error;
}

usize recordOffset(usize index) noexcept { return PackageWire::HeaderBytes + index * PackageWire::EntryBytes; }

std::string_view entryPath(const Detail::PackageStorage& storage, usize index) noexcept
{
    const auto offset = recordOffset(index);
    const auto start = static_cast<usize>(readLe(storage.bytes, offset));
    const auto length = static_cast<usize>(readLe(storage.bytes, offset + 8));
    return {reinterpret_cast<const char*>(storage.bytes.data() + PackageWire::HeaderBytes + start), length};
}

Status validateStorage(Detail::PackageStorage& storage, PackageOpenConfig config)
{
    const auto bytes = storage.bytes;
    if (bytes.size() < PackageWire::HeaderBytes)
        return failure(CoreErrorCode::InvalidArgument, "truncated package header");
    if (readLe<u32>(bytes, 0) != PackageWire::Magic || readLe<u32>(bytes, 4) != PackageWire::SchemaVersion)
        return failure(CoreErrorCode::Unsupported, "unsupported package magic/schema; recook the package");
    if (readLe<u32>(bytes, 8) != PackageWire::HeaderBytes || readLe<u32>(bytes, 12) != PackageWire::EntryBytes ||
        readLe(bytes, 32) != bytes.size())
        return failure(CoreErrorCode::InvalidArgument, "invalid package header or file extent");
    const u64 count = readLe(bytes, 16);
    const u64 indexBytes = readLe(bytes, 24);
    const u64 namesBytes = readLe(bytes, 40);
    if (indexBytes > bytes.size() - PackageWire::HeaderBytes || count > indexBytes / PackageWire::EntryBytes ||
        namesBytes > indexBytes - count * PackageWire::EntryBytes)
        return failure(CoreErrorCode::InvalidArgument, "package index range/count overflows file");
    if (config.maxMetadataBytes != 0 && indexBytes > config.maxMetadataBytes)
        return failure(CoreErrorCode::CapacityExceeded, "package metadata exceeds configured byte budget");
    const u64 namesEnd = count * PackageWire::EntryBytes + namesBytes;
    if (namesEnd > MaxU64 - 15 || aligned(namesEnd) != indexBytes)
        return failure(CoreErrorCode::InvalidArgument, "invalid package index extent/alignment");
    const auto index = bytes.subspan(PackageWire::HeaderBytes, static_cast<usize>(indexBytes));
    const auto digest = digestContentHashV1(index);
    if (!digest || *digest != readHash(bytes, 48))
        return failure(CoreErrorCode::InvalidArgument, "package index digest mismatch");
    for (usize i = static_cast<usize>(namesEnd); i < index.size(); ++i)
        if (index[i] != std::byte{0}) return failure(CoreErrorCode::InvalidArgument, "nonzero package index padding");

    u64 nameCursor = count * PackageWire::EntryBytes;
    u64 dataCursor = PackageWire::HeaderBytes + indexBytes;
    std::string_view previous;
    for (usize i = 0; i < static_cast<usize>(count); ++i)
    {
        const auto record = recordOffset(i);
        const u64 pathOffset = readLe(bytes, record);
        const u64 pathBytes = readLe(bytes, record + 8);
        const u64 dataOffset = readLe(bytes, record + 16);
        const u64 dataBytes = readLe(bytes, record + 24);
        if (pathOffset != nameCursor || pathBytes > namesEnd - nameCursor)
            return failure(CoreErrorCode::InvalidArgument, "invalid package path range");
        const auto path = entryPath(storage, i);
        if (!validVirtualPath(path) || (i != 0 && path <= previous))
            return failure(CoreErrorCode::InvalidArgument, "package paths must be canonical, unique and sorted UTF-8");
        if (dataCursor > MaxU64 - 15 || dataOffset != aligned(dataCursor) ||
            dataOffset > bytes.size() || dataBytes > bytes.size() - dataOffset || !readHash(bytes, record + 32))
            return failure(CoreErrorCode::InvalidArgument, "invalid package payload range/digest");
        nameCursor += pathBytes;
        dataCursor = dataOffset + dataBytes;
        previous = path;
    }
    if (nameCursor != namesEnd || dataCursor != bytes.size())
        return failure(CoreErrorCode::InvalidArgument, "package has unused names or trailing payload bytes");
    storage.count = static_cast<usize>(count);
    return success();
}

} // namespace

Result<PackageReader> PackageReader::Open(std::string_view utf8Path, PackageOpenConfig config)
{
    TINA_TRACE_ZONE("Package.Open");
    if (utf8Path.empty() || !isStrictUtf8WithoutNul(utf8Path))
        return failure(CoreErrorCode::InvalidArgument, "package path must be strict UTF-8 without NUL");
    try
    {
        auto storage = std::make_shared<Detail::PackageStorage>();
        const auto path = Detail::pathFromUtf8Bytes(utf8Path);
        u64 size = 0;
#if defined(_WIN32)
        storage->file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (storage->file == INVALID_HANDLE_VALUE)
            return failure(nativeError("failed to open package", static_cast<int>(::GetLastError())));
        LARGE_INTEGER fileSize{};
        if (!::GetFileSizeEx(storage->file, &fileSize))
            return failure(nativeError("failed to query package size", static_cast<int>(::GetLastError())));
        if (fileSize.QuadPart < 0) return failure(CoreErrorCode::InvalidArgument, "negative package size");
        size = static_cast<u64>(fileSize.QuadPart);
#else
        storage->file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (storage->file == -1) return failure(nativeError("failed to open package", errno));
        struct stat info{};
        if (::fstat(storage->file, &info) != 0) return failure(nativeError("failed to query package size", errno));
        if (!S_ISREG(info.st_mode) || info.st_size < 0)
            return failure(CoreErrorCode::InvalidArgument, "package must be a regular file");
        size = static_cast<u64>(info.st_size);
#endif
        if (size < PackageWire::HeaderBytes) return failure(CoreErrorCode::InvalidArgument, "truncated package file");
        if (size > MaxSize) return failure(CoreErrorCode::CapacityExceeded, "package exceeds process address space");
#if defined(_WIN32)
        storage->mapping = ::CreateFileMappingW(storage->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (storage->mapping == nullptr)
            return failure(nativeError("failed to create package mapping", static_cast<int>(::GetLastError())));
        storage->view = ::MapViewOfFile(storage->mapping, FILE_MAP_READ, 0, 0, 0);
        if (storage->view == nullptr)
            return failure(nativeError("failed to map package", static_cast<int>(::GetLastError())));
#else
        storage->view = ::mmap(nullptr, static_cast<usize>(size), PROT_READ, MAP_PRIVATE, storage->file, 0);
        if (storage->view == MAP_FAILED) {
            storage->view = nullptr;
            return failure(nativeError("failed to map package", errno));
        }
#endif
        storage->bytes = {static_cast<const std::byte*>(storage->view), static_cast<usize>(size)};
        if (auto status = validateStorage(*storage, config); !status)
            return failure(std::move(status.error()).withContext("PackageReader::Open", utf8Path));
        PackageReader reader;
        reader.m_storage = std::move(storage);
        return reader;
    }
    catch (const std::bad_alloc&) { return failure(CoreErrorCode::OutOfMemory, "package open allocation failed"); }
    catch (const std::system_error& error) { return failure(nativeError("package path conversion failed", error.code().value())); }
}

Result<PackageReader> PackageReader::FromMemory(std::vector<std::byte> bytes, PackageOpenConfig config)
{
    try
    {
        auto storage = std::make_shared<Detail::PackageStorage>();
        storage->embedded = std::move(bytes);
        storage->bytes = storage->embedded;
        if (auto status = validateStorage(*storage, config); !status) return failure(std::move(status.error()));
        PackageReader reader;
        reader.m_storage = std::move(storage);
        return reader;
    }
    catch (const std::bad_alloc&) { return failure(CoreErrorCode::OutOfMemory, "package memory owner allocation failed"); }
}

usize PackageReader::fileCount() const noexcept { return m_storage ? m_storage->count : 0; }

std::optional<PackageFileInfo> PackageReader::entry(usize index) const noexcept
{
    if (index >= fileCount()) return std::nullopt;
    const auto record = recordOffset(index);
    return PackageFileInfo{entryPath(*m_storage, index), readLe(m_storage->bytes, record + 24),
                           readHash(m_storage->bytes, record + 32)};
}

std::optional<usize> PackageReader::find(std::string_view path) const noexcept
{
    usize first = 0, last = fileCount();
    while (first < last)
    {
        const usize middle = first + (last - first) / 2;
        const auto order = entryPath(*m_storage, middle).compare(path);
        if (order < 0) first = middle + 1;
        else if (order > 0) last = middle;
        else return middle;
    }
    return std::nullopt;
}

bool PackageReader::hasFile(std::string_view path) const noexcept { return find(path).has_value(); }

std::optional<u64> PackageReader::getFileSize(std::string_view path) const noexcept
{
    const auto index = find(path);
    if (!index) return std::nullopt;
    return readLe(m_storage->bytes, recordOffset(*index) + 24);
}

Result<PackageFileView> PackageReader::viewFile(std::string_view path, u64 maxBytes) const
{
    TINA_TRACE_ZONE("Package.View");
    const auto index = find(path);
    if (!index) return failure(CoreErrorCode::NotFound, "virtual file not found in package");
    const auto record = recordOffset(*index);
    const auto size = readLe(m_storage->bytes, record + 24);
    if (maxBytes != 0 && size > maxBytes)
        return failure(CoreErrorCode::CapacityExceeded, "virtual file exceeds configured byte budget");
    const auto bytes = m_storage->bytes.subspan(static_cast<usize>(readLe(m_storage->bytes, record + 16)),
                                                static_cast<usize>(size));
    const auto digest = digestContentHashV1(bytes);
    if (!digest || *digest != readHash(m_storage->bytes, record + 32))
        return failure(CoreErrorCode::InvalidArgument, "package payload digest mismatch");
    PackageFileView result;
    result.m_storage = m_storage;
    result.m_bytes = bytes;
    return result;
}

Result<std::pmr::vector<std::byte>> PackageReader::readFile(
    std::string_view path, std::pmr::memory_resource* memoryResource, u64 maxBytes) const
{
    if (memoryResource == nullptr) return failure(CoreErrorCode::InvalidArgument, "package read requires memory resource");
    auto view = viewFile(path, maxBytes);
    if (!view) return failure(std::move(view.error()));
    try { return std::pmr::vector<std::byte>(view->bytes().begin(), view->bytes().end(), memoryResource); }
    catch (const std::bad_alloc&) { return failure(CoreErrorCode::OutOfMemory, "package read allocation failed"); }
}

Status writePackageFile(std::string_view utf8Path, std::span<const PackageWriteEntry> entries,
                         PackageWriteConfig config)
{
    TINA_TRACE_ZONE("Package.Write");
    try
    {
        std::vector<PackageWriteEntry> sorted(entries.begin(), entries.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) { return left.path < right.path; });
        if (sorted.size() > (MaxSize - PackageWire::HeaderBytes - 15) / PackageWire::EntryBytes)
            return failure(CoreErrorCode::CapacityExceeded, "package metadata exceeds address space");
        u64 namesBytes = 0;
        for (usize i = 0; i < sorted.size(); ++i)
        {
            if (!validVirtualPath(sorted[i].path)) return failure(CoreErrorCode::InvalidArgument, "invalid package virtual path");
            if (i != 0 && sorted[i].path == sorted[i - 1].path)
                return failure(CoreErrorCode::AlreadyExists, "duplicate package virtual path");
            if (sorted[i].path.size() > MaxSize - namesBytes)
                return failure(CoreErrorCode::CapacityExceeded, "package path table exceeds address space");
            namesBytes += sorted[i].path.size();
        }
        const u64 recordsBytes = sorted.size() * PackageWire::EntryBytes;
        if (namesBytes > MaxSize - recordsBytes - PackageWire::HeaderBytes - 15)
            return failure(CoreErrorCode::CapacityExceeded, "package metadata exceeds address space");
        const u64 indexBytes = aligned(recordsBytes + namesBytes);
        std::vector<std::byte> metadata(PackageWire::HeaderBytes + static_cast<usize>(indexBytes));
        writeLe(metadata, 0, PackageWire::Magic, 4);
        writeLe(metadata, 4, PackageWire::SchemaVersion, 4);
        writeLe(metadata, 8, PackageWire::HeaderBytes, 4);
        writeLe(metadata, 12, PackageWire::EntryBytes, 4);
        writeLe(metadata, 16, sorted.size());
        writeLe(metadata, 24, indexBytes);
        writeLe(metadata, 40, namesBytes);
        u64 nameCursor = recordsBytes;
        u64 dataCursor = metadata.size();
        for (usize i = 0; i < sorted.size(); ++i)
        {
            const auto& item = sorted[i];
            if (dataCursor > MaxU64 - 15 || item.bytes.size() > MaxU64 - aligned(dataCursor))
                return failure(CoreErrorCode::CapacityExceeded, "package payload extent overflow");
            dataCursor = aligned(dataCursor);
            const auto record = recordOffset(i);
            writeLe(metadata, record, nameCursor);
            writeLe(metadata, record + 8, item.path.size());
            writeLe(metadata, record + 16, dataCursor);
            writeLe(metadata, record + 24, item.bytes.size());
            const auto digest = digestContentHashV1(item.bytes);
            if (!digest) return failure(digest.error());
            std::memcpy(metadata.data() + record + 32, digest->bytes().data(), 16);
            std::memcpy(metadata.data() + PackageWire::HeaderBytes + static_cast<usize>(nameCursor), item.path.data(), item.path.size());
            nameCursor += item.path.size();
            dataCursor += item.bytes.size();
        }
        writeLe(metadata, 32, dataCursor);
        const auto digest = digestContentHashV1(std::span<const std::byte>(metadata).subspan(PackageWire::HeaderBytes));
        if (!digest) return failure(digest.error());
        std::memcpy(metadata.data() + 48, digest->bytes().data(), 16);

        const std::array<std::byte, PackageWire::DataAlignment> padding{};
        std::vector<std::span<const std::byte>> parts;
        parts.reserve(1 + sorted.size() * 2);
        parts.push_back(metadata);
        dataCursor = metadata.size();
        for (const auto& item : sorted)
        {
            const auto paddingBytes = static_cast<usize>(aligned(dataCursor) - dataCursor);
            if (paddingBytes != 0) parts.emplace_back(padding.data(), paddingBytes);
            parts.push_back(item.bytes);
            dataCursor = aligned(dataCursor) + item.bytes.size();
        }
        return writeFileParts(utf8Path, parts, WriteFileConfig{.atomicReplace = true, .createParents = config.createParents});
    }
    catch (const std::bad_alloc&) { return failure(CoreErrorCode::OutOfMemory, "package writer metadata allocation failed"); }
}

} // namespace Tina::Core
