#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogChangePlan.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/hash/ContentHash.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

using TestSupport::assetId;

struct DependencySpec final {
    Core::u8 targetSeed = 0;
    AssetFormat::AssetKind expectedKind = AssetFormat::AssetKind::Invalid;
    AssetFormat::DependencyFlags flags = AssetFormat::DependencyFlags::Required;
};

struct EntrySpec final {
    Core::u8 idSeed = 0;
    Core::u8 hashSeed = 0;
    AssetFormat::AssetKind kind = AssetFormat::AssetKind::Invalid;
    Core::u16 typeVersion = 1;
    Core::u64 cookedFileBytes = 64;
    std::vector<DependencySpec> dependencies{};
};

[[nodiscard]] Core::ContentHash contentHash(Core::u8 seed)
{
    Core::ContentHash::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::ContentHash::fromBytes(bytes);
}

[[nodiscard]] EntrySpec entry(Core::u8 idSeed, Core::u8 hashSeed, AssetFormat::AssetKind kind,
                              std::vector<DependencySpec> dependencies = {})
{
    return EntrySpec{
        .idSeed = idSeed,
        .hashSeed = hashSeed,
        .kind = kind,
        .typeVersion = 1,
        .cookedFileBytes = static_cast<Core::u64>(64U + idSeed),
        .dependencies = std::move(dependencies),
    };
}

[[nodiscard]] Core::Result<CatalogSnapshot> makeCatalog(const std::vector<EntrySpec>& specs,
                                                        std::pmr::memory_resource& memory)
{
    std::vector<std::vector<AssetFormat::CookedAssetWriteDependency>> ownedDependencies;
    ownedDependencies.reserve(specs.size());
    Core::u32 dependencyCount = 0;
    Core::u32 maxDependenciesPerAsset = 0;
    for (const auto& spec : specs)
    {
        auto& dependencies = ownedDependencies.emplace_back();
        dependencies.reserve(spec.dependencies.size());
        for (const auto& dependency : spec.dependencies)
        {
            dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                .assetId = assetId(dependency.targetSeed),
                .expectedKind = dependency.expectedKind,
                .flags = dependency.flags,
            });
        }
        dependencyCount += static_cast<Core::u32>(dependencies.size());
        maxDependenciesPerAsset =
            std::max(maxDependenciesPerAsset, static_cast<Core::u32>(dependencies.size()));
    }

    std::vector<AssetFormat::CookedManifestWriteEntry> entries;
    entries.reserve(specs.size());
    for (Core::usize index = 0; index < specs.size(); ++index)
    {
        const auto& spec = specs[index];
        entries.push_back(AssetFormat::CookedManifestWriteEntry{
            .assetId = assetId(spec.idSeed),
            .contentHash = contentHash(spec.hashSeed),
            .assetKind = spec.kind,
            .assetTypeVersion = spec.typeVersion,
            .cookedFileBytes = spec.cookedFileBytes,
            .dependencies = ownedDependencies[index],
        });
    }

    auto bytes = AssetFormat::writeCookedManifestBytes(AssetFormat::CookedManifestWriteDesc{.entries = entries});
    if (!bytes)
    {
        return Core::failure(bytes.error());
    }
    auto view = AssetFormat::parseCookedManifestView(*bytes);
    if (!view)
    {
        return Core::failure(view.error());
    }
    return CatalogSnapshot::Create(
        *view,
        CatalogConfig{
            .maxEntries = std::max<Core::u32>(1U, static_cast<Core::u32>(entries.size())),
            .maxDependencies = std::max<Core::u32>(1U, dependencyCount),
            .maxDependenciesPerAsset = std::max<Core::u32>(1U, maxDependenciesPerAsset),
            .memoryResource = &memory,
        });
}

void expectChange(const CatalogChangePlan& plan, Core::usize index, Core::u8 idSeed, CatalogChangeKind kind)
{
    ASSERT_LT(index, plan.changes.size());
    EXPECT_EQ(plan.changes[index].assetId, assetId(idSeed));
    EXPECT_EQ(plan.changes[index].kind, kind);
}

TEST(CatalogChangePlanTests, ClassifiesAddedRemovedAndEveryModifiedField)
{
    enum class Mutation : Core::u8 {
        ContentHash,
        AssetKind,
        TypeVersion,
        CookedFileBytes,
        DependencyAssetId,
        DependencyExpectedKind,
        DependencyFlags,
    };
    constexpr std::array Mutations{
        Mutation::ContentHash,          Mutation::AssetKind,       Mutation::TypeVersion,
        Mutation::CookedFileBytes,      Mutation::DependencyAssetId,
        Mutation::DependencyExpectedKind, Mutation::DependencyFlags,
    };

    for (const auto mutation : Mutations)
    {
        SCOPED_TRACE(static_cast<unsigned int>(mutation));
        std::vector oldSpecs{
            entry(1U, 11U, AssetFormat::AssetKind::Texture2D),
            entry(2U, 12U, AssetFormat::AssetKind::Material,
                  {{.targetSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}),
            entry(3U, 13U, AssetFormat::AssetKind::Texture2D),
        };
        auto newSpecs = oldSpecs;
        switch (mutation)
        {
        case Mutation::ContentHash:
            newSpecs[1].hashSeed = 42U;
            break;
        case Mutation::AssetKind:
            newSpecs[1].kind = AssetFormat::AssetKind::Sprite;
            break;
        case Mutation::TypeVersion:
            newSpecs[1].typeVersion = 2U;
            break;
        case Mutation::CookedFileBytes:
            ++newSpecs[1].cookedFileBytes;
            break;
        case Mutation::DependencyAssetId:
            newSpecs[1].dependencies[0].targetSeed = 3U;
            break;
        case Mutation::DependencyExpectedKind:
            newSpecs[0].kind = AssetFormat::AssetKind::StaticMesh;
            newSpecs[1].dependencies[0].expectedKind = AssetFormat::AssetKind::StaticMesh;
            break;
        case Mutation::DependencyFlags:
            newSpecs[1].dependencies[0].flags =
                AssetFormat::DependencyFlags::Required | AssetFormat::DependencyFlags::Deferred;
            break;
        }

        std::pmr::unsynchronized_pool_resource memory;
        auto oldCatalogResult = makeCatalog(oldSpecs, memory);
        auto newCatalogResult = makeCatalog(newSpecs, memory);
        ASSERT_TRUE(oldCatalogResult.has_value()) << oldCatalogResult.error().message;
        ASSERT_TRUE(newCatalogResult.has_value()) << newCatalogResult.error().message;
        auto oldCatalog = std::move(*oldCatalogResult);
        auto newCatalog = std::move(*newCatalogResult);

        auto plan = planCatalogChanges(oldCatalog, newCatalog,
                                       CatalogChangePlanConfig{.memoryResource = &memory, .maxChanges = 4U});
        ASSERT_TRUE(plan.has_value()) << plan.error().message;
        const auto expectedChanges = mutation == Mutation::DependencyExpectedKind ? 2U : 1U;
        EXPECT_EQ(plan->changes.size(), expectedChanges);
        EXPECT_EQ(plan->modifiedCount, expectedChanges);
        expectChange(*plan, expectedChanges - 1U, 2U, CatalogChangeKind::Modified);
    }

    std::pmr::unsynchronized_pool_resource memory;
    auto oldCatalogResult = makeCatalog(
        {entry(1U, 11U, AssetFormat::AssetKind::Texture2D),
         entry(2U, 12U, AssetFormat::AssetKind::Material)},
        memory);
    auto newCatalogResult = makeCatalog(
        {entry(2U, 12U, AssetFormat::AssetKind::Material),
         entry(3U, 13U, AssetFormat::AssetKind::StaticMesh)},
        memory);
    ASSERT_TRUE(oldCatalogResult.has_value()) << oldCatalogResult.error().message;
    ASSERT_TRUE(newCatalogResult.has_value()) << newCatalogResult.error().message;
    auto oldCatalog = std::move(*oldCatalogResult);
    auto newCatalog = std::move(*newCatalogResult);

    auto plan = planCatalogChanges(oldCatalog, newCatalog,
                                   CatalogChangePlanConfig{.memoryResource = &memory, .maxChanges = 2U});
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    ASSERT_EQ(plan->changes.size(), 2U);
    EXPECT_EQ(plan->addedCount, 1U);
    EXPECT_EQ(plan->removedCount, 1U);
    expectChange(*plan, 0U, 1U, CatalogChangeKind::Removed);
    expectChange(*plan, 1U, 3U, CatalogChangeKind::Added);
}

TEST(CatalogChangePlanTests, PropagatesAffectedTransitivelyWithoutOverwritingDirectChanges)
{
    const std::vector oldSpecs{
        entry(1U, 11U, AssetFormat::AssetKind::Texture2D),
        entry(2U, 12U, AssetFormat::AssetKind::Sprite,
              {{.targetSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}),
        entry(3U, 13U, AssetFormat::AssetKind::Material,
              {{.targetSeed = 2U, .expectedKind = AssetFormat::AssetKind::Sprite}}),
        entry(4U, 14U, AssetFormat::AssetKind::Texture2D),
        entry(5U, 15U, AssetFormat::AssetKind::Material,
              {{.targetSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}),
    };
    auto newSpecs = oldSpecs;
    newSpecs[0].hashSeed = 31U;
    newSpecs[4].hashSeed = 35U;

    std::pmr::unsynchronized_pool_resource memory;
    auto oldCatalogResult = makeCatalog(oldSpecs, memory);
    auto newCatalogResult = makeCatalog(newSpecs, memory);
    ASSERT_TRUE(oldCatalogResult.has_value()) << oldCatalogResult.error().message;
    ASSERT_TRUE(newCatalogResult.has_value()) << newCatalogResult.error().message;
    auto oldCatalog = std::move(*oldCatalogResult);
    auto newCatalog = std::move(*newCatalogResult);

    auto plan = planCatalogChanges(oldCatalog, newCatalog,
                                   CatalogChangePlanConfig{.memoryResource = &memory, .maxChanges = 5U});
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    ASSERT_EQ(plan->changes.size(), 4U);
    EXPECT_EQ(plan->modifiedCount, 2U);
    EXPECT_EQ(plan->affectedCount, 2U);
    expectChange(*plan, 0U, 1U, CatalogChangeKind::Modified);
    expectChange(*plan, 1U, 2U, CatalogChangeKind::Affected);
    expectChange(*plan, 2U, 3U, CatalogChangeKind::Affected);
    expectChange(*plan, 3U, 5U, CatalogChangeKind::Modified);
}

TEST(CatalogChangePlanTests, EnforcesCapacityAndUsesCallerMemoryResource)
{
    std::pmr::unsynchronized_pool_resource catalogMemory;
    auto oldCatalogResult = makeCatalog({entry(1U, 11U, AssetFormat::AssetKind::Texture2D)}, catalogMemory);
    auto newCatalogResult = makeCatalog({entry(2U, 12U, AssetFormat::AssetKind::Texture2D)}, catalogMemory);
    ASSERT_TRUE(oldCatalogResult.has_value()) << oldCatalogResult.error().message;
    ASSERT_TRUE(newCatalogResult.has_value()) << newCatalogResult.error().message;
    auto oldCatalog = std::move(*oldCatalogResult);
    auto newCatalog = std::move(*newCatalogResult);

    TestSupport::TrackingMemoryResource planMemory;
    auto rejected = planCatalogChanges(oldCatalog, newCatalog,
                                       CatalogChangePlanConfig{.memoryResource = &planMemory, .maxChanges = 1U});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, AssetErrorCode::CatalogCapacityExceeded);
    EXPECT_EQ(planMemory.outstandingAllocations(), 0U);

    auto plan = planCatalogChanges(oldCatalog, newCatalog,
                                   CatalogChangePlanConfig{.memoryResource = &planMemory, .maxChanges = 2U});
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    EXPECT_EQ(plan->changes.get_allocator().resource(), &planMemory);
    EXPECT_GT(planMemory.outstandingAllocations(), 0U);
}

TEST(CatalogChangePlanTests, RejectsInvalidInputsAndMapsAllocationFailure)
{
    CatalogSnapshot invalidCatalog;
    std::pmr::unsynchronized_pool_resource memory;
    auto invalid = planCatalogChanges(
        invalidCatalog, invalidCatalog, CatalogChangePlanConfig{.memoryResource = &memory, .maxChanges = 1U});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto oldCatalogResult = makeCatalog({entry(1U, 11U, AssetFormat::AssetKind::Texture2D)}, memory);
    auto newCatalogResult = makeCatalog({entry(2U, 12U, AssetFormat::AssetKind::Texture2D)}, memory);
    ASSERT_TRUE(oldCatalogResult.has_value()) << oldCatalogResult.error().message;
    ASSERT_TRUE(newCatalogResult.has_value()) << newCatalogResult.error().message;
    auto oldCatalog = std::move(*oldCatalogResult);
    auto newCatalog = std::move(*newCatalogResult);

    auto nullResource = planCatalogChanges(oldCatalog, newCatalog,
                                           CatalogChangePlanConfig{.memoryResource = nullptr, .maxChanges = 2U});
    ASSERT_FALSE(nullResource.has_value());
    EXPECT_EQ(nullResource.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto allocationFailure = planCatalogChanges(
        oldCatalog, newCatalog,
        CatalogChangePlanConfig{.memoryResource = std::pmr::null_memory_resource(), .maxChanges = 2U});
    ASSERT_FALSE(allocationFailure.has_value());
    EXPECT_EQ(allocationFailure.error().code, AssetErrorCode::AllocationFailed);
}

TEST(CatalogChangePlanTests, ZeroCapacityAcceptsOnlyAnEmptyPlan)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::vector specs{entry(1U, 11U, AssetFormat::AssetKind::Texture2D)};
    auto oldCatalogResult = makeCatalog(specs, memory);
    auto unchangedCatalogResult = makeCatalog(specs, memory);
    ASSERT_TRUE(oldCatalogResult.has_value()) << oldCatalogResult.error().message;
    ASSERT_TRUE(unchangedCatalogResult.has_value()) << unchangedCatalogResult.error().message;
    auto oldCatalog = std::move(*oldCatalogResult);
    auto unchangedCatalog = std::move(*unchangedCatalogResult);

    auto emptyPlan = planCatalogChanges(oldCatalog, unchangedCatalog,
                                        CatalogChangePlanConfig{.memoryResource = &memory, .maxChanges = 0U});
    ASSERT_TRUE(emptyPlan.has_value()) << emptyPlan.error().message;
    EXPECT_TRUE(emptyPlan->changes.empty());
    EXPECT_EQ(emptyPlan->addedCount + emptyPlan->removedCount + emptyPlan->modifiedCount +
                  emptyPlan->affectedCount,
              0U);
}

} // namespace
} // namespace Tina::Asset
