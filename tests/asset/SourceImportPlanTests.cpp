#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/SourceImportPlan.hpp>
#include <tina/core/hash/ContentHash.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

struct SourceSpec final {
    std::string path{};
    Core::u8 hashSeed = 0;
    Core::u64 fileBytes = 64;
    AssetFormat::SourceImportReadExtent readExtent = AssetFormat::SourceImportReadExtent::WholeFile;
};

struct InputSpec final {
    Core::u32 sourceIndex = 0;
    bool primary = false;
};

struct OutputSpec final {
    Core::u8 assetSeed = 0;
    AssetFormat::AssetKind kind = AssetFormat::AssetKind::Invalid;
};

struct UnitSpec final {
    Core::u8 unitSeed = 0;
    Core::u32 importerKind = 1;
    Core::u32 importerVersion = 1;
    Core::u8 settingsSeed = 0;
    std::vector<InputSpec> inputs{};
    std::vector<OutputSpec> outputs{};
};

[[nodiscard]] Core::ContentHash contentHash(Core::u8 seed)
{
    Core::ContentHash::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::ContentHash::fromBytes(bytes);
}

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] AssetFormat::SourceImportUnitId unitId(Core::u8 seed)
{
    AssetFormat::SourceImportUnitId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x3CU);
    return *AssetFormat::SourceImportUnitId::fromBytes(bytes);
}

[[nodiscard]] AssetFormat::SourceImportManifestRevision revision(Core::u8 seed)
{
    return AssetFormat::SourceImportManifestRevision{
        .manifestDigest = contentHash(seed),
        .manifestBytes = static_cast<Core::u64>(128U + seed),
    };
}

struct MetadataBytes final {
    std::vector<std::byte> bytes{};

    [[nodiscard]] Core::Result<AssetFormat::SourceImportMetadataView> view() const
    {
        return AssetFormat::parseSourceImportMetadataView(bytes);
    }
};

[[nodiscard]] MetadataBytes makeMetadata(
    const std::vector<SourceSpec>& sourceSpecs,
    const std::vector<UnitSpec>& unitSpecs,
    AssetFormat::TargetPlatform platform = AssetFormat::TargetPlatform::WindowsX64,
    AssetFormat::SourceImportManifestRevision manifestRevision = revision(90U))
{
    std::vector<AssetFormat::SourceImportMetadataWriteSource> sources;
    sources.reserve(sourceSpecs.size());
    for (const auto& source : sourceSpecs)
    {
        sources.push_back(AssetFormat::SourceImportMetadataWriteSource{
            .path = source.path,
            .contentHash = contentHash(source.hashSeed),
            .fileBytes = source.fileBytes,
            .readExtent = source.readExtent,
        });
    }

    std::vector<std::vector<AssetFormat::SourceImportMetadataWriteInput>> ownedInputs;
    std::vector<std::vector<AssetFormat::SourceImportMetadataWriteOutput>> ownedOutputs;
    ownedInputs.reserve(unitSpecs.size());
    ownedOutputs.reserve(unitSpecs.size());
    for (const auto& unit : unitSpecs)
    {
        auto& inputs = ownedInputs.emplace_back();
        inputs.reserve(unit.inputs.size());
        for (const auto& input : unit.inputs)
        {
            inputs.push_back(AssetFormat::SourceImportMetadataWriteInput{
                .sourceIndex = input.sourceIndex,
                .flags = input.primary ? AssetFormat::SourceImportInputFlags::Primary
                                       : AssetFormat::SourceImportInputFlags::None,
            });
        }
        auto& outputs = ownedOutputs.emplace_back();
        outputs.reserve(unit.outputs.size());
        for (const auto& output : unit.outputs)
        {
            outputs.push_back(AssetFormat::SourceImportMetadataWriteOutput{
                .assetId = assetId(output.assetSeed),
                .assetKind = output.kind,
            });
        }
    }

    std::vector<AssetFormat::SourceImportMetadataWriteUnit> units;
    units.reserve(unitSpecs.size());
    for (Core::usize index = 0; index < unitSpecs.size(); ++index)
    {
        const auto& unit = unitSpecs[index];
        units.push_back(AssetFormat::SourceImportMetadataWriteUnit{
            .unitId = unitId(unit.unitSeed),
            .importerKind = unit.importerKind,
            .importerVersion = unit.importerVersion,
            .settingsHash = contentHash(unit.settingsSeed),
            .inputs = ownedInputs[index],
            .outputs = ownedOutputs[index],
        });
    }

    auto result = AssetFormat::writeSourceImportMetadataBytes(
        AssetFormat::SourceImportMetadataWriteDesc{
            .targetPlatform = platform,
            .manifestRevision = manifestRevision,
            .sources = sources,
            .units = units,
        });
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return MetadataBytes{.bytes = result ? std::move(*result) : std::vector<std::byte>{}};
}

[[nodiscard]] std::vector<SourceSpec> baseSources()
{
    return {
        SourceSpec{.path = "audio/click.wav", .hashSeed = 11U, .fileBytes = 96U},
        SourceSpec{.path = "models/ship.gltf", .hashSeed = 12U, .fileBytes = 128U},
    };
}

[[nodiscard]] std::vector<UnitSpec> baseUnits()
{
    return {
        UnitSpec{
            .unitSeed = 1U,
            .importerKind = 1U,
            .importerVersion = 3U,
            .settingsSeed = 21U,
            .inputs = {{.sourceIndex = 0U, .primary = true}},
            .outputs = {{.assetSeed = 1U, .kind = AssetFormat::AssetKind::AudioClip}},
        },
        UnitSpec{
            .unitSeed = 2U,
            .importerKind = 2U,
            .importerVersion = 5U,
            .settingsSeed = 22U,
            .inputs = {{.sourceIndex = 1U, .primary = true}},
            .outputs = {{.assetSeed = 2U, .kind = AssetFormat::AssetKind::StaticMesh},
                        {.assetSeed = 3U, .kind = AssetFormat::AssetKind::Material}},
        },
    };
}

void expectChange(const SourceImportPlan& plan, Core::usize index, Core::u8 seed,
                  SourceImportChangeKind kind)
{
    ASSERT_LT(index, plan.changes.size());
    EXPECT_EQ(plan.changes[index].unitId, unitId(seed));
    EXPECT_EQ(plan.changes[index].kind, kind);
}

TEST(SourceImportPlanTests, EmptyForIdenticalGraphsAndValidatesCatalogBinding)
{
    const auto metadata = makeMetadata(baseSources(), baseUnits());
    auto baseline = metadata.view();
    auto candidate = metadata.view();
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    ASSERT_TRUE(candidate.has_value()) << candidate.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto plan = planSourceImports(*baseline, *candidate,
                                  SourceImportPlanConfig{.memoryResource = &memory, .maxChanges = 0U});
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    EXPECT_TRUE(plan->changes.empty());
    EXPECT_TRUE(validateSourceImportCatalogBinding(*baseline, revision(90U)).has_value());

    const auto mismatch = validateSourceImportCatalogBinding(*baseline, revision(91U));
    ASSERT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error().code, AssetErrorCode::SourceImportCatalogMismatch);
}

TEST(SourceImportPlanTests, ClassifiesAddedRemovedAndReimportInUnitIdOrder)
{
    auto oldUnits = baseUnits();
    auto newUnits = oldUnits;
    newUnits.erase(newUnits.begin());
    newUnits[0].importerVersion += 1U;
    newUnits.push_back(UnitSpec{
        .unitSeed = 3U,
        .importerKind = 1U,
        .importerVersion = 1U,
        .settingsSeed = 23U,
        .inputs = {{.sourceIndex = 0U, .primary = true}},
        .outputs = {{.assetSeed = 4U, .kind = AssetFormat::AssetKind::Texture2D}},
    });
    const auto oldBytes = makeMetadata(baseSources(), oldUnits);
    const auto newBytes = makeMetadata(baseSources(), newUnits);
    auto baseline = oldBytes.view();
    auto candidate = newBytes.view();
    ASSERT_TRUE(baseline.has_value());
    ASSERT_TRUE(candidate.has_value());

    std::pmr::unsynchronized_pool_resource memory;
    auto plan = planSourceImports(*baseline, *candidate,
                                  SourceImportPlanConfig{.memoryResource = &memory, .maxChanges = 3U});
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    ASSERT_EQ(plan->changes.size(), 3U);
    EXPECT_EQ(plan->removedCount, 1U);
    EXPECT_EQ(plan->reimportCount, 1U);
    EXPECT_EQ(plan->addedCount, 1U);
    expectChange(*plan, 0U, 1U, SourceImportChangeKind::Removed);
    expectChange(*plan, 1U, 2U, SourceImportChangeKind::Reimport);
    expectChange(*plan, 2U, 3U, SourceImportChangeKind::Added);
}

TEST(SourceImportPlanTests, SharedSourceContentChangeReimportsEveryConsumer)
{
    std::vector sources{SourceSpec{.path = "shared/source.bin", .hashSeed = 31U}};
    const std::vector units{
        UnitSpec{.unitSeed = 1U,
                 .settingsSeed = 41U,
                 .inputs = {{.sourceIndex = 0U, .primary = true}},
                 .outputs = {{.assetSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D}}},
        UnitSpec{.unitSeed = 2U,
                 .settingsSeed = 42U,
                 .inputs = {{.sourceIndex = 0U, .primary = true}},
                 .outputs = {{.assetSeed = 2U, .kind = AssetFormat::AssetKind::Material}}},
    };
    const auto oldBytes = makeMetadata(sources, units);
    sources[0].hashSeed = 32U;
    const auto newBytes = makeMetadata(sources, units);
    auto baseline = oldBytes.view();
    auto candidate = newBytes.view();
    ASSERT_TRUE(baseline.has_value());
    ASSERT_TRUE(candidate.has_value());

    std::pmr::unsynchronized_pool_resource memory;
    auto plan = planSourceImports(*baseline, *candidate,
                                  SourceImportPlanConfig{.memoryResource = &memory, .maxChanges = 2U});
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    ASSERT_EQ(plan->changes.size(), 2U);
    EXPECT_EQ(plan->reimportCount, 2U);
    expectChange(*plan, 0U, 1U, SourceImportChangeKind::Reimport);
    expectChange(*plan, 1U, 2U, SourceImportChangeKind::Reimport);
}

TEST(SourceImportPlanTests, DetectsSettingsInputsOutputsAndTargetChanges)
{
    enum class Mutation : Core::u8 {
        ImporterKind,
        ImporterVersion,
        Settings,
        InputMembership,
        ReadExtent,
        OutputKind,
        TargetPlatform,
    };
    constexpr std::array Mutations{Mutation::ImporterKind, Mutation::ImporterVersion,
                                   Mutation::Settings, Mutation::InputMembership, Mutation::ReadExtent,
                                   Mutation::OutputKind, Mutation::TargetPlatform};

    for (const auto mutation : Mutations)
    {
        SCOPED_TRACE(static_cast<unsigned int>(mutation));
        auto sources = baseSources();
        auto units = baseUnits();
        auto platform = AssetFormat::TargetPlatform::WindowsX64;
        switch (mutation)
        {
        case Mutation::ImporterKind:
            ++units[0].importerKind;
            break;
        case Mutation::ImporterVersion:
            ++units[0].importerVersion;
            break;
        case Mutation::Settings:
            ++units[0].settingsSeed;
            break;
        case Mutation::InputMembership:
            units[0].inputs.push_back(InputSpec{.sourceIndex = 1U});
            break;
        case Mutation::ReadExtent:
            sources[0].readExtent = AssetFormat::SourceImportReadExtent::Prefix;
            break;
        case Mutation::OutputKind:
            units[0].outputs[0].kind = AssetFormat::AssetKind::Texture2D;
            break;
        case Mutation::TargetPlatform:
            platform = AssetFormat::TargetPlatform::LinuxX64;
            break;
        }

        const auto oldBytes = makeMetadata(baseSources(), baseUnits());
        const auto newBytes = makeMetadata(sources, units, platform);
        auto baseline = oldBytes.view();
        auto candidate = newBytes.view();
        ASSERT_TRUE(baseline.has_value());
        ASSERT_TRUE(candidate.has_value());

        std::pmr::unsynchronized_pool_resource memory;
        auto plan = planSourceImports(*baseline, *candidate,
                                      SourceImportPlanConfig{.memoryResource = &memory, .maxChanges = 2U});
        ASSERT_TRUE(plan.has_value()) << plan.error().message;
        const Core::u32 expected = mutation == Mutation::TargetPlatform ? 2U : 1U;
        EXPECT_EQ(plan->reimportCount, expected);
    }
}

TEST(SourceImportPlanTests, RejectsInvalidInputsAndCapacityWithoutPartialPlan)
{
    const auto oldBytes = makeMetadata(baseSources(), baseUnits());
    auto newUnits = baseUnits();
    ++newUnits[0].importerVersion;
    ++newUnits[1].importerVersion;
    const auto newBytes = makeMetadata(baseSources(), newUnits);
    auto baseline = oldBytes.view();
    auto candidate = newBytes.view();
    ASSERT_TRUE(baseline.has_value());
    ASSERT_TRUE(candidate.has_value());

    std::pmr::unsynchronized_pool_resource memory;
    auto capacity = planSourceImports(*baseline, *candidate,
                                      SourceImportPlanConfig{.memoryResource = &memory, .maxChanges = 1U});
    ASSERT_FALSE(capacity.has_value());
    EXPECT_EQ(capacity.error().code, AssetErrorCode::SourceImportPlanCapacityExceeded);

    AssetFormat::SourceImportMetadataView invalid;
    auto invalidView = planSourceImports(invalid, *candidate,
                                         SourceImportPlanConfig{.memoryResource = &memory, .maxChanges = 2U});
    ASSERT_FALSE(invalidView.has_value());
    EXPECT_EQ(invalidView.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto nullMemory = planSourceImports(*baseline, *candidate,
                                        SourceImportPlanConfig{.memoryResource = nullptr, .maxChanges = 2U});
    ASSERT_FALSE(nullMemory.has_value());
    EXPECT_EQ(nullMemory.error().code, AssetErrorCode::InvalidCatalogConfig);
}

} // namespace
} // namespace Tina::Asset
