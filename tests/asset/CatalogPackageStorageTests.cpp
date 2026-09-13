#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetErrors.hpp>
#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

namespace Tina::Asset {
namespace {

TEST(CatalogPackageStorageTests, SnapshotMoveAndCookedOwnerRetainMappedStorageWithoutPmrPayloadCopies)
{
    TestSupport::TrackingMemoryResource memory;
    const auto fixture = TestSupport::writeTextureMaterialPackage("tina_package_snapshot_pin");
    CookedAssetFile retained;
    {
        auto catalog = openCatalogPackage(TestSupport::toUtf8(fixture.root),
            {.manifest = {.catalog = {.memoryResource = &memory}}});
        ASSERT_TRUE(catalog) << catalog.error().message;
        CatalogSnapshot moved{std::move(*catalog)};
        CatalogSnapshot assigned;
        assigned = std::move(moved);
        EXPECT_FALSE(moved);
        const auto before = memory.allocationCalls();
        auto cooked = loadCookedAssetFromCatalog(assigned, fixture.textureId, {.memoryResource = &memory});
        ASSERT_TRUE(cooked) << cooked.error().message;
        EXPECT_EQ(memory.allocationCalls(), before);
        const auto path = AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Texture2D, fixture.textureId);
        auto view = assigned.packageReader().viewFile(path->view());
        ASSERT_TRUE(view);
        EXPECT_EQ(cooked->bytes().data(), view->bytes().data());
        retained = std::move(*cooked);
    }
    EXPECT_EQ(memory.outstandingAllocations(), 0U);
    EXPECT_TRUE(std::ranges::equal(retained.bytes(), fixture.textureBytes));
    retained = {};
    TestSupport::removePackage(fixture);
}

TEST(CatalogPackageStorageTests, AtomicReplacementDoesNotMixExistingManifestAndNewObjects)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto fixture = TestSupport::writeTextureMaterialPackage("tina_package_snapshot_replace");
    auto old = openCatalogPackage(TestSupport::toUtf8(fixture.root),
        {.manifest = {.catalog = {.memoryResource = &memory}}});
    ASSERT_TRUE(old);
    auto changed = fixture.textureBytes;
    // A legal package containing an intentionally invalid cooked object.
    changed.back() ^= std::byte{1};
    const auto path = AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Texture2D, fixture.textureId);
    ASSERT_TRUE(TestSupport::replacePackageEntry(fixture.root, path->view(), changed));
    auto oldAsset = loadCookedAssetFromCatalog(*old, fixture.textureId, {});
    ASSERT_TRUE(oldAsset);
    EXPECT_TRUE(std::ranges::equal(oldAsset->bytes(), fixture.textureBytes));
    auto onOpen = openCatalogPackage(TestSupport::toUtf8(fixture.root),
        {.manifest = {.catalog = {.memoryResource = &memory}},
         .objectValidation = Tina::Asset::CatalogObjectValidation::OnOpen});
    EXPECT_FALSE(onOpen);
    // OnDemand mounts without faulting in payloads, so the invalid object is
    // rejected by the first load instead of by open.
    auto candidate = openCatalogPackage(TestSupport::toUtf8(fixture.root),
        {.manifest = {.catalog = {.memoryResource = &memory}},
         .objectValidation = Tina::Asset::CatalogObjectValidation::OnDemand});
    ASSERT_TRUE(candidate) << candidate.error().message;
    auto changedAsset = loadCookedAssetFromCatalog(*candidate, fixture.textureId, {});
    EXPECT_FALSE(changedAsset);
    candidate = CatalogSnapshot{};
    old = CatalogSnapshot{};
    TestSupport::removePackage(fixture);
}

TEST(CatalogPackageStorageTests, MissingVirtualObjectNeverFallsBackToLooseFile)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto fixture = TestSupport::writeTextureMaterialPackage("tina_package_no_loose_fallback", false);
    const auto path = AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Material, fixture.materialId);
    TestSupport::writeBytes(fixture.root / Tina::TestSupport::pathFromUtf8Bytes(path->view()), fixture.materialBytes);
    auto catalog = openCatalogPackage(TestSupport::toUtf8(fixture.root),
        {.manifest = {.catalog = {.memoryResource = &memory}}, .objectValidation = Tina::Asset::CatalogObjectValidation::OnDemand});
    ASSERT_TRUE(catalog);
    auto missing = loadCookedAssetFromCatalog(*catalog, fixture.materialId, {});
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, Core::CoreErrorCode::NotFound);
    catalog = CatalogSnapshot{};
    TestSupport::removePackage(fixture);
}

TEST(CatalogPackageStorageTests, DefaultCatalogCeilingsDoNotImposeFormer1024EntryLimit)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto root = std::filesystem::temp_directory_path() / "tina_package_many_entries";
    std::vector<AssetFormat::CookedManifestWriteEntry> entries;
    for (Core::u32 i = 1; i <= 1200; ++i) {
        Core::AssetId::Bytes identity{};
        identity[0] = std::byte(i >> 8);
        identity[1] = std::byte(i & 255);
        entries.push_back({.assetId = *Core::AssetId::fromBytes(identity),
            .contentHash = TestSupport::defaultPayloadHash(),
            .assetKind = AssetFormat::AssetKind::Material, .assetTypeVersion = 1,
            .cookedFileBytes = 128});
    }
    auto manifest = AssetFormat::writeCookedManifestBytes({.entries = entries});
    ASSERT_TRUE(manifest) << manifest.error().message;
    ASSERT_TRUE(TestSupport::writePackage(root, *manifest));
    auto system = AssetSystem::Create({.storeCapacity = 8, .memoryResource = &memory});
    ASSERT_TRUE(system);
    auto bound = system->openAndBindCatalog(TestSupport::toUtf8(root), {.objectValidation = Tina::Asset::CatalogObjectValidation::OnDemand});
    ASSERT_TRUE(bound) << bound.error().message;
    EXPECT_TRUE(system->catalogFirstIdOfKind(AssetFormat::AssetKind::Material));
    auto explicitLimit = openCatalogPackage(TestSupport::toUtf8(root),
        {.manifest = {.catalog = {.maxEntries = 100, .memoryResource = &memory}}, .objectValidation = Tina::Asset::CatalogObjectValidation::OnDemand});
    EXPECT_FALSE(explicitLimit);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error);
}

TEST(CatalogPackageStorageTests, DefaultQueueExceeds4096AndPumpsWithoutPayloadAllocations)
{
    constexpr Core::u32 Count = 4100;
    TestSupport::TrackingMemoryResource memory;
    std::vector<TestSupport::CookedPackageAsset> assets;
    assets.reserve(Count);
    for (Core::u32 i = 1; i <= Count; ++i) {
        Core::AssetId::Bytes identity{};
        identity[0] = std::byte(i >> 8);
        identity[1] = std::byte(i & 255);
        const auto id = *Core::AssetId::fromBytes(identity);
        auto cooked = AssetFormat::writeCookedAssetBytes({
            .assetKind = AssetFormat::AssetKind::Material, .assetId = id,
            .payload = TestSupport::defaultPayload()});
        ASSERT_TRUE(cooked);
        assets.push_back({id, AssetFormat::AssetKind::Material, std::move(*cooked), {}});
    }
    const auto fixture = TestSupport::writeCookedPackage("tina_package_dynamic_queue", std::move(assets));
    {
        auto system = AssetSystem::Create({.storeCapacity = Count, .memoryResource = &memory});
        ASSERT_TRUE(system);
        ASSERT_TRUE(system->openAndBindCatalog(TestSupport::toUtf8(fixture.root)));
        auto handles = system->request({});
        ASSERT_TRUE(handles) << handles.error().message;
        ASSERT_EQ(system->pendingCount(), Count);
        const auto allocationsBefore = memory.allocationCalls();
        for (Core::u32 remaining = Count; remaining != 0; --remaining) {
            auto pumped = system->pump(1);
            ASSERT_TRUE(pumped) << pumped.error().message;
            EXPECT_EQ(pumped->becameReady, 1U);
            EXPECT_EQ(pumped->remaining, remaining - 1);
        }
        EXPECT_EQ(memory.allocationCalls(), allocationsBefore);
    }
    EXPECT_EQ(memory.outstandingAllocations(), 0U);
    TestSupport::removePackage(fixture);
}

} // namespace
} // namespace Tina::Asset
