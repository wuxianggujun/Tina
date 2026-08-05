#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackageChangeDetector.hpp>
#include <tina/asset/CatalogPackageWatcher.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <thread>
#include <utility>

namespace Tina::Asset {
namespace {

using namespace std::chrono_literals;

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

class ScopedTestDirectory final {
public:
    explicit ScopedTestDirectory(std::string_view name)
    {
        static std::atomic<Core::u64> nextId{0U};
        const auto timeId = static_cast<Core::u64>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        m_path = std::filesystem::temp_directory_path() /
                 (std::string{name} + "_" + std::to_string(timeId) + "_" +
                  std::to_string(nextId.fetch_add(1U, std::memory_order_relaxed)));
        std::error_code errorCode;
        std::filesystem::create_directories(m_path, errorCode);
        m_created = !errorCode;
    }

    ~ScopedTestDirectory()
    {
        std::error_code errorCode;
        std::filesystem::remove_all(m_path, errorCode);
    }

    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }
    [[nodiscard]] bool created() const noexcept { return m_created; }

private:
    std::filesystem::path m_path;
    bool m_created = false;
};

[[nodiscard]] Core::Result<CatalogPackageWatchProbe>
waitForState(CatalogPackageWatcher& watcher, CatalogPackageWatchState expectedState,
             std::chrono::milliseconds timeout = 3s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        auto probe = watcher.poll();
        if (!probe)
        {
            return Core::failure(std::move(probe.error()));
        }
        if (probe->state == expectedState)
        {
            return probe;
        }
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < deadline);

    return Core::failure(Core::CoreErrorCode::Timeout,
                         "timed out waiting for Catalog package watcher state");
}

[[nodiscard]] Core::Status requireQuietFor(CatalogPackageWatcher& watcher,
                                           std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    do
    {
        auto probe = watcher.poll();
        if (!probe)
        {
            return Core::failure(std::move(probe.error()));
        }
        if (probe->state != CatalogPackageWatchState::Quiet)
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "unrelated file produced a Catalog manifest watch hint");
        }
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return Core::success();
}

constexpr std::array InitialManifestBytes{
    std::byte{0x10}, std::byte{0x20}, std::byte{0x30},
};
constexpr std::array ChangedManifestBytes{
    std::byte{0x10}, std::byte{0x21}, std::byte{0x30},
};

#if defined(_WIN32) || defined(__linux__)

TEST(CatalogPackageWatcherTests, FiltersSiblingEventsAndHintsManifestRevisionChange)
{
    ScopedTestDirectory testDirectory{"tina_catalog_package_watcher_filter"};
    ASSERT_TRUE(testDirectory.created());
    const auto manifestDirectory = testDirectory.path() / "metadata";
    ASSERT_TRUE(std::filesystem::create_directory(manifestDirectory));
    const auto manifest = manifestDirectory / "manifest.tmnft";
    ASSERT_TRUE(Core::writeFile(toUtf8(manifest), InitialManifestBytes).has_value());

    const CatalogPackageWatcherConfig watcherConfig{
        .manifestRelativePath = "metadata/manifest.tmnft",
    };
    auto watcher = CatalogPackageWatcher::Create(toUtf8(testDirectory.path()), watcherConfig);
    ASSERT_TRUE(watcher.has_value()) << watcher.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    const CatalogPackageChangeDetectorConfig detectorConfig{
        .scratchMemoryResource = &memory,
        .maxManifestBytes = 64U,
        .manifestRelativePath = watcherConfig.manifestRelativePath,
    };
    auto baseline = captureCatalogPackageRevision(toUtf8(testDirectory.path()), detectorConfig);
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;

    const auto sibling = manifestDirectory / "object.tmp";
    ASSERT_TRUE(Core::writeFile(toUtf8(sibling), ChangedManifestBytes).has_value());
    auto siblingProbe = requireQuietFor(*watcher, 100ms);
    ASSERT_TRUE(siblingProbe.has_value()) << siblingProbe.error().message;

    ASSERT_TRUE(Core::writeFile(toUtf8(manifest), ChangedManifestBytes).has_value());
    auto hint = waitForState(*watcher, CatalogPackageWatchState::Changed);
    ASSERT_TRUE(hint.has_value()) << hint.error().message;
    EXPECT_GE(hint->eventCount, 1U);

    auto revision =
        pollCatalogPackageChange(toUtf8(testDirectory.path()), *baseline, detectorConfig);
    ASSERT_TRUE(revision.has_value()) << revision.error().message;
    EXPECT_EQ(revision->state, CatalogPackageChangeState::Changed);
}

TEST(CatalogPackageWatcherTests, HintsManifestRenameDeleteAndReplacement)
{
    ScopedTestDirectory testDirectory{"tina_catalog_package_watcher_mutations"};
    ASSERT_TRUE(testDirectory.created());
    const auto manifest = testDirectory.path() / "manifest.tmnft";
    const auto renamed = testDirectory.path() / "manifest.old";

    ASSERT_TRUE(Core::writeFile(toUtf8(manifest), InitialManifestBytes).has_value());
    {
        auto watcher = CatalogPackageWatcher::Create(toUtf8(testDirectory.path()));
        ASSERT_TRUE(watcher.has_value()) << watcher.error().message;
        std::filesystem::rename(manifest, renamed);
        auto hint = waitForState(*watcher, CatalogPackageWatchState::Changed);
        ASSERT_TRUE(hint.has_value()) << hint.error().message;
        EXPECT_GE(hint->eventCount, 1U);
    }

    std::filesystem::rename(renamed, manifest);
    {
        auto watcher = CatalogPackageWatcher::Create(toUtf8(testDirectory.path()));
        ASSERT_TRUE(watcher.has_value()) << watcher.error().message;
        ASSERT_TRUE(std::filesystem::remove(manifest));
        auto hint = waitForState(*watcher, CatalogPackageWatchState::Changed);
        ASSERT_TRUE(hint.has_value()) << hint.error().message;
        EXPECT_GE(hint->eventCount, 1U);
    }

    ASSERT_TRUE(Core::writeFile(toUtf8(manifest), InitialManifestBytes).has_value());
    const auto replacement = testDirectory.path() / "replacement.tmp";
    ASSERT_TRUE(Core::writeFile(toUtf8(replacement), ChangedManifestBytes).has_value());
    {
        auto watcher = CatalogPackageWatcher::Create(toUtf8(testDirectory.path()));
        ASSERT_TRUE(watcher.has_value()) << watcher.error().message;
        ASSERT_TRUE(std::filesystem::remove(manifest));
        std::filesystem::rename(replacement, manifest);
        auto hint = waitForState(*watcher, CatalogPackageWatchState::Changed);
        ASSERT_TRUE(hint.has_value()) << hint.error().message;
        EXPECT_GE(hint->eventCount, 1U);
    }
}

TEST(CatalogPackageWatcherTests, ReportsRescanWhenWatchedDirectoryIsInvalidated)
{
    ScopedTestDirectory testDirectory{"tina_catalog_package_watcher_invalidation"};
    ASSERT_TRUE(testDirectory.created());
    const auto manifest = testDirectory.path() / "manifest.tmnft";
    ASSERT_TRUE(Core::writeFile(toUtf8(manifest), InitialManifestBytes).has_value());

    auto watcher = CatalogPackageWatcher::Create(toUtf8(testDirectory.path()));
    ASSERT_TRUE(watcher.has_value()) << watcher.error().message;

    std::error_code errorCode;
    std::filesystem::remove_all(testDirectory.path(), errorCode);
    ASSERT_FALSE(errorCode) << errorCode.message();
    auto invalidated = waitForState(*watcher, CatalogPackageWatchState::RescanRequired);
    ASSERT_TRUE(invalidated.has_value()) << invalidated.error().message;

    auto repeated = watcher->poll();
    ASSERT_TRUE(repeated.has_value()) << repeated.error().message;
    EXPECT_EQ(repeated->state, CatalogPackageWatchState::RescanRequired);
}

#endif

TEST(CatalogPackageWatcherTests, RejectsInvalidConfigAndMissingDirectory)
{
    CatalogPackageWatcherConfig smallBuffer{};
    smallBuffer.eventBufferBytes = MinCatalogPackageWatchBufferBytes - 1U;
    auto invalidBuffer = CatalogPackageWatcher::Create("catalog", smallBuffer);
    ASSERT_FALSE(invalidBuffer.has_value());
    EXPECT_EQ(invalidBuffer.error().code, AssetErrorCode::InvalidCatalogConfig);

    CatalogPackageWatcherConfig unsafePath{};
    unsafePath.manifestRelativePath = "../manifest.tmnft";
    auto escaped = CatalogPackageWatcher::Create("catalog", unsafePath);
    ASSERT_FALSE(escaped.has_value());
    EXPECT_EQ(escaped.error().code, AssetErrorCode::InvalidCatalogConfig);

#if defined(_WIN32) || defined(__linux__)
    ScopedTestDirectory testDirectory{"tina_catalog_package_watcher_missing"};
    ASSERT_TRUE(testDirectory.created());
    const auto missing = testDirectory.path() / "missing";
    auto missingDirectory = CatalogPackageWatcher::Create(toUtf8(missing));
    ASSERT_FALSE(missingDirectory.has_value());
    EXPECT_EQ(missingDirectory.error().code, Core::CoreErrorCode::NotFound);
#endif
}

#if defined(_WIN32) || defined(__linux__)

TEST(CatalogPackageWatcherTests, MoveTransfersNativeWatchOwnership)
{
    ScopedTestDirectory testDirectory{"tina_catalog_package_watcher_move"};
    ASSERT_TRUE(testDirectory.created());
    const auto manifest = testDirectory.path() / "manifest.tmnft";
    ASSERT_TRUE(Core::writeFile(toUtf8(manifest), InitialManifestBytes).has_value());

    auto original = CatalogPackageWatcher::Create(toUtf8(testDirectory.path()));
    ASSERT_TRUE(original.has_value()) << original.error().message;
    CatalogPackageWatcher moved = std::move(*original);

    auto movedFromProbe = original->poll();
    ASSERT_FALSE(movedFromProbe.has_value());
    EXPECT_EQ(movedFromProbe.error().code, AssetErrorCode::InvalidCatalogConfig);

    ASSERT_TRUE(Core::writeFile(toUtf8(manifest), ChangedManifestBytes).has_value());
    auto hint = waitForState(moved, CatalogPackageWatchState::Changed);
    ASSERT_TRUE(hint.has_value()) << hint.error().message;
}

#endif

} // namespace
} // namespace Tina::Asset
