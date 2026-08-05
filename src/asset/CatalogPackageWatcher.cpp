#include <tina/asset/CatalogPackageWatcher.hpp>

#include "CatalogPackagePath.hpp"

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Error nativeWatchError(Core::ErrorCode code, std::string_view message,
                                           Core::i64 nativeCode)
{
    Core::Error error{code, message};
    error.setNativeCode(nativeCode);
    return error;
}

[[nodiscard]] CatalogPackageWatchProbe rescanProbe(Core::u32 eventCount = 0U) noexcept
{
    return CatalogPackageWatchProbe{
        .state = CatalogPackageWatchState::RescanRequired,
        .eventCount = eventCount,
    };
}

void countEvent(Core::u32& eventCount) noexcept
{
    if (eventCount != (std::numeric_limits<Core::u32>::max)())
    {
        ++eventCount;
    }
}

} // namespace

#if defined(_WIN32)
namespace {

[[nodiscard]] Core::ErrorCode windowsWatchErrorCode(DWORD nativeCode) noexcept
{
    switch (nativeCode)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_DIRECTORY:
        return Core::CoreErrorCode::NotFound;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        return Core::CoreErrorCode::PermissionDenied;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return Core::CoreErrorCode::OutOfMemory;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:
        return Core::CoreErrorCode::Unsupported;
    default:
        return Core::CoreErrorCode::Io;
    }
}

[[nodiscard]] Core::Error windowsWatchError(std::string_view message, DWORD nativeCode)
{
    return nativeWatchError(windowsWatchErrorCode(nativeCode), message,
                            static_cast<Core::i64>(nativeCode));
}

[[nodiscard]] bool isWindowsDirectoryInvalidation(DWORD nativeCode) noexcept
{
    return nativeCode == ERROR_ACCESS_DENIED || nativeCode == ERROR_FILE_NOT_FOUND ||
           nativeCode == ERROR_PATH_NOT_FOUND || nativeCode == ERROR_INVALID_HANDLE ||
           nativeCode == ERROR_OPERATION_ABORTED;
}

[[nodiscard]] bool isManifestAction(DWORD action) noexcept
{
    return action == FILE_ACTION_ADDED || action == FILE_ACTION_REMOVED ||
           action == FILE_ACTION_MODIFIED || action == FILE_ACTION_RENAMED_OLD_NAME ||
           action == FILE_ACTION_RENAMED_NEW_NAME;
}

} // namespace

struct CatalogPackageWatcher::Impl final {
    struct WatchBuffer final {
        std::vector<std::byte> bytes;
        OVERLAPPED overlapped{};
        HANDLE event = nullptr;
        bool pending = false;
    };

    static Core::Result<std::unique_ptr<Impl>>
    Create(const Detail::CatalogManifestPath& manifestPath, Core::u32 eventBufferBytes)
    {
        auto impl = std::unique_ptr<Impl>(new Impl());
        impl->m_manifestFileName = manifestPath.fileName.native();
        for (auto& buffer : impl->m_buffers)
        {
            buffer.bytes.resize(eventBufferBytes);
            buffer.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (buffer.event == nullptr)
            {
                const DWORD nativeCode = GetLastError();
                return Core::failure(windowsWatchError(
                    "failed to create Catalog package watcher event", nativeCode));
            }
        }

        impl->m_directory = CreateFileW(
            manifestPath.directory.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (impl->m_directory == INVALID_HANDLE_VALUE)
        {
            const DWORD nativeCode = GetLastError();
            return Core::failure(windowsWatchError(
                "failed to open Catalog manifest directory for watching", nativeCode));
        }

        auto armed = impl->arm(0U);
        if (!armed)
        {
            return Core::failure(std::move(armed.error()).withContext(
                "CatalogPackageWatcher::Create", "arm"));
        }
        impl->m_activeBuffer = 0U;
        return impl;
    }

    ~Impl() noexcept
    {
        if (m_directory != INVALID_HANDLE_VALUE)
        {
            for (auto& buffer : m_buffers)
            {
                if (buffer.pending)
                {
                    static_cast<void>(CancelIoEx(m_directory, &buffer.overlapped));
                }
            }
            for (auto& buffer : m_buffers)
            {
                if (buffer.pending && buffer.event != nullptr)
                {
                    static_cast<void>(WaitForSingleObject(buffer.event, INFINITE));
                    DWORD ignoredBytes = 0;
                    static_cast<void>(
                        GetOverlappedResult(m_directory, &buffer.overlapped, &ignoredBytes, FALSE));
                    buffer.pending = false;
                }
            }
            CloseHandle(m_directory);
            m_directory = INVALID_HANDLE_VALUE;
        }
        for (auto& buffer : m_buffers)
        {
            if (buffer.event != nullptr)
            {
                CloseHandle(buffer.event);
                buffer.event = nullptr;
            }
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] Core::Result<CatalogPackageWatchProbe> poll()
    {
        if (m_invalidated)
        {
            return rescanProbe();
        }

        WatchBuffer& completed = m_buffers[m_activeBuffer];
        DWORD transferredBytes = 0;
        if (!GetOverlappedResult(m_directory, &completed.overlapped, &transferredBytes, FALSE))
        {
            const DWORD nativeCode = GetLastError();
            if (nativeCode == ERROR_IO_INCOMPLETE)
            {
                return CatalogPackageWatchProbe{};
            }

            completed.pending = false;
            if (isWindowsDirectoryInvalidation(nativeCode))
            {
                m_invalidated = true;
                return rescanProbe();
            }
            if (nativeCode == ERROR_NOTIFY_ENUM_DIR)
            {
                auto rearmed = armNext(m_activeBuffer);
                if (!rearmed)
                {
                    return Core::failure(std::move(rearmed.error()));
                }
                return rescanProbe();
            }
            return Core::failure(windowsWatchError(
                "failed to collect Catalog package watcher events", nativeCode));
        }
        completed.pending = false;

        const std::size_t completedIndex = m_activeBuffer;
        auto rearmed = armNext(completedIndex);
        if (!rearmed)
        {
            const auto nativeCode = rearmed.error().nativeCode;
            if (nativeCode && isWindowsDirectoryInvalidation(static_cast<DWORD>(*nativeCode)))
            {
                m_invalidated = true;
                return rescanProbe();
            }
            return Core::failure(std::move(rearmed.error()));
        }

        if (transferredBytes == 0U || transferredBytes > completed.bytes.size())
        {
            return rescanProbe();
        }
        return parseEvents(completed.bytes.data(), transferredBytes);
    }

private:
    Impl() = default;

    [[nodiscard]] Core::Status arm(std::size_t bufferIndex)
    {
        WatchBuffer& buffer = m_buffers[bufferIndex];
        ResetEvent(buffer.event);
        buffer.overlapped = {};
        buffer.overlapped.hEvent = buffer.event;

        constexpr DWORD NotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME |
                                       FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                       FILE_NOTIFY_CHANGE_SIZE |
                                       FILE_NOTIFY_CHANGE_LAST_WRITE |
                                       FILE_NOTIFY_CHANGE_CREATION;
        const BOOL started = ReadDirectoryChangesW(
            m_directory, buffer.bytes.data(), static_cast<DWORD>(buffer.bytes.size()), FALSE,
            NotifyFilter, nullptr, &buffer.overlapped, nullptr);
        if (!started)
        {
            const DWORD nativeCode = GetLastError();
            if (nativeCode != ERROR_IO_PENDING)
            {
                return Core::failure(windowsWatchError(
                    "failed to arm Catalog package directory watch", nativeCode));
            }
        }
        buffer.pending = true;
        return Core::success();
    }

    [[nodiscard]] Core::Status armNext(std::size_t completedIndex)
    {
        const std::size_t nextIndex = (completedIndex + 1U) % m_buffers.size();
        auto armed = arm(nextIndex);
        if (!armed)
        {
            return Core::failure(std::move(armed.error()).withContext(
                "CatalogPackageWatcher::poll", "rearm"));
        }
        m_activeBuffer = nextIndex;
        return Core::success();
    }

    [[nodiscard]] CatalogPackageWatchProbe parseEvents(const std::byte* bytes,
                                                       std::size_t byteCount) const noexcept
    {
        constexpr std::size_t HeaderBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        CatalogPackageWatchProbe probe{};
        std::size_t offset = 0;
        for (;;)
        {
            const std::size_t remaining = byteCount - offset;
            if (remaining < HeaderBytes)
            {
                return rescanProbe(probe.eventCount);
            }

            const auto* event =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(bytes + offset);
            const std::size_t fileNameBytes = event->FileNameLength;
            if ((fileNameBytes % sizeof(wchar_t)) != 0U ||
                fileNameBytes > remaining - HeaderBytes)
            {
                return rescanProbe(probe.eventCount);
            }

            const std::wstring_view fileName{event->FileName,
                                             fileNameBytes / sizeof(wchar_t)};
            if (fileName == m_manifestFileName)
            {
                if (!isManifestAction(event->Action))
                {
                    return rescanProbe(probe.eventCount);
                }
                countEvent(probe.eventCount);
                probe.state = CatalogPackageWatchState::Changed;
            }

            if (event->NextEntryOffset == 0U)
            {
                return probe;
            }
            if (event->NextEntryOffset < HeaderBytes + fileNameBytes ||
                event->NextEntryOffset > remaining)
            {
                return rescanProbe(probe.eventCount);
            }
            offset += event->NextEntryOffset;
        }
    }

    HANDLE m_directory = INVALID_HANDLE_VALUE;
    std::array<WatchBuffer, 2U> m_buffers{};
    std::wstring m_manifestFileName;
    std::size_t m_activeBuffer = 0U;
    bool m_invalidated = false;
};

#elif defined(__linux__)
namespace {

[[nodiscard]] Core::ErrorCode linuxWatchErrorCode(int nativeCode) noexcept
{
    switch (nativeCode)
    {
    case ENOENT:
    case ENOTDIR:
        return Core::CoreErrorCode::NotFound;
    case EACCES:
    case EPERM:
        return Core::CoreErrorCode::PermissionDenied;
    case ENOMEM:
        return Core::CoreErrorCode::OutOfMemory;
    case ENOSPC:
    case EMFILE:
    case ENFILE:
        return Core::CoreErrorCode::CapacityExceeded;
    case ENOSYS:
        return Core::CoreErrorCode::Unsupported;
    default:
        return Core::CoreErrorCode::Io;
    }
}

[[nodiscard]] Core::Error linuxWatchError(std::string_view message, int nativeCode)
{
    return nativeWatchError(linuxWatchErrorCode(nativeCode), message,
                            static_cast<Core::i64>(nativeCode));
}

} // namespace

struct CatalogPackageWatcher::Impl final {
    static Core::Result<std::unique_ptr<Impl>>
    Create(const Detail::CatalogManifestPath& manifestPath, Core::u32 eventBufferBytes)
    {
        auto impl = std::unique_ptr<Impl>(new Impl());
        impl->m_manifestFileName = manifestPath.fileName.native();
        impl->m_buffer.resize(eventBufferBytes);

        impl->m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (impl->m_inotify < 0)
        {
            const int nativeCode = errno;
            return Core::failure(linuxWatchError(
                "failed to create Catalog package inotify instance", nativeCode));
        }

        constexpr uint32_t WatchMask = IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
                                       IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF |
                                       IN_MOVE_SELF | IN_UNMOUNT | IN_ONLYDIR | IN_EXCL_UNLINK;
        impl->m_watchDescriptor =
            inotify_add_watch(impl->m_inotify, manifestPath.directory.c_str(), WatchMask);
        if (impl->m_watchDescriptor < 0)
        {
            const int nativeCode = errno;
            return Core::failure(linuxWatchError(
                "failed to watch Catalog manifest directory", nativeCode));
        }
        return impl;
    }

    ~Impl() noexcept
    {
        if (m_inotify >= 0)
        {
            if (m_watchDescriptor >= 0)
            {
                static_cast<void>(inotify_rm_watch(m_inotify, m_watchDescriptor));
            }
            ::close(m_inotify);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] Core::Result<CatalogPackageWatchProbe> poll()
    {
        if (m_invalidated)
        {
            return rescanProbe();
        }

        CatalogPackageWatchProbe probe{};
        for (;;)
        {
            const ssize_t bytesRead = ::read(m_inotify, m_buffer.data(), m_buffer.size());
            if (bytesRead < 0)
            {
                const int nativeCode = errno;
                if (nativeCode == EINTR)
                {
                    continue;
                }
                if (nativeCode == EAGAIN || nativeCode == EWOULDBLOCK)
                {
                    return probe;
                }
                return Core::failure(linuxWatchError(
                    "failed to read Catalog package watcher events", nativeCode));
            }
            if (bytesRead == 0)
            {
                return rescanProbe(probe.eventCount);
            }

            parseEvents(m_buffer.data(), static_cast<std::size_t>(bytesRead), probe);
            if (probe.state == CatalogPackageWatchState::RescanRequired && m_invalidated)
            {
                return probe;
            }
        }
    }

private:
    Impl() = default;

    void parseEvents(const std::byte* bytes, std::size_t byteCount,
                     CatalogPackageWatchProbe& probe) noexcept
    {
        std::size_t offset = 0;
        while (offset < byteCount)
        {
            const std::size_t remaining = byteCount - offset;
            if (remaining < sizeof(inotify_event))
            {
                probe.state = CatalogPackageWatchState::RescanRequired;
                return;
            }

            const auto* event = reinterpret_cast<const inotify_event*>(bytes + offset);
            const std::size_t eventBytes = sizeof(inotify_event) + event->len;
            if (eventBytes > remaining)
            {
                probe.state = CatalogPackageWatchState::RescanRequired;
                return;
            }

            if ((event->mask & IN_Q_OVERFLOW) != 0U)
            {
                probe.state = CatalogPackageWatchState::RescanRequired;
            }
            if ((event->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_IGNORED)) != 0U)
            {
                probe.state = CatalogPackageWatchState::RescanRequired;
                m_invalidated = true;
                m_watchDescriptor = -1;
            }

            if (event->wd == m_watchDescriptor && event->len > 0U)
            {
                const char* name = event->name;
                std::size_t nameBytes = 0;
                while (nameBytes < event->len && name[nameBytes] != '\0')
                {
                    ++nameBytes;
                }
                if (nameBytes == event->len)
                {
                    probe.state = CatalogPackageWatchState::RescanRequired;
                    return;
                }

                constexpr uint32_t ManifestMask = IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE |
                                                  IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                                                  IN_MOVED_TO;
                if ((event->mask & ManifestMask) != 0U &&
                    std::string_view{name, nameBytes} == m_manifestFileName)
                {
                    countEvent(probe.eventCount);
                    if (probe.state != CatalogPackageWatchState::RescanRequired)
                    {
                        probe.state = CatalogPackageWatchState::Changed;
                    }
                }
            }
            offset += eventBytes;
        }
    }

    int m_inotify = -1;
    int m_watchDescriptor = -1;
    std::vector<std::byte> m_buffer;
    std::string m_manifestFileName;
    bool m_invalidated = false;
};

#else

struct CatalogPackageWatcher::Impl final {
    static Core::Result<std::unique_ptr<Impl>>
    Create(const Detail::CatalogManifestPath&, Core::u32)
    {
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "Catalog package watcher supports only Windows and Linux");
    }

    [[nodiscard]] Core::Result<CatalogPackageWatchProbe> poll()
    {
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "Catalog package watcher supports only Windows and Linux");
    }
};

#endif

Core::Result<CatalogPackageWatcher>
CatalogPackageWatcher::Create(std::string_view catalogRootUtf8, CatalogPackageWatcherConfig config)
{
    if (config.eventBufferBytes < MinCatalogPackageWatchBufferBytes ||
        config.eventBufferBytes > MaxCatalogPackageWatchBufferBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Catalog package watcher event buffer size is invalid");
    }

    auto manifestPath =
        Detail::resolveCatalogManifestPath(catalogRootUtf8, config.manifestRelativePath);
    if (!manifestPath)
    {
        return Core::failure(std::move(manifestPath.error()).withContext(
            "CatalogPackageWatcher::Create", "manifestPath"));
    }

    try
    {
        auto impl = Impl::Create(*manifestPath, config.eventBufferBytes);
        if (!impl)
        {
            return Core::failure(std::move(impl.error()).withContext(
                "CatalogPackageWatcher::Create", "platformWatch"));
        }
        return CatalogPackageWatcher{std::move(*impl)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "failed to allocate Catalog package watcher");
    }
}

CatalogPackageWatcher::CatalogPackageWatcher(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

CatalogPackageWatcher::~CatalogPackageWatcher() noexcept = default;

CatalogPackageWatcher::CatalogPackageWatcher(CatalogPackageWatcher&& other) noexcept = default;

CatalogPackageWatcher&
CatalogPackageWatcher::operator=(CatalogPackageWatcher&& other) noexcept = default;

Core::Result<CatalogPackageWatchProbe> CatalogPackageWatcher::poll()
{
    if (!m_impl)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Catalog package watcher is not initialized");
    }
    return m_impl->poll();
}

} // namespace Tina::Asset
