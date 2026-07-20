#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>

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
        std::filesystem::temp_directory_path() / ("tina_asset_store_" + std::to_string(seed) + ".tasset");
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

TEST(AssetStoreTests, PublishAcquireTryGetAndUnloadWithoutLeases)
{
    TrackingMemoryResource resource;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 4, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value()) << store.error().message;

    auto published = store->publish(loadOneCooked(resource, 1U, AssetFormat::AssetKind::Texture2D));
    ASSERT_TRUE(published.has_value()) << published.error().message;
    EXPECT_TRUE(*published);
    EXPECT_EQ(store->activeCount(), 1U);

    const auto* weak = store->tryGet(*published);
    ASSERT_NE(weak, nullptr);
    EXPECT_EQ(weak->header().assetId, assetId(1U));

    auto lease = store->acquire(*published);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;
    EXPECT_EQ(store->leaseCount(*published), 1U);
    ASSERT_NE(lease->get(), nullptr);
    EXPECT_EQ(lease->assetId(), assetId(1U));

    lease = AssetLease{};
    EXPECT_EQ(store->leaseCount(*published), 0U);
    EXPECT_NE(store->tryGet(*published), nullptr);

    ASSERT_TRUE(store->unload(*published).has_value());
    EXPECT_EQ(store->tryGet(*published), nullptr);
    EXPECT_EQ(store->state(*published), AssetLogicalState::Unloaded);
    EXPECT_EQ(store->activeCount(), 0U);
}

TEST(AssetStoreTests, UnloadDefersUntilLastLeaseReleased)
{
    TrackingMemoryResource resource;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 2, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value());

    auto handle = store->publish(loadOneCooked(resource, 2U, AssetFormat::AssetKind::Material));
    ASSERT_TRUE(handle.has_value());

    auto leaseA = store->acquire(*handle);
    auto leaseB = store->acquire(*handle);
    ASSERT_TRUE(leaseA.has_value());
    ASSERT_TRUE(leaseB.has_value());
    EXPECT_EQ(store->leaseCount(*handle), 2U);

    ASSERT_TRUE(store->unload(*handle).has_value());
    EXPECT_EQ(store->state(*handle), AssetLogicalState::UnloadPending);
    EXPECT_NE(store->tryGet(*handle), nullptr);
    auto rejected = store->acquire(*handle);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, AssetErrorCode::AssetNotReady);

    leaseA = AssetLease{};
    EXPECT_EQ(store->state(*handle), AssetLogicalState::UnloadPending);
    EXPECT_NE(store->tryGet(*handle), nullptr);

    leaseB = AssetLease{};
    EXPECT_EQ(store->tryGet(*handle), nullptr);
    EXPECT_EQ(store->state(*handle), AssetLogicalState::Unloaded);
    EXPECT_EQ(store->activeCount(), 0U);
}

TEST(AssetStoreTests, StaleHandleAfterUnloadAndRepublish)
{
    TrackingMemoryResource resource;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 1, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value());

    auto first = store->publish(loadOneCooked(resource, 3U, AssetFormat::AssetKind::Sprite));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(store->unload(*first).has_value());

    auto second = store->publish(loadOneCooked(resource, 4U, AssetFormat::AssetKind::Font));
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    EXPECT_EQ(store->tryGet(*first), nullptr);
    EXPECT_NE(store->tryGet(*second), nullptr);
    EXPECT_EQ(store->tryGet(*second)->header().assetId, assetId(4U));
}

TEST(AssetStoreTests, CapacityExceededOnPublish)
{
    TrackingMemoryResource resource;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 1, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value());
    ASSERT_TRUE(store->publish(loadOneCooked(resource, 5U, AssetFormat::AssetKind::Texture2D)).has_value());
    auto overflow = store->publish(loadOneCooked(resource, 6U, AssetFormat::AssetKind::Material));
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, AssetErrorCode::CatalogCapacityExceeded);
}

TEST(AssetStoreTests, QueuedLoadingCompleteAndFail)
{
    TrackingMemoryResource resource;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 2, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value());

    auto handle = store->beginQueued(assetId(7U), AssetFormat::AssetKind::Texture2D);
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(store->state(*handle), AssetLogicalState::Queued);
    EXPECT_EQ(store->tryGet(*handle), nullptr);
    auto notReady = store->acquire(*handle);
    ASSERT_FALSE(notReady.has_value());
    EXPECT_EQ(notReady.error().code, AssetErrorCode::AssetNotReady);

    ASSERT_TRUE(store->markLoading(*handle).has_value());
    EXPECT_EQ(store->state(*handle), AssetLogicalState::Loading);

    ASSERT_TRUE(store->complete(*handle, loadOneCooked(resource, 7U, AssetFormat::AssetKind::Texture2D)).has_value());
    EXPECT_EQ(store->state(*handle), AssetLogicalState::Ready);
    EXPECT_NE(store->tryGet(*handle), nullptr);

    auto failed = store->beginQueued(assetId(8U), AssetFormat::AssetKind::Material);
    ASSERT_TRUE(failed.has_value());
    ASSERT_TRUE(store->markLoading(*failed).has_value());
    ASSERT_TRUE(store->fail(*failed).has_value());
    EXPECT_EQ(store->state(*failed), AssetLogicalState::Failed);
    auto acquireFailed = store->acquire(*failed);
    ASSERT_FALSE(acquireFailed.has_value());
    EXPECT_EQ(acquireFailed.error().code, AssetErrorCode::AssetFailed);
}

} // namespace
} // namespace Tina::Asset
