#pragma once

#include <tina/asset/CatalogPackage.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory>
#include <string_view>

namespace Tina::Asset {

inline constexpr Core::u32 DefaultCatalogPackageWatchBufferBytes = 16U * 1024U;
inline constexpr Core::u32 MinCatalogPackageWatchBufferBytes = 4U * 1024U;
inline constexpr Core::u32 MaxCatalogPackageWatchBufferBytes = 64U * 1024U;

enum class CatalogPackageWatchState : Core::u8 {
    Quiet = 0,
    Changed = 1,
    RescanRequired = 2,
};

struct CatalogPackageWatchProbe final {
    CatalogPackageWatchState state = CatalogPackageWatchState::Quiet;
    Core::u32 eventCount = 0;
};

struct CatalogPackageWatcherConfig final {
    Core::u32 eventBufferBytes = DefaultCatalogPackageWatchBufferBytes;
    std::string_view manifestRelativePath = DefaultCatalogManifestRelativePath;
};

// Non-blocking OS hint for changes to the Catalog manifest commit marker. Create arms the
// platform watch before returning. A hint never validates the package or advances a revision;
// callers still use pollCatalogPackageChange() and accept its candidate only after full reload.
class CatalogPackageWatcher final {
public:
    [[nodiscard]] static Core::Result<CatalogPackageWatcher>
    Create(std::string_view catalogRootUtf8, CatalogPackageWatcherConfig config = {});

    ~CatalogPackageWatcher() noexcept;

    CatalogPackageWatcher(const CatalogPackageWatcher&) = delete;
    CatalogPackageWatcher& operator=(const CatalogPackageWatcher&) = delete;
    CatalogPackageWatcher(CatalogPackageWatcher&& other) noexcept;
    CatalogPackageWatcher& operator=(CatalogPackageWatcher&& other) noexcept;

    // Drains currently available native events without blocking. RescanRequired means event
    // loss or directory invalidation; the caller must rescan and recreate an invalidated watcher.
    [[nodiscard]] Core::Result<CatalogPackageWatchProbe> poll();

private:
    struct Impl;
    explicit CatalogPackageWatcher(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::Asset
