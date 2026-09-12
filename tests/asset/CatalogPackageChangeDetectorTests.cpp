#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackageChangeDetector.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <memory_resource>
#include <string>

namespace Tina::Asset {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

Core::Status writeManifestPackage(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    const std::array entries{Core::PackageWriteEntry{CatalogManifestVirtualPath, bytes}};
    return Core::writePackageFile(toUtf8(path), entries);
}

void removeDirectory(const std::filesystem::path& path)
{
    std::error_code errorCode;
    std::filesystem::remove_all(path, errorCode);
}

[[nodiscard]] CatalogPackageChangeDetectorConfig
detectorConfig(Core::u64 maxManifestBytes = 64U)
{
    return CatalogPackageChangeDetectorConfig{
        .maxManifestBytes = maxManifestBytes,
    };
}

TEST(CatalogPackageChangeDetectorTests, ReportsChangedUntilCandidateIsAccepted)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto root = std::filesystem::temp_directory_path() / "tina_catalog_change_detector";
    removeDirectory(root);
    const auto manifest = root / "catalog.pck";
    constexpr std::array BaselineBytes{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    constexpr std::array ChangedBytes{std::byte{0x10}, std::byte{0x21}, std::byte{0x30}};
    ASSERT_TRUE(writeManifestPackage(manifest, BaselineBytes).has_value());

    auto baseline = captureCatalogPackageRevision(toUtf8(root), detectorConfig());
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    auto unchanged = pollCatalogPackageChange(toUtf8(root), *baseline, detectorConfig());
    ASSERT_TRUE(unchanged.has_value()) << unchanged.error().message;
    EXPECT_EQ(unchanged->state, CatalogPackageChangeState::Unchanged);
    EXPECT_EQ(unchanged->candidate, *baseline);

    ASSERT_TRUE(writeManifestPackage(manifest, ChangedBytes).has_value());
    auto firstChanged = pollCatalogPackageChange(toUtf8(root), *baseline, detectorConfig());
    auto retriedChanged = pollCatalogPackageChange(toUtf8(root), *baseline, detectorConfig());
    ASSERT_TRUE(firstChanged.has_value()) << firstChanged.error().message;
    ASSERT_TRUE(retriedChanged.has_value()) << retriedChanged.error().message;
    EXPECT_EQ(firstChanged->state, CatalogPackageChangeState::Changed);
    EXPECT_EQ(retriedChanged->state, CatalogPackageChangeState::Changed);
    EXPECT_EQ(retriedChanged->candidate, firstChanged->candidate);

    auto accepted = pollCatalogPackageChange(toUtf8(root), firstChanged->candidate,
                                             detectorConfig());
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    EXPECT_EQ(accepted->state, CatalogPackageChangeState::Unchanged);

    removeDirectory(root);
}

TEST(CatalogPackageChangeDetectorTests, SameManifestBytesRemainUnchangedAfterRewrite)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto root = std::filesystem::temp_directory_path() / "tina_catalog_change_detector_rewrite";
    removeDirectory(root);
    const auto manifest = root / "catalog.pck";
    constexpr std::array ManifestBytes{std::byte{0x41}, std::byte{0x42}, std::byte{0x43}};
    ASSERT_TRUE(writeManifestPackage(manifest, ManifestBytes).has_value());
    auto baseline = captureCatalogPackageRevision(toUtf8(root), detectorConfig());
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;

    ASSERT_TRUE(writeManifestPackage(manifest, ManifestBytes).has_value());
    auto probe = pollCatalogPackageChange(toUtf8(root), *baseline, detectorConfig());
    ASSERT_TRUE(probe.has_value()) << probe.error().message;
    EXPECT_EQ(probe->state, CatalogPackageChangeState::Unchanged);
    EXPECT_EQ(probe->candidate, *baseline);

    removeDirectory(root);
}

TEST(CatalogPackageChangeDetectorTests, RejectsUnsafePathsAndInvalidBaseline)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto invalidRoot = captureCatalogPackageRevision("", detectorConfig());
    ASSERT_FALSE(invalidRoot.has_value());
    EXPECT_EQ(invalidRoot.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto unsafeRelative = detectorConfig();
    unsafeRelative.packageRelativePath = "../catalog.pck";
    auto escaped = captureCatalogPackageRevision("catalog", unsafeRelative);
    ASSERT_FALSE(escaped.has_value());
    EXPECT_EQ(escaped.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto invalidBaseline = pollCatalogPackageChange("catalog", CatalogPackageRevision{},
                                                    detectorConfig());
    ASSERT_FALSE(invalidBaseline.has_value());
    EXPECT_EQ(invalidBaseline.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(CatalogPackageChangeDetectorTests, PreservesStructuredReadAndConfigFailures)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto root = std::filesystem::temp_directory_path() / "tina_catalog_change_detector_failures";
    removeDirectory(root);

    auto missing = captureCatalogPackageRevision(toUtf8(root), detectorConfig());
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, Core::CoreErrorCode::NotFound);

    const auto manifest = root / "catalog.pck";
    constexpr std::array ManifestBytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    ASSERT_TRUE(writeManifestPackage(manifest, ManifestBytes).has_value());
    auto tooSmall = captureCatalogPackageRevision(toUtf8(root), detectorConfig(2U));
    ASSERT_FALSE(tooSmall.has_value());
    EXPECT_EQ(tooSmall.error().code, Core::CoreErrorCode::CapacityExceeded);

    auto invalidConfig = detectorConfig();
    invalidConfig.maxManifestBytes = 0;
    auto configFailure = captureCatalogPackageRevision(toUtf8(root), invalidConfig);
    ASSERT_FALSE(configFailure.has_value());
    EXPECT_EQ(configFailure.error().code, AssetErrorCode::InvalidCatalogConfig);

    removeDirectory(root);
}

} // namespace
} // namespace Tina::Asset
