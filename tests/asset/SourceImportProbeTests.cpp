#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

struct SourceSpec final {
    std::string path{};
    std::vector<std::byte> consumedBytes{};
    AssetFormat::SourceImportReadExtent readExtent =
        AssetFormat::SourceImportReadExtent::WholeFile;
    bool primary = false;
};

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

void removeDirectory(const std::filesystem::path& path)
{
    std::error_code errorCode;
    std::filesystem::remove_all(path, errorCode);
}

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

[[nodiscard]] AssetFormat::SourceImportManifestRevision revision(Core::u8 seed = 70U)
{
    return AssetFormat::SourceImportManifestRevision{
        .manifestDigest = contentHash(seed),
        .manifestBytes = static_cast<Core::u64>(128U + seed),
    };
}

[[nodiscard]] std::vector<std::byte>
makeMetadata(const SourceImportUnitContract& contract,
             const std::vector<SourceSpec>& sourceSpecs,
             AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
             AssetFormat::SourceImportManifestRevision manifestRevision = revision(),
             bool includeSecondUnit = false)
{
    std::vector<AssetFormat::SourceImportMetadataWriteSource> sources;
    std::vector<AssetFormat::SourceImportMetadataWriteInput> inputs;
    sources.reserve(sourceSpecs.size());
    inputs.reserve(sourceSpecs.size());
    for (Core::u32 index = 0; index < sourceSpecs.size(); ++index)
    {
        const auto& source = sourceSpecs[index];
        auto digest = Core::digestContentHashV1(source.consumedBytes);
        EXPECT_TRUE(digest.has_value()) << (digest ? "" : digest.error().message);
        sources.push_back(AssetFormat::SourceImportMetadataWriteSource{
            .path = source.path,
            .contentHash = digest ? *digest : Core::ContentHash{},
            .fileBytes = static_cast<Core::u64>(source.consumedBytes.size()),
            .readExtent = source.readExtent,
        });
        inputs.push_back(AssetFormat::SourceImportMetadataWriteInput{
            .sourceIndex = index,
            .flags = source.primary ? AssetFormat::SourceImportInputFlags::Primary
                                    : AssetFormat::SourceImportInputFlags::None,
        });
    }
    const std::array outputs{
        AssetFormat::SourceImportMetadataWriteOutput{
            .assetId = assetId(1U),
            .assetKind = AssetFormat::AssetKind::StaticMesh,
        },
        AssetFormat::SourceImportMetadataWriteOutput{
            .assetId = assetId(2U),
            .assetKind = AssetFormat::AssetKind::Material,
        },
    };
    std::vector<AssetFormat::SourceImportMetadataWriteUnit> units{
        AssetFormat::SourceImportMetadataWriteUnit{
            .unitId = contract.unitId,
            .importerKind = static_cast<Core::u32>(contract.importerKind),
            .importerVersion = contract.importerVersion,
            .settingsHash = contract.settingsHash,
            .inputs = inputs,
            .outputs = outputs,
        }
    };
    const std::array secondOutputs{
        AssetFormat::SourceImportMetadataWriteOutput{
            .assetId = assetId(3U),
            .assetKind = AssetFormat::AssetKind::Prefab,
        },
    };
    if (includeSecondUnit)
    {
        AssetFormat::SourceImportUnitId::Bytes bytes{};
        bytes.fill(std::byte{0xFF});
        auto secondUnitId = *AssetFormat::SourceImportUnitId::fromBytes(bytes);
        if (secondUnitId == contract.unitId)
        {
            bytes[0] = std::byte{0xFE};
            secondUnitId = *AssetFormat::SourceImportUnitId::fromBytes(bytes);
        }
        units.push_back(AssetFormat::SourceImportMetadataWriteUnit{
            .unitId = secondUnitId,
            .importerKind = static_cast<Core::u32>(contract.importerKind),
            .importerVersion = contract.importerVersion,
            .settingsHash = contract.settingsHash,
            .inputs = inputs,
            .outputs = secondOutputs,
        });
        std::sort(units.begin(), units.end(), [](const auto& left, const auto& right) {
            return left.unitId < right.unitId;
        });
    }
    auto bytes = AssetFormat::writeSourceImportMetadataBytes(
        AssetFormat::SourceImportMetadataWriteDesc{
            .targetPlatform = targetPlatform,
            .manifestRevision = manifestRevision,
            .sources = sources,
            .units = units,
        });
    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error().message);
    return bytes ? std::move(*bytes) : std::vector<std::byte>{};
}

TEST(SourceImportProbeTests, MissingStateIsNoBaselineWithPrintableReason)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_source_probe_no_baseline";
    removeDirectory(root);
    const std::string rootUtf8 = toUtf8(root);
    auto desc = makeCatalogRecipeSourceImportProbeDesc(
        rootUtf8, toUtf8(root / "main.recipe"), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(desc.has_value()) << desc.error().message;

    auto probe = probeSourceImportState(toUtf8(root / "state.tmeta"), revision(), *desc);
    ASSERT_TRUE(probe.has_value()) << probe.error().message;
    EXPECT_EQ(probe->state, SourceImportProbeState::NoBaseline);
    EXPECT_EQ(probe->reason, SourceImportProbeReason::StateNotFound);
    EXPECT_EQ(sourceImportProbeReasonName(probe->reason), "state-not-found");
    EXPECT_EQ(probe->cleanUnitCount, 0U);
    EXPECT_EQ(probe->cleanObjectCount, 0U);
}

TEST(SourceImportProbeTests, ContractDirtyDoesNotReadSources)
{
    const auto temp = std::filesystem::temp_directory_path() / "tina_source_probe_contract";
    removeDirectory(temp);
    const auto sourceRoot = temp / "sources";
    const auto statePath = temp / "state.tmeta";
    const std::string rootUtf8 = toUtf8(sourceRoot);
    auto desc = makeCatalogRecipeSourceImportProbeDesc(
        rootUtf8, toUtf8(sourceRoot / "main.recipe"), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(desc.has_value()) << desc.error().message;
    const std::vector sources{
        SourceSpec{.path = "main.recipe", .consumedBytes = {std::byte{'r'}}, .primary = true},
    };
    ASSERT_TRUE(Core::writeFile(toUtf8(statePath), makeMetadata(desc->expected, sources)).has_value());

    auto revisionDirty = probeSourceImportState(toUtf8(statePath), revision(71U), *desc);
    ASSERT_TRUE(revisionDirty.has_value()) << revisionDirty.error().message;
    EXPECT_EQ(revisionDirty->state, SourceImportProbeState::Dirty);
    EXPECT_EQ(revisionDirty->reason, SourceImportProbeReason::CatalogRevisionChanged);

    ++desc->expected.importerVersion;
    auto versionDirty = probeSourceImportState(toUtf8(statePath), revision(), *desc);
    ASSERT_TRUE(versionDirty.has_value()) << versionDirty.error().message;
    EXPECT_EQ(versionDirty->state, SourceImportProbeState::Dirty);
    EXPECT_EQ(versionDirty->reason, SourceImportProbeReason::ImporterVersionChanged);

    --desc->expected.importerVersion;
    ASSERT_TRUE(Core::writeFile(toUtf8(statePath),
                                makeMetadata(desc->expected, sources,
                                             AssetFormat::TargetPlatform::WindowsX64,
                                             revision(), true)).has_value());
    auto unitSetDirty = probeSourceImportState(toUtf8(statePath), revision(), *desc);
    ASSERT_TRUE(unitSetDirty.has_value()) << unitSetDirty.error().message;
    EXPECT_EQ(unitSetDirty->state, SourceImportProbeState::Dirty);
    EXPECT_EQ(unitSetDirty->reason, SourceImportProbeReason::UnitSetChanged);

    auto oldSchemaBytes = makeMetadata(desc->expected, sources);
    ASSERT_GT(oldSchemaBytes.size(), 11U);
    oldSchemaBytes[10] = std::byte{0};
    oldSchemaBytes[11] = std::byte{0};
    ASSERT_TRUE(Core::writeFile(toUtf8(statePath), oldSchemaBytes).has_value());
    auto schemaDirty = probeSourceImportState(toUtf8(statePath), revision(), *desc);
    ASSERT_TRUE(schemaDirty.has_value()) << schemaDirty.error().message;
    EXPECT_EQ(schemaDirty->state, SourceImportProbeState::Dirty);
    EXPECT_EQ(schemaDirty->reason, SourceImportProbeReason::StateSchemaChanged);
    removeDirectory(temp);
}

TEST(SourceImportProbeTests, WholeFileRequiresExactSizeAndPrefixAllowsTrailingBytes)
{
    const auto temp = std::filesystem::temp_directory_path() / "tina_source_probe_extent";
    removeDirectory(temp);
    const auto sourceRoot = temp / "sources";
    const auto recipePath = sourceRoot / "recipes/main.recipe";
    const auto payloadPath = sourceRoot / "shared/payload.bin";
    const auto statePath = temp / "state.tmeta";
    constexpr std::array RecipeBytes{std::byte{'r'}, std::byte{'e'}, std::byte{'c'}};
    constexpr std::array PrefixBytes{std::byte{'p'}, std::byte{'r'}, std::byte{'e'}};
    constexpr std::array PayloadWithTail{
        std::byte{'p'}, std::byte{'r'}, std::byte{'e'}, std::byte{'t'}, std::byte{'a'}, std::byte{'i'}, std::byte{'l'},
    };
    ASSERT_TRUE(Core::writeFile(toUtf8(recipePath), RecipeBytes).has_value());
    ASSERT_TRUE(Core::writeFile(toUtf8(payloadPath), PayloadWithTail).has_value());

    auto desc = makeCatalogRecipeSourceImportProbeDesc(
        toUtf8(sourceRoot), toUtf8(recipePath), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(desc.has_value()) << desc.error().message;
    const std::vector sources{
        SourceSpec{.path = "recipes/main.recipe",
                   .consumedBytes = std::vector<std::byte>(RecipeBytes.begin(), RecipeBytes.end()),
                   .readExtent = AssetFormat::SourceImportReadExtent::WholeFile,
                   .primary = true},
        SourceSpec{.path = "shared/payload.bin",
                   .consumedBytes = std::vector<std::byte>(PrefixBytes.begin(), PrefixBytes.end()),
                   .readExtent = AssetFormat::SourceImportReadExtent::Prefix},
    };
    ASSERT_TRUE(Core::writeFile(toUtf8(statePath), makeMetadata(desc->expected, sources)).has_value());

    auto clean = probeCatalogRecipeSourceImportState(
        toUtf8(statePath), revision(), toUtf8(sourceRoot), toUtf8(recipePath));
    ASSERT_TRUE(clean.has_value()) << clean.error().message;
    EXPECT_EQ(clean->state, SourceImportProbeState::Clean);
    EXPECT_EQ(clean->cleanUnitCount, 1U);
    EXPECT_EQ(clean->cleanObjectCount, 2U);

    constexpr std::array ShortPrefix{std::byte{'p'}, std::byte{'r'}};
    ASSERT_TRUE(Core::writeFile(toUtf8(payloadPath), ShortPrefix).has_value());
    auto prefixSizeDirty = probeSourceImportState(toUtf8(statePath), revision(), *desc);
    ASSERT_TRUE(prefixSizeDirty.has_value()) << prefixSizeDirty.error().message;
    EXPECT_EQ(prefixSizeDirty->state, SourceImportProbeState::Dirty);
    EXPECT_EQ(prefixSizeDirty->reason, SourceImportProbeReason::SourceSizeChanged);
    ASSERT_TRUE(Core::writeFile(toUtf8(payloadPath), PayloadWithTail).has_value());

    constexpr std::array RecipeWithTail{
        std::byte{'r'}, std::byte{'e'}, std::byte{'c'}, std::byte{'!'},
    };
    ASSERT_TRUE(Core::writeFile(toUtf8(recipePath), RecipeWithTail).has_value());
    auto sizeDirty = probeSourceImportState(toUtf8(statePath), revision(), *desc);
    ASSERT_TRUE(sizeDirty.has_value()) << sizeDirty.error().message;
    EXPECT_EQ(sizeDirty->state, SourceImportProbeState::Dirty);
    EXPECT_EQ(sizeDirty->reason, SourceImportProbeReason::SourceSizeChanged);

    ASSERT_TRUE(Core::writeFile(toUtf8(recipePath), RecipeBytes).has_value());
    constexpr std::array ChangedPrefix{
        std::byte{'x'}, std::byte{'r'}, std::byte{'e'}, std::byte{'t'}, std::byte{'a'}, std::byte{'i'}, std::byte{'l'},
    };
    ASSERT_TRUE(Core::writeFile(toUtf8(payloadPath), ChangedPrefix).has_value());
    auto contentDirty = probeSourceImportState(toUtf8(statePath), revision(), *desc);
    ASSERT_TRUE(contentDirty.has_value()) << contentDirty.error().message;
    EXPECT_EQ(contentDirty->state, SourceImportProbeState::Dirty);
    EXPECT_EQ(contentDirty->reason, SourceImportProbeReason::SourceContentChanged);
    removeDirectory(temp);
}

TEST(SourceImportProbeTests, GltfSecondaryMustRemainBelowOpenedPrimaryDirectory)
{
    const auto temp = std::filesystem::temp_directory_path() / "tina_source_probe_gltf_containment";
    removeDirectory(temp);
    const auto sourceRoot = temp / "sources";
    const auto primaryPath = sourceRoot / "models/scene.gltf";
    const auto externalPath = sourceRoot / "outside/mesh.bin";
    constexpr std::array PrimaryBytes{std::byte{'g'}, std::byte{'l'}, std::byte{'t'}, std::byte{'f'}};
    constexpr std::array ExternalBytes{std::byte{'b'}, std::byte{'i'}, std::byte{'n'}};
    ASSERT_TRUE(Core::writeFile(toUtf8(primaryPath), PrimaryBytes).has_value());
    ASSERT_TRUE(Core::writeFile(toUtf8(externalPath), ExternalBytes).has_value());
    auto desc = makeGltfSourceImportProbeDesc(toUtf8(sourceRoot), toUtf8(primaryPath), GltfCookIds{});
    ASSERT_TRUE(desc.has_value()) << desc.error().message;
    const std::vector sources{
        SourceSpec{.path = "models/scene.gltf",
                   .consumedBytes = std::vector<std::byte>(PrimaryBytes.begin(), PrimaryBytes.end()),
                   .primary = true},
        SourceSpec{.path = "outside/mesh.bin",
                   .consumedBytes = std::vector<std::byte>(ExternalBytes.begin(), ExternalBytes.end())},
    };
    const auto metadataBytes = makeMetadata(desc->expected, sources);
    auto baseline = AssetFormat::parseSourceImportMetadataView(metadataBytes);
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;

    auto probe = probeSourceImportUnit(*baseline, revision(), *desc);
    ASSERT_FALSE(probe.has_value());
    EXPECT_EQ(probe.error().code, AssetErrorCode::InvalidCatalogConfig);
    removeDirectory(temp);
}

} // namespace
} // namespace Tina::Asset
