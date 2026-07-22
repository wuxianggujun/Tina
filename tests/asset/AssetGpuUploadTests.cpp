#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuUpload.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/render/UploadTicket.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::makeCookedAsset;
using TestSupport::writeBytes;
using TestSupport::toUtf8;
using TestSupport::assetId;

[[nodiscard]] CookedAssetFile loadOneCooked(std::pmr::memory_resource& resource, Core::u8 seed,
                                            AssetFormat::AssetKind kind)
{
    const auto bytes = makeCookedAsset(seed, kind);
    const auto path =
        std::filesystem::temp_directory_path() / ("tina_gpu_upload_" + std::to_string(seed) + ".tasset");
    writeBytes(path, bytes);
    CookedAssetFileLoadConfig config{.memoryResource = &resource};
    auto loaded = loadCookedAssetFile(toUtf8(path), config);
    EXPECT_TRUE(loaded.has_value()) << (loaded ? "" : loaded.error().message);
    std::error_code errorCode;
    std::filesystem::remove(path, errorCode);
    if (!loaded)
    {
        return {};
    }
    return std::move(*loaded);
}

TEST(AssetGpuUploadTests, ReadyCpuToReadyGpuViaNullLedger)
{
    TrackingMemoryResource resource;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 4, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value());
    auto ledger =
        Render::NullUploadLedger::Create(Render::UploadLedgerConfig{.capacity = 4, .memoryResource = &resource});
    ASSERT_TRUE(ledger.has_value());

    auto handle = store->publish(loadOneCooked(resource, 1U, AssetFormat::AssetKind::Texture2D));
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(store->state(*handle), AssetLogicalState::ReadyCpu);
    EXPECT_FALSE(store->isGpuReady(*handle));

    AssetGpuUploadCoordinator coordinator(*store, *ledger, AssetGpuUploadConfig{.submitBudget = 8, .pollBudget = 8});
    ASSERT_TRUE(coordinator.track(*handle).has_value());

    auto stats = coordinator.pumpUploads();
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(stats->submitted, 1U);
    EXPECT_EQ(stats->becameGpuReady, 1U);
    EXPECT_EQ(store->state(*handle), AssetLogicalState::ReadyGpu);
    EXPECT_TRUE(store->isGpuReady(*handle));
    EXPECT_NE(store->tryGet(*handle), nullptr);
    EXPECT_EQ(ledger->liveCount(), 0U); // retired on Ready
}

TEST(AssetGpuUploadTests, CpuLeaseWorksDuringUploadQueued)
{
    TrackingMemoryResource resource;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 2, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value());
    auto handle = store->publish(loadOneCooked(resource, 2U, AssetFormat::AssetKind::Material));
    ASSERT_TRUE(handle.has_value());
    ASSERT_TRUE(store->beginUpload(*handle).has_value());
    EXPECT_EQ(store->state(*handle), AssetLogicalState::UploadQueued);

    auto lease = store->acquire(*handle);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ(lease->assetId(), assetId(2U));
    ASSERT_TRUE(store->completeGpu(*handle).has_value());
    EXPECT_EQ(store->state(*handle), AssetLogicalState::ReadyGpu);
}

} // namespace
} // namespace Tina::Asset
