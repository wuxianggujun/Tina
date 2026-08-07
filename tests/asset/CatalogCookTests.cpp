#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogChangePlan.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageLoad.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/EnvironmentMapPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

[[nodiscard]] CatalogCookRequest makeTextureMaterialRequest(std::byte textureValue = std::byte{'t'})
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));
    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = {textureValue, std::byte{'e'}, std::byte{'x'}},
    });
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .payload = {std::byte{'m'}, std::byte{'a'}, std::byte{'t'}},
        .dependencies =
            {
                AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = AssetFormat::AssetKind::Texture2D,
                    .flags = AssetFormat::DependencyFlags::Required,
                },
            },
    });
    return request;
}

[[nodiscard]] CatalogPackageStageConfig makeStageConfig(std::pmr::memory_resource& memory,
                                                        bool verifyTypedPayload = false)
{
    return CatalogPackageStageConfig{
        .validation =
            CatalogPackageOpenConfig{
                .manifest =
                    CatalogFileLoadConfig{
                        .catalog =
                            CatalogConfig{
                                .maxEntries = 8U,
                                .maxDependencies = 8U,
                                .maxDependenciesPerAsset = 4U,
                                .memoryResource = &memory,
                            },
                    },
                .validation =
                    CatalogPackageValidationConfig{
                        .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                        .verifyTypedPayload = verifyTypedPayload,
                    },
            },
    };
}

void removeDirectory(const std::filesystem::path& path)
{
    std::error_code errorCode;
    std::filesystem::remove_all(path, errorCode);
}

[[nodiscard]] std::vector<std::byte> makeMonoPcm16Wav()
{
    constexpr Core::u32 SampleRate = 8000U;
    constexpr Core::u16 Channels = 1U;
    constexpr Core::u16 BitsPerSample = 16U;
    constexpr std::array<Core::i16, 8> Samples{0, 8000, 16000, 8000, 0, -8000, -16000, -8000};
    constexpr Core::u32 DataBytes = static_cast<Core::u32>(Samples.size() * sizeof(Core::i16));
    constexpr Core::u32 FormatChunkBytes = 16U;
    constexpr Core::u32 RiffBytes = 4U + (8U + FormatChunkBytes) + (8U + DataBytes);

    std::vector<std::byte> wav;
    wav.reserve(44U + DataBytes);
    const auto appendText = [&](std::string_view text) {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        wav.insert(wav.end(), bytes.begin(), bytes.end());
    };
    const auto appendU16 = [&](Core::u16 value) {
        wav.push_back(static_cast<std::byte>(value & 0xFFU));
        wav.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    };
    const auto appendU32 = [&](Core::u32 value) {
        for (Core::u32 shift = 0U; shift < 32U; shift += 8U)
        {
            wav.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        }
    };

    appendText("RIFF");
    appendU32(RiffBytes);
    appendText("WAVE");
    appendText("fmt ");
    appendU32(FormatChunkBytes);
    appendU16(1U);
    appendU16(Channels);
    appendU32(SampleRate);
    appendU32(SampleRate * Channels * (BitsPerSample / 8U));
    appendU16(static_cast<Core::u16>(Channels * (BitsPerSample / 8U)));
    appendU16(BitsPerSample);
    appendText("data");
    appendU32(DataBytes);
    for (const auto sample : Samples)
    {
        appendU16(static_cast<Core::u16>(sample));
    }
    return wav;
}

[[nodiscard]] constexpr std::array<std::byte, 16>
catalogRecipeSettingsBytes(AssetFormat::TargetPlatform targetPlatform) noexcept
{
    return {
        std::byte{'T'}, std::byte{'I'}, std::byte{'N'}, std::byte{'A'},
        std::byte{'R'}, std::byte{'S'}, std::byte{'E'}, std::byte{'T'},
        std::byte{1}, std::byte{0},
        static_cast<std::byte>(static_cast<Core::u16>(targetPlatform) & 0xFFU),
        static_cast<std::byte>((static_cast<Core::u16>(targetPlatform) >> 8U) & 0xFFU),
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
    };
}

[[nodiscard]] const SourceImportCapturedSource*
findCapturedSource(const SourceImportCandidate& candidate, std::string_view path) noexcept
{
    const auto found = std::find_if(candidate.sources.begin(), candidate.sources.end(),
                                    [path](const SourceImportCapturedSource& source) {
                                        return source.path == path;
                                    });
    return found == candidate.sources.end() ? nullptr : &*found;
}

TEST(CatalogCookTests, CookAndPublishFromRequest)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));
    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = {std::byte{'t'}, std::byte{'e'}, std::byte{'x'}},
    });
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .payload = {std::byte{'m'}, std::byte{'a'}, std::byte{'t'}},
        .dependencies =
            {
                AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = AssetFormat::AssetKind::Texture2D,
                    .flags = AssetFormat::DependencyFlags::Required,
                },
            },
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_catalog_cook_req";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 2U);
    EXPECT_EQ(catalog->dependencyCount(), 1U);
    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, StagesIntoFreshRootAndReturnsValidatedCatalog)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto stageRoot = std::filesystem::temp_directory_path() / "tina_catalog_stage_fresh";
    removeDirectory(stageRoot);

    auto staged = cookAndStageCatalogPackage(toUtf8(stageRoot), makeTextureMaterialRequest(),
                                             makeStageConfig(memory));
    ASSERT_TRUE(staged.has_value()) << staged.error().message;
    EXPECT_EQ(staged->entryCount(), 2U);
    EXPECT_EQ(staged->dependencyCount(), 1U);
    EXPECT_TRUE(std::filesystem::is_regular_file(stageRoot / "manifest.tmnft"));

    removeDirectory(stageRoot);
}

TEST(CatalogCookTests, StageRejectsExistingRootWithoutChangingItsContents)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto stageRoot = std::filesystem::temp_directory_path() / "tina_catalog_stage_existing";
    removeDirectory(stageRoot);
    ASSERT_TRUE(std::filesystem::create_directory(stageRoot));
    const auto marker = stageRoot / "owner.marker";
    const std::array markerBytes{std::byte{0x42}};
    ASSERT_TRUE(Core::writeFile(toUtf8(marker), markerBytes).has_value());

    auto staged = cookAndStageCatalogPackage(toUtf8(stageRoot), makeTextureMaterialRequest(),
                                             makeStageConfig(memory));
    ASSERT_FALSE(staged.has_value());
    EXPECT_EQ(staged.error().code, Core::CoreErrorCode::AlreadyExists);
    EXPECT_TRUE(std::filesystem::is_regular_file(marker));
    EXPECT_FALSE(std::filesystem::exists(stageRoot / "manifest.tmnft"));

    removeDirectory(stageRoot);
}

TEST(CatalogCookTests, StageValidationFailureLeavesOnlyPrivateStagingData)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto stageRoot = std::filesystem::temp_directory_path() / "tina_catalog_stage_invalid_typed";
    removeDirectory(stageRoot);

    auto staged = cookAndStageCatalogPackage(toUtf8(stageRoot), makeTextureMaterialRequest(),
                                             makeStageConfig(memory, true));
    ASSERT_FALSE(staged.has_value());
    EXPECT_TRUE(std::filesystem::is_regular_file(stageRoot / "manifest.tmnft"));

    removeDirectory(stageRoot);
}

TEST(CatalogCookTests, StagedCatalogsFeedDeterministicChangePlanning)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto baselineRoot = std::filesystem::temp_directory_path() / "tina_catalog_stage_plan_baseline";
    const auto changedRoot = std::filesystem::temp_directory_path() / "tina_catalog_stage_plan_changed";
    removeDirectory(baselineRoot);
    removeDirectory(changedRoot);

    auto baseline = cookAndStageCatalogPackage(toUtf8(baselineRoot), makeTextureMaterialRequest(),
                                               makeStageConfig(memory));
    auto changed = cookAndStageCatalogPackage(toUtf8(changedRoot), makeTextureMaterialRequest(std::byte{'u'}),
                                              makeStageConfig(memory));
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    ASSERT_TRUE(changed.has_value()) << changed.error().message;

    auto plan = planCatalogChanges(*baseline, *changed,
                                   CatalogChangePlanConfig{.memoryResource = &memory, .maxChanges = 2U});
    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    ASSERT_EQ(plan->changes.size(), 2U);
    EXPECT_EQ(plan->modifiedCount, 1U);
    EXPECT_EQ(plan->affectedCount, 1U);
    EXPECT_EQ(plan->changes[0].assetId, *Core::AssetId::fromBytes(idBytes(1U)));
    EXPECT_EQ(plan->changes[0].kind, CatalogChangeKind::Modified);
    EXPECT_EQ(plan->changes[1].assetId, *Core::AssetId::fromBytes(idBytes(2U)));
    EXPECT_EQ(plan->changes[1].kind, CatalogChangeKind::Affected);

    removeDirectory(baselineRoot);
    removeDirectory(changedRoot);
}

TEST(CatalogCookTests, IncrementalStageCopiesCleanBytesAndCooksDirtyAssets)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto baselineRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_baseline";
    const auto stageRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_stage";
    removeDirectory(baselineRoot);
    removeDirectory(stageRoot);

    auto baseline = cookAndStageCatalogPackage(toUtf8(baselineRoot), makeTextureMaterialRequest(),
                                               makeStageConfig(memory));
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;

    auto dirtyRequest = makeTextureMaterialRequest();
    dirtyRequest.assets.erase(dirtyRequest.assets.begin());
    dirtyRequest.assets.front().payload = {std::byte{'n'}, std::byte{'e'}, std::byte{'w'}};
    const std::array cleanAssetIds{textureId};
    auto staged = cookAndStageIncrementalCatalogPackage(
        toUtf8(stageRoot), toUtf8(baselineRoot), *baseline, cleanAssetIds, dirtyRequest,
        makeStageConfig(memory));
    ASSERT_TRUE(staged.has_value()) << staged.error().message;
    EXPECT_EQ(staged->entryCount(), 2U);
    EXPECT_EQ(staged->dependencyCount(), 1U);

    auto loadConfig = makeStageConfig(memory).validation.validation.file;
    auto baselineTexture = loadCookedAssetFromCatalog(toUtf8(baselineRoot), *baseline, textureId,
                                                       loadConfig);
    auto stagedTexture = loadCookedAssetFromCatalog(toUtf8(stageRoot), *staged, textureId, loadConfig);
    ASSERT_TRUE(baselineTexture.has_value()) << baselineTexture.error().message;
    ASSERT_TRUE(stagedTexture.has_value()) << stagedTexture.error().message;
    ASSERT_EQ(baselineTexture->bytes().size(), stagedTexture->bytes().size());
    EXPECT_TRUE(std::equal(baselineTexture->bytes().begin(), baselineTexture->bytes().end(),
                           stagedTexture->bytes().begin()));

    auto stagedMaterial = loadCookedAssetFromCatalog(toUtf8(stageRoot), *staged, materialId, loadConfig);
    ASSERT_TRUE(stagedMaterial.has_value()) << stagedMaterial.error().message;
    ASSERT_EQ(stagedMaterial->payload().size(), 3U);
    EXPECT_EQ(stagedMaterial->payload()[0], std::byte{'n'});

    removeDirectory(baselineRoot);
    removeDirectory(stageRoot);
}

TEST(CatalogCookTests, IncrementalStageRejectsCleanDirtyAssetIdCollisionBeforeCreatingRoot)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto baselineRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_dup_base";
    const auto stageRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_dup_stage";
    removeDirectory(baselineRoot);
    removeDirectory(stageRoot);

    auto baseline = cookAndStageCatalogPackage(toUtf8(baselineRoot), makeTextureMaterialRequest(),
                                               makeStageConfig(memory));
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    const std::array cleanAssetIds{textureId};

    auto staged = cookAndStageIncrementalCatalogPackage(
        toUtf8(stageRoot), toUtf8(baselineRoot), *baseline, cleanAssetIds,
        makeTextureMaterialRequest(std::byte{'u'}), makeStageConfig(memory));
    ASSERT_FALSE(staged.has_value());
    EXPECT_EQ(staged.error().code, AssetErrorCode::InvalidCatalogConfig);
    EXPECT_FALSE(std::filesystem::exists(stageRoot));

    removeDirectory(baselineRoot);
}

TEST(CatalogCookTests, IncrementalStageRejectsBaselinePlatformMismatchBeforeCreatingRoot)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto baselineRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_platform_base";
    const auto stageRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_platform_stage";
    removeDirectory(baselineRoot);
    removeDirectory(stageRoot);

    auto baseline = cookAndStageCatalogPackage(toUtf8(baselineRoot), makeTextureMaterialRequest(),
                                               makeStageConfig(memory));
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    auto dirtyRequest = makeTextureMaterialRequest();
    dirtyRequest.assets.erase(dirtyRequest.assets.begin());
    dirtyRequest.targetPlatform = AssetFormat::TargetPlatform::LinuxX64;
    const std::array cleanAssetIds{textureId};

    auto staged = cookAndStageIncrementalCatalogPackage(
        toUtf8(stageRoot), toUtf8(baselineRoot), *baseline, cleanAssetIds, dirtyRequest,
        makeStageConfig(memory));
    ASSERT_FALSE(staged.has_value());
    EXPECT_EQ(staged.error().code, AssetErrorCode::CatalogEntryMismatch);
    EXPECT_FALSE(std::filesystem::exists(stageRoot));

    removeDirectory(baselineRoot);
}

TEST(CatalogCookTests, IncrementalStageRejectsStageInsideBaselinePackage)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto baselineRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_nested_base";
    const auto stageRoot = baselineRoot / "candidate";
    removeDirectory(baselineRoot);

    auto baseline = cookAndStageCatalogPackage(toUtf8(baselineRoot), makeTextureMaterialRequest(),
                                               makeStageConfig(memory));
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    auto dirtyRequest = makeTextureMaterialRequest();
    dirtyRequest.assets.erase(dirtyRequest.assets.begin());
    const std::array cleanAssetIds{textureId};

    auto staged = cookAndStageIncrementalCatalogPackage(
        toUtf8(stageRoot), toUtf8(baselineRoot), *baseline, cleanAssetIds, dirtyRequest,
        makeStageConfig(memory));
    ASSERT_FALSE(staged.has_value());
    EXPECT_EQ(staged.error().code, AssetErrorCode::InvalidCatalogConfig);
    EXPECT_FALSE(std::filesystem::exists(stageRoot));

    removeDirectory(baselineRoot);
}

TEST(CatalogCookTests, IncrementalStageRejectsMissingDependencyBeforeCreatingRoot)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto missingId = *Core::AssetId::fromBytes(idBytes(9U));
    const auto baselineRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_dep_base";
    const auto stageRoot = std::filesystem::temp_directory_path() / "tina_catalog_incremental_dep_stage";
    removeDirectory(baselineRoot);
    removeDirectory(stageRoot);

    auto baseline = cookAndStageCatalogPackage(toUtf8(baselineRoot), makeTextureMaterialRequest(),
                                               makeStageConfig(memory));
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    auto dirtyRequest = makeTextureMaterialRequest();
    dirtyRequest.assets.erase(dirtyRequest.assets.begin());
    dirtyRequest.assets.front().dependencies.front().assetId = missingId;
    const std::array cleanAssetIds{textureId};

    auto staged = cookAndStageIncrementalCatalogPackage(
        toUtf8(stageRoot), toUtf8(baselineRoot), *baseline, cleanAssetIds, dirtyRequest,
        makeStageConfig(memory));
    ASSERT_FALSE(staged.has_value());
    EXPECT_EQ(staged.error().code, AssetFormat::AssetFormatErrorCode::MissingDependency);
    EXPECT_FALSE(std::filesystem::exists(stageRoot));

    removeDirectory(baselineRoot);
}

TEST(CatalogCookTests, InlineTexture2dAndSpriteRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(3U));
    const auto texHex = textureId.canonicalText();
    const auto spriteHex = spriteId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "texture2d ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 1 1 FF0000FF\n";
    recipe += "sprite ";
    recipe.append(spriteHex.data(), spriteHex.size());
    recipe += " ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 0 0 1 1 0.5 0.5 16\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    EXPECT_EQ(request->assets.size(), 2U);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_recipe";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 2U);
    EXPECT_EQ(catalog->dependencyCount(), 1U);
    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, InlineSpriteAnimationRecipesCookAndLoadThreeClips)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteA = *Core::AssetId::fromBytes(idBytes(2U));
    const auto spriteB = *Core::AssetId::fromBytes(idBytes(3U));
    const auto spriteC = *Core::AssetId::fromBytes(idBytes(4U));
    const auto onceClipId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto loopClipId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto pingPongClipId = *Core::AssetId::fromBytes(idBytes(7U));

    std::string recipe = "platform WindowsX64\n";
    const auto appendId = [&recipe](Core::AssetId id) {
        const auto text = id.canonicalText();
        recipe.append(text.data(), text.size());
    };
    recipe += "texture2d ";
    appendId(textureId);
    recipe += " 1 1 FFFFFFFF\n";
    for (const auto spriteId : std::array{spriteA, spriteB, spriteC})
    {
        recipe += "sprite ";
        appendId(spriteId);
        recipe += " ";
        appendId(textureId);
        recipe += "\n";
    }
    recipe += "spriteanim ";
    appendId(onceClipId);
    recipe += " Once ";
    appendId(spriteC);
    recipe += ":0.12\n";
    recipe += "spriteanim ";
    appendId(loopClipId);
    recipe += " Loop ";
    appendId(spriteA);
    recipe += ":0.20 ";
    appendId(spriteA);
    recipe += ":0.15\n";
    recipe += "spriteanim ";
    appendId(pingPongClipId);
    recipe += " PingPong ";
    appendId(spriteB);
    recipe += ":0.08 ";
    appendId(spriteA);
    recipe += ":0.09 ";
    appendId(spriteB);
    recipe += ":0.10\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 7U);
    const auto pingSpec = std::find_if(
        request->assets.begin(), request->assets.end(),
        [pingPongClipId](const CatalogCookAssetSpec& asset) { return asset.assetId == pingPongClipId; });
    ASSERT_NE(pingSpec, request->assets.end());
    EXPECT_EQ(pingSpec->assetKind, AssetFormat::AssetKind::SpriteAnimationClip);
    ASSERT_EQ(pingSpec->dependencies.size(), 2U);
    EXPECT_EQ(pingSpec->dependencies[0].assetId, spriteA);
    EXPECT_EQ(pingSpec->dependencies[1].assetId, spriteB);
    auto pingPayload = AssetFormat::parseSpriteAnimationClipPayload(pingSpec->payload);
    ASSERT_TRUE(pingPayload.has_value()) << pingPayload.error().message;
    EXPECT_EQ(pingPayload->playbackMode, AssetFormat::SpriteAnimationPlaybackMode::PingPong);
    ASSERT_TRUE(pingPayload->frame(0U).has_value());
    ASSERT_TRUE(pingPayload->frame(1U).has_value());
    EXPECT_EQ(pingPayload->frame(0U)->spriteDependencyIndex, 1U);
    EXPECT_EQ(pingPayload->frame(1U)->spriteDependencyIndex, 0U);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_sprite_animation";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 16,
                        .maxDependencies = 32,
                        .maxDependenciesPerAsset = 8,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 7U);
    EXPECT_EQ(catalog->dependencyCount(), 7U);

    auto loaded = loadCookedAssetsFromPackage(
        toUtf8(root), std::array{onceClipId, loopClipId, pingPongClipId}, openConfig,
        CookedAssetBatchLoadConfig{
            .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
            .memoryResource = &memory,
        });
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(loaded->assets.size(), 7U);
    const CookedAssetFile* pingFile = nullptr;
    for (const auto& asset : loaded->assets)
    {
        if (asset.header().assetId == pingPongClipId)
        {
            pingFile = &asset;
            break;
        }
    }
    ASSERT_NE(pingFile, nullptr);
    auto pingClip = parseSpriteAnimationClipFromCooked(*pingFile);
    ASSERT_TRUE(pingClip.has_value()) << pingClip.error().message;
    const auto firstFrame = pingClip->frame(0U);
    ASSERT_TRUE(firstFrame.has_value());
    const auto firstSprite = pingFile->dependency(firstFrame->spriteDependencyIndex);
    ASSERT_TRUE(firstSprite.has_value());
    EXPECT_EQ(firstSprite->assetId, spriteB);

    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, SpriteAnimationRecipeRejectsBadModeAndDuration)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto clipId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto spriteText = spriteId.canonicalText();
    const auto clipText = clipId.canonicalText();

    std::string badMode = "spriteanim ";
    badMode.append(clipText.data(), clipText.size());
    badMode += " Bounce ";
    badMode.append(spriteText.data(), spriteText.size());
    badMode += ":0.1\n";
    EXPECT_FALSE(parseCatalogCookRecipe(badMode, ".").has_value());

    std::string badDuration = "spriteanim ";
    badDuration.append(clipText.data(), clipText.size());
    badDuration += " Once ";
    badDuration.append(spriteText.data(), spriteText.size());
    badDuration += ":0\n";
    EXPECT_FALSE(parseCatalogCookRecipe(badDuration, ".").has_value());
}

TEST(CatalogCookTests, InlineTilesetAndTileMapRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto texHex = textureId.canonicalText();
    const auto tilesetHex = tilesetId.canonicalText();
    const auto mapHex = mapId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "texture2d ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 2 2 FFFFFFFF 000000FF FFFFFFFF 000000FF\n";
    recipe += "tileset ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 16 16\n";
    recipe += "tile 1 1 0 0 0.5 0.5\n";
    recipe += "tile 2 0 0.5 0 1 0.5\n";
    recipe += "tilemap ";
    recipe.append(mapHex.data(), mapHex.size());
    recipe += " ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " 2 2 1.0\n";
    recipe += "tilelayer 10 1 visual\n";
    recipe += "property role render\n";
    recipe += "row 1 2\n";
    recipe += "row 2 0\n";
    recipe += "endlayer\n";
    recipe += "objectlayer 20 1 gameplay\n";
    recipe += "property domain gameplay\n";
    recipe += "point 101 1 spawn 1 1\n";
    recipe += "objectproperty 101 role player\n";
    recipe += "endlayer\n";
    recipe += "endtilemap\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    EXPECT_EQ(request->assets.size(), 4U);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_tilemap_recipe";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 16,
                        .maxDependencies = 16,
                        .maxDependenciesPerAsset = 8,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 4U);
    // Texture has 0 deps, tileset 1, tilemap 2 (Tileset + deferred chunk).
    EXPECT_EQ(catalog->dependencyCount(), 3U);

    // Load expands dependencies (Texture → Tileset → TileMap).
    auto loaded = loadCookedAssetsFromPackage(
        toUtf8(root), std::array{mapId}, openConfig,
        CookedAssetBatchLoadConfig{.file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                                   .memoryResource = &memory});
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(loaded->assets.size(), 3U);
    const CookedAssetFile* mapFile = nullptr;
    for (const auto& asset : loaded->assets)
    {
        if (asset.header().assetKind == AssetFormat::AssetKind::TileMap)
        {
            mapFile = &asset;
            break;
        }
    }
    ASSERT_NE(mapFile, nullptr);
    auto mapView = parseTileMapFromCooked(*mapFile);
    ASSERT_TRUE(mapView.has_value()) << mapView.error().message;
    EXPECT_EQ(mapView->widthCells, 2U);
    EXPECT_EQ(mapView->heightCells, 2U);
    ASSERT_TRUE(mapView->layerAt(0).has_value());
    ASSERT_TRUE(mapView->layerAt(1).has_value());
    EXPECT_EQ(mapView->layerAt(0)->stableLayerId, 10U);
    EXPECT_EQ(mapView->layerAt(1)->stableLayerId, 20U);
    const auto visual = mapView->findLayer(10);
    ASSERT_TRUE(visual.has_value());
    EXPECT_TRUE(visual->visible);
    ASSERT_TRUE(visual->findProperty("role").has_value());
    EXPECT_EQ(visual->findProperty("role")->value, "render");
    EXPECT_EQ(visual->chunkRefCount, 1U);
    const auto visualChunk = visual->chunkRefAt(0);
    ASSERT_TRUE(visualChunk.has_value());
    EXPECT_EQ(visualChunk->chunkX, 0U);
    EXPECT_EQ(visualChunk->chunkY, 0U);
    EXPECT_EQ(visualChunk->widthCells, 2U);
    EXPECT_EQ(visualChunk->heightCells, 2U);
    EXPECT_EQ(visualChunk->nonEmptyCount, 3U);
    const auto gameplay = mapView->findLayer(20);
    ASSERT_TRUE(gameplay.has_value());
    EXPECT_EQ(gameplay->objectCount, 1U);
    const auto spawn = gameplay->findObject(101);
    ASSERT_TRUE(spawn.has_value());
    EXPECT_TRUE(spawn->visible);
    ASSERT_TRUE(spawn->findProperty("role").has_value());
    EXPECT_EQ(spawn->findProperty("role")->value, "player");

    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, TileMapChunkIdsIncludeFullParentMapIdentity)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    auto firstMapBytes = idBytes(6U);
    firstMapBytes[1] = std::byte{0x11};
    auto secondMapBytes = firstMapBytes;
    secondMapBytes[1] = std::byte{0x22};
    const auto firstMapId = *Core::AssetId::fromBytes(firstMapBytes);
    const auto secondMapId = *Core::AssetId::fromBytes(secondMapBytes);

    std::string recipe = "platform WindowsX64\ntexture2d ";
    const auto appendId = [&recipe](Core::AssetId id) {
        const auto text = id.canonicalText();
        recipe.append(text.data(), text.size());
    };
    appendId(textureId);
    recipe += " 1 1 FFFFFFFF\ntileset ";
    appendId(tilesetId);
    recipe += " ";
    appendId(textureId);
    recipe += " 16 16\ntile 1 1 0 0 1 1\n";
    for (const Core::AssetId mapId : std::array{firstMapId, secondMapId})
    {
        recipe += "tilemap ";
        appendId(mapId);
        recipe += " ";
        appendId(tilesetId);
        recipe += " 1 1 1.0\ntilelayer 10 1 visual\nrow 1\nendlayer\nendtilemap\n";
    }

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 6U);

    const auto findChunkId = [&request](Core::AssetId mapId) {
        const auto map = std::find_if(request->assets.begin(), request->assets.end(),
                                      [mapId](const CatalogCookAssetSpec& asset) { return asset.assetId == mapId; });
        EXPECT_NE(map, request->assets.end());
        if (map == request->assets.end())
        {
            return Core::AssetId{};
        }
        const auto chunk = std::find_if(
            map->dependencies.begin(), map->dependencies.end(),
            [](const AssetFormat::CookedAssetWriteDependency& dependency) {
                return dependency.expectedKind == AssetFormat::AssetKind::TileMapChunk;
            });
        EXPECT_NE(chunk, map->dependencies.end());
        return chunk == map->dependencies.end() ? Core::AssetId{} : chunk->assetId;
    };
    const Core::AssetId firstChunkId = findChunkId(firstMapId);
    const Core::AssetId secondChunkId = findChunkId(secondMapId);
    ASSERT_TRUE(firstChunkId);
    ASSERT_TRUE(secondChunkId);
    EXPECT_NE(firstChunkId, secondChunkId);

    auto cooked = cookCatalogPackage(*request);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    EXPECT_EQ(cooked->entryCount, 6U);
    EXPECT_EQ(cooked->dependencyCount, 5U);
}

TEST(CatalogCookTests, TileMapChunkDependenciesAreCanonicalizedByAssetId)
{
    constexpr Core::u32 MapExtent = 64U;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));

    std::string recipe = "platform WindowsX64\ntexture2d ";
    const auto appendId = [&recipe](Core::AssetId id) {
        const auto text = id.canonicalText();
        recipe.append(text.data(), text.size());
    };
    appendId(textureId);
    recipe += " 1 1 FFFFFFFF\ntileset ";
    appendId(tilesetId);
    recipe += " ";
    appendId(textureId);
    recipe += " 16 16\ntile 1 1 0 0 1 1\ntilemap ";
    appendId(mapId);
    recipe += " ";
    appendId(tilesetId);
    recipe += " 64 64 1.0\ntilelayer 10 1 visual\n";
    for (Core::u32 y = 0; y < MapExtent; ++y)
    {
        recipe += "row";
        for (Core::u32 x = 0; x < MapExtent; ++x)
        {
            recipe += " 1";
        }
        recipe += "\n";
    }
    recipe += "endlayer\nendtilemap\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 19U);
    const auto map = std::find_if(request->assets.begin(), request->assets.end(),
                                  [mapId](const CatalogCookAssetSpec& asset) { return asset.assetId == mapId; });
    ASSERT_NE(map, request->assets.end());
    ASSERT_EQ(map->dependencies.size(), 17U);
    EXPECT_TRUE(std::is_sorted(map->dependencies.begin(), map->dependencies.end(),
                               [](const AssetFormat::CookedAssetWriteDependency& left,
                                  const AssetFormat::CookedAssetWriteDependency& right) {
                                   return left.assetId < right.assetId;
                               }));

    auto cooked = cookCatalogPackage(*request);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    EXPECT_EQ(cooked->entryCount, 19U);
    EXPECT_EQ(cooked->dependencyCount, 18U);
}

TEST(CatalogCookTests, TileMapChunkIdIsStableWhenEarlierChunkBecomesNonEmpty)
{
    constexpr Core::u32 MapWidth = 32U;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto makeRecipe = [=](bool includeLeadingChunk) {
        std::string recipe = "platform WindowsX64\ntexture2d ";
        const auto appendId = [&recipe](Core::AssetId id) {
            const auto text = id.canonicalText();
            recipe.append(text.data(), text.size());
        };
        appendId(textureId);
        recipe += " 1 1 FFFFFFFF\ntileset ";
        appendId(tilesetId);
        recipe += " ";
        appendId(textureId);
        recipe += " 16 16\ntile 1 1 0 0 1 1\ntilemap ";
        appendId(mapId);
        recipe += " ";
        appendId(tilesetId);
        recipe += " 32 1 1.0\ntilelayer 10 1 visual\nrow";
        for (Core::u32 x = 0; x < MapWidth; ++x)
        {
            recipe += (x == 16U || (includeLeadingChunk && x == 0U)) ? " 1" : " 0";
        }
        recipe += "\nendlayer\nendtilemap\n";
        return recipe;
    };

    auto sparseRequest = parseCatalogCookRecipe(makeRecipe(false), ".");
    auto expandedRequest = parseCatalogCookRecipe(makeRecipe(true), ".");
    ASSERT_TRUE(sparseRequest.has_value()) << sparseRequest.error().message;
    ASSERT_TRUE(expandedRequest.has_value()) << expandedRequest.error().message;

    const auto chunkIdAt = [mapId](const CatalogCookRequest& request, Core::u32 chunkX) {
        const auto mapAsset = std::find_if(request.assets.begin(), request.assets.end(),
                                           [mapId](const CatalogCookAssetSpec& asset) {
                                               return asset.assetId == mapId;
                                           });
        EXPECT_NE(mapAsset, request.assets.end());
        if (mapAsset == request.assets.end())
        {
            return Core::AssetId{};
        }
        auto root = AssetFormat::parseTileMapPayload(mapAsset->payload);
        EXPECT_TRUE(root.has_value());
        if (!root)
        {
            return Core::AssetId{};
        }
        const auto layer = root->findLayer(10U);
        EXPECT_TRUE(layer.has_value());
        if (!layer)
        {
            return Core::AssetId{};
        }
        const auto chunk = layer->findChunkRef(chunkX, 0U);
        EXPECT_TRUE(chunk.has_value());
        return chunk ? chunk->chunkAssetId : Core::AssetId{};
    };
    const Core::AssetId sparseChunkId = chunkIdAt(*sparseRequest, 1U);
    const Core::AssetId expandedChunkId = chunkIdAt(*expandedRequest, 1U);
    ASSERT_TRUE(sparseChunkId);
    ASSERT_TRUE(expandedChunkId);
    EXPECT_EQ(sparseChunkId, expandedChunkId);
}

TEST(CatalogCookTests, TileMapRecipeRejectsLegacyRowsInvalidReferencesAndUnclosedBlocks)
{
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto tilesetHex = tilesetId.canonicalText();
    const auto mapHex = mapId.canonicalText();

    std::string header = "tilemap ";
    header.append(mapHex.data(), mapHex.size());
    header += " ";
    header.append(tilesetHex.data(), tilesetHex.size());
    header += " 1 1 1.0\n";

    EXPECT_FALSE(parseCatalogCookRecipe(header + "row 1\nendtilemap\n", ".").has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(header + "tilelayer 10 1 visual\nrow 1\nendtilemap\n", ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(header + "tilelayer 10 1 visual\nrow 1\nendlayer\n", ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(
                     header + "objectlayer 20 1 gameplay\nobjectproperty 999 role spawn\nendlayer\nendtilemap\n",
                     ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(
                     header + "objectlayer 20 1 gameplay\npoint 101 2 spawn 0 0\nendlayer\nendtilemap\n", ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(
                     header +
                         "tilelayer 10 1 visual\nrow 1\nendlayer\ntilelayer 10 1 collision\nrow 1\nendlayer\n"
                         "endtilemap\n",
                     ".")
                     .has_value());
}

TEST(CatalogCookTests, InvalidTileReferenceDoesNotPublishPartialCatalog)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto textureHex = textureId.canonicalText();
    const auto tilesetHex = tilesetId.canonicalText();
    const auto mapHex = mapId.canonicalText();

    std::string recipe = "texture2d ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += " 1 1 FFFFFFFF\ntileset ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += " 16 16\ntile 1 1 0 0 1 1\ntilemap ";
    recipe.append(mapHex.data(), mapHex.size());
    recipe += " ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " 1 1 1.0\ntilelayer 10 1 visual\nrow 99\nendlayer\nendtilemap\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    const auto root = std::filesystem::temp_directory_path() / "tina_invalid_tilemap_no_partial_publish";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    auto published = cookAndPublishCatalogPackage(toUtf8(root), *request);
    ASSERT_FALSE(published.has_value());
    EXPECT_FALSE(std::filesystem::exists(root / "manifest.tmnft"));
    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, RecipeFileRoundTrip)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto dir = std::filesystem::temp_directory_path()
        / std::filesystem::path{u8"tina_catalog_cook_recipe_\u76ee\u5f55"};
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto texPayload = dir / "tex.bin";
    const auto matPayload = dir / "mat.bin";
    const auto recipePath = dir / "pack.recipe";
    ASSERT_TRUE(Core::writeFile(toUtf8(texPayload), std::as_bytes(std::span("TEX", 3))).has_value());
    ASSERT_TRUE(Core::writeFile(toUtf8(matPayload), std::as_bytes(std::span("MAT", 3))).has_value());

    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto texHex = textureId.canonicalText();
    const auto matHex = materialId.canonicalText();
    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "asset Texture2D ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " tex.bin\n";
    recipe += "asset Material ";
    recipe.append(matHex.data(), matHex.size());
    recipe += " mat.bin ";
    recipe.append(texHex.data(), texHex.size());
    recipe += ":Texture2D\n";
    ASSERT_TRUE(Core::writeFile(toUtf8(recipePath),
                                std::as_bytes(std::span(recipe.data(), recipe.size())))
                    .has_value());

    auto request = loadCatalogCookRecipeFile(toUtf8(recipePath));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    EXPECT_EQ(request->assets.size(), 2U);

    const auto outRoot = dir / "out";
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(outRoot), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(outRoot), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 2U);
    EXPECT_EQ(catalog->dependencyCount(), 1U);

    std::filesystem::remove_all(dir, ec);
}

TEST(CatalogCookTests, ReadsRecipeTargetWithoutOpeningReferencedPayloads)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     "tina_catalog_recipe_target_only";
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    ASSERT_TRUE(std::filesystem::create_directories(dir, error));
    const auto recipePath = dir / "pack.recipe";
    const auto textureId = Core::AssetId::fromBytes(idBytes(0xA7U))->canonicalText();
    std::string recipe = "platform LinuxX64\nasset Texture2D ";
    recipe.append(textureId.data(), textureId.size());
    recipe += " missing-payload.bin\n";
    ASSERT_TRUE(Core::writeFile(
        toUtf8(recipePath), std::as_bytes(std::span(recipe.data(), recipe.size()))));

    auto target = loadCatalogCookRecipeTargetPlatform(toUtf8(recipePath));
    ASSERT_TRUE(target) << (target ? "" : target.error().message);
    EXPECT_EQ(*target, AssetFormat::TargetPlatform::LinuxX64);
    EXPECT_FALSE(loadCatalogCookRecipeFile(toUtf8(recipePath)));

    std::filesystem::remove_all(dir, error);
}

TEST(CatalogCookSourceTests, CapturesRecipeSharedGenericPayloadAndWavWithoutDuplicateSources)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_catalog_recipe_sources";
    removeDirectory(root);
    std::error_code errorCode;
    std::filesystem::create_directories(root / "payloads", errorCode);
    ASSERT_FALSE(errorCode);
    std::filesystem::create_directories(root / "audio", errorCode);
    ASSERT_FALSE(errorCode);

    const std::array payloadBytes{std::byte{'S'}, std::byte{'H'}, std::byte{'A'}, std::byte{'R'}, std::byte{'E'}};
    const auto wavBytes = makeMonoPcm16Wav();
    const auto payloadPath = root / "payloads" / "shared.bin";
    const auto wavPath = root / "audio" / "click.wav";
    const auto recipePath = root / "pack.recipe";
    ASSERT_TRUE(Core::writeFile(toUtf8(payloadPath), payloadBytes).has_value());
    ASSERT_TRUE(Core::writeFile(toUtf8(wavPath), wavBytes).has_value());

    const auto shaderId = *Core::AssetId::fromBytes(idBytes(20U));
    const auto fontId = *Core::AssetId::fromBytes(idBytes(21U));
    const auto audioId = *Core::AssetId::fromBytes(idBytes(22U));
    const auto shaderText = shaderId.canonicalText();
    const auto fontText = fontId.canonicalText();
    const auto audioText = audioId.canonicalText();
    std::string recipe = "platform WindowsX64\nasset Shader ";
    recipe.append(shaderText.data(), shaderText.size());
    recipe += " payloads/shared.bin\nasset Font ";
    recipe.append(fontText.data(), fontText.size());
    recipe += " payloads/shared.bin\naudioclip ";
    recipe.append(audioText.data(), audioText.size());
    recipe += " file audio/click.wav\n";
    const auto recipeBytes = std::as_bytes(std::span(recipe.data(), recipe.size()));
    ASSERT_TRUE(Core::writeFile(toUtf8(recipePath), recipeBytes).has_value());

    auto result = loadCatalogCookRecipeSourceFile(
        toUtf8(recipePath),
        SourceImportCaptureConfig{
            .sourceRootUtf8 = toUtf8(root),
            .maxSources = 8U,
        });
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->request.assets.size(), 3U);
    EXPECT_EQ(result->sourceImports.targetPlatform, AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_EQ(result->sourceImports.sources.size(), 3U);
    ASSERT_EQ(result->sourceImports.units.size(), 1U);

    const auto* recipeSource = findCapturedSource(result->sourceImports, "pack.recipe");
    const auto* payloadSource = findCapturedSource(result->sourceImports, "payloads/shared.bin");
    const auto* wavSource = findCapturedSource(result->sourceImports, "audio/click.wav");
    ASSERT_NE(recipeSource, nullptr);
    ASSERT_NE(payloadSource, nullptr);
    ASSERT_NE(wavSource, nullptr);
    const auto expectedRecipeHash = Core::digestContentHashV1(recipeBytes);
    const auto expectedPayloadHash = Core::digestContentHashV1(payloadBytes);
    const auto expectedWavHash = Core::digestContentHashV1(wavBytes);
    ASSERT_TRUE(expectedRecipeHash.has_value());
    ASSERT_TRUE(expectedPayloadHash.has_value());
    ASSERT_TRUE(expectedWavHash.has_value());
    EXPECT_EQ(recipeSource->contentHash, *expectedRecipeHash);
    EXPECT_EQ(recipeSource->fileBytes, recipeBytes.size());
    EXPECT_EQ(recipeSource->readExtent, AssetFormat::SourceImportReadExtent::WholeFile);
    EXPECT_EQ(payloadSource->contentHash, *expectedPayloadHash);
    EXPECT_EQ(payloadSource->fileBytes, payloadBytes.size());
    EXPECT_EQ(payloadSource->readExtent, AssetFormat::SourceImportReadExtent::WholeFile);
    EXPECT_EQ(wavSource->contentHash, *expectedWavHash);
    EXPECT_EQ(wavSource->fileBytes, wavBytes.size());
    EXPECT_EQ(wavSource->readExtent, AssetFormat::SourceImportReadExtent::WholeFile);

    const auto& unit = result->sourceImports.units.front();
    EXPECT_EQ(unit.importerKind, SourceImporterKind::CatalogRecipe);
    EXPECT_EQ(unit.importerVersion, 1U);
    const auto expectedUnitId = deriveSourceImportUnitId(SourceImporterKind::CatalogRecipe, "pack.recipe");
    const auto expectedSettingsHash =
        digestSourceImportSettings(catalogRecipeSettingsBytes(AssetFormat::TargetPlatform::WindowsX64));
    ASSERT_TRUE(expectedUnitId.has_value());
    ASSERT_TRUE(expectedSettingsHash.has_value());
    EXPECT_EQ(unit.unitId, *expectedUnitId);
    EXPECT_EQ(unit.settingsHash, *expectedSettingsHash);
    ASSERT_EQ(unit.inputs.size(), 3U);

    Core::u32 primaryCount = 0U;
    for (const auto& input : unit.inputs)
    {
        ASSERT_LT(input.sourceIndex, result->sourceImports.sources.size());
        if (AssetFormat::hasSourceImportInputFlag(input.flags,
                                                  AssetFormat::SourceImportInputFlags::Primary))
        {
            ++primaryCount;
            EXPECT_EQ(result->sourceImports.sources[input.sourceIndex].path, "pack.recipe");
        }
    }
    EXPECT_EQ(primaryCount, 1U);

    ASSERT_EQ(unit.outputs.size(), result->request.assets.size());
    for (const auto& asset : result->request.assets)
    {
        const auto output = std::find_if(unit.outputs.begin(), unit.outputs.end(),
                                         [&asset](const SourceImportCapturedOutput& candidate) {
                                             return candidate.assetId == asset.assetId &&
                                                    candidate.assetKind == asset.assetKind;
                                         });
        EXPECT_NE(output, unit.outputs.end());
    }

    removeDirectory(root);
}

TEST(CatalogCookSourceTests, RejectsRecipeDependenciesOutsideExplicitSourceRoot)
{
    const auto parent = std::filesystem::temp_directory_path() / "tina_catalog_recipe_source_root";
    const auto root = parent / "root";
    const auto outsidePath = parent / "outside.bin";
    const auto recipePath = root / "pack.recipe";
    removeDirectory(parent);
    std::error_code errorCode;
    std::filesystem::create_directories(root, errorCode);
    ASSERT_FALSE(errorCode);
    constexpr std::array OutsideBytes{std::byte{'O'}, std::byte{'U'}, std::byte{'T'}};
    ASSERT_TRUE(Core::writeFile(toUtf8(outsidePath), OutsideBytes).has_value());

    const auto assetId = *Core::AssetId::fromBytes(idBytes(23U));
    const auto assetText = assetId.canonicalText();
    std::string recipe = "asset Shader ";
    recipe.append(assetText.data(), assetText.size());
    recipe += " ../outside.bin\n";
    ASSERT_TRUE(Core::writeFile(toUtf8(recipePath),
                                std::as_bytes(std::span(recipe.data(), recipe.size())))
                    .has_value());

    const auto result = loadCatalogCookRecipeSourceFile(
        toUtf8(recipePath),
        SourceImportCaptureConfig{
            .sourceRootUtf8 = toUtf8(root),
            .maxSources = 4U,
        });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::InvalidCatalogConfig);

    removeDirectory(parent);
}

TEST(CatalogCookTests, GenericEnvironmentMapRecipeUsesCurrentPayloadVersion)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto environmentId = *Core::AssetId::fromBytes(idBytes(13U));
    const auto environmentHex = environmentId.canonicalText();
    std::vector<std::byte> diffuse(48U, std::byte{0x11});
    std::vector<std::byte> specular(48U, std::byte{0x22});
    std::vector<std::byte> brdf(4U, std::byte{0x33});
    auto payload = AssetFormat::writeEnvironmentMapPayloadBytes(AssetFormat::EnvironmentMapPayloadDesc{
        .diffuseFaceSize = 1,
        .specularFaceSize = 1,
        .specularMipCount = 1,
        .brdfWidth = 1,
        .brdfHeight = 1,
        .diffusePixels = diffuse,
        .specularPixels = specular,
        .brdfPixels = brdf,
    });
    ASSERT_TRUE(payload.has_value()) << payload.error().message;

    const auto dir = std::filesystem::temp_directory_path() / "tina_environment_map_recipe";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto payloadPath = dir / "environment.bin";
    ASSERT_TRUE(Core::writeFile(toUtf8(payloadPath), *payload).has_value());

    std::string recipe = "platform WindowsX64\nasset EnvironmentMap ";
    recipe.append(environmentHex.data(), environmentHex.size());
    recipe += " environment.bin\n";
    auto request = parseCatalogCookRecipe(recipe, toUtf8(dir));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::EnvironmentMap);
    EXPECT_EQ(request->assets[0].assetTypeVersion, AssetFormat::EnvironmentMapWire::SchemaVersion);
    EXPECT_EQ(request->assets[0].payload, *payload);

    const auto outRoot = dir / "out";
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(outRoot), *request).has_value());
    auto catalog = openCatalogPackage(
        toUtf8(outRoot),
        CatalogPackageOpenConfig{
            .manifest =
                CatalogFileLoadConfig{
                    .catalog =
                        CatalogConfig{
                            .maxEntries = 4,
                            .maxDependencies = 4,
                            .maxDependenciesPerAsset = 4,
                            .memoryResource = &memory,
                        },
                },
            .validateOnOpen = true,
            .validation =
                CatalogPackageValidationConfig{
                    .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                    .verifyContent = true,
                },
        });
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    auto asset = loadCookedAssetFromCatalog(toUtf8(outRoot), *catalog, environmentId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetFormat::AssetKind::EnvironmentMap);
    EXPECT_EQ(asset->header().assetTypeVersion, AssetFormat::EnvironmentMapWire::SchemaVersion);
    auto environment = AssetFormat::parseEnvironmentMapPayload(asset->payload());
    ASSERT_TRUE(environment.has_value()) << environment.error().message;
    EXPECT_EQ(environment->diffuseFaceSize, 1U);
    EXPECT_EQ(environment->brdfPixels.front(), std::byte{0x33});

    std::filesystem::remove_all(dir, ec);
}

TEST(CatalogCookTests, InlineAudioClipSineRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto clipId = *Core::AssetId::fromBytes(idBytes(4U));
    const auto clipHex = clipId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "audioclip ";
    recipe.append(clipHex.data(), clipHex.size());
    recipe += " 48000 1 64 sine 880\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::AudioClip);
    EXPECT_EQ(request->assets[0].assetId, clipId);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_audioclip";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 1U);

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, clipId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto clip = parseAudioClipFromCooked(*asset);
    ASSERT_TRUE(clip.has_value()) << clip.error().message;
    EXPECT_EQ(clip->sampleRate, 48000U);
    EXPECT_EQ(clip->frameCount, 64U);
    EXPECT_EQ(clip->channels, 1U);

    std::filesystem::remove_all(root, ec);
}

// M11-A20: cook PCM16 WAV file into AudioClip payload via recipe `file` form.
TEST(CatalogCookTests, AudioClipFileWavRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto clipId = *Core::AssetId::fromBytes(idBytes(7U));
    const auto clipHex = clipId.canonicalText();

    const auto dir = std::filesystem::temp_directory_path() / "tina_audioclip_wav_src";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto wavPath = dir / "click.wav";

    const auto wav = makeMonoPcm16Wav();
    ASSERT_TRUE(Core::writeFile(toUtf8(wavPath), wav).has_value());

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "audioclip ";
    recipe.append(clipHex.data(), clipHex.size());
    recipe += " file click.wav\n";

    auto request = parseCatalogCookRecipe(recipe, toUtf8(dir));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::AudioClip);

    const auto outRoot = dir / "pkg";
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(outRoot), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(outRoot), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    auto asset = loadCookedAssetFromCatalog(toUtf8(outRoot), *catalog, clipId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto clip = parseAudioClipFromCooked(*asset);
    ASSERT_TRUE(clip.has_value()) << clip.error().message;
    EXPECT_EQ(clip->sampleRate, 8000U);
    EXPECT_EQ(clip->frameCount, 8U);
    EXPECT_EQ(clip->channels, 1U);
    EXPECT_NEAR(clip->interleavedPcm[1], 8000.0F / 32768.0F, 1.0e-4F);

    std::filesystem::remove_all(dir, ec);
}

// M11-E1: cook canonical unit cube StaticMesh via recipe `staticmesh <id> cube`.
TEST(CatalogCookTests, StaticMeshCubeRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto meshId = *Core::AssetId::fromBytes(idBytes(9U));
    const auto meshHex = meshId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "staticmesh ";
    recipe.append(meshHex.data(), meshHex.size());
    recipe += " cube\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::StaticMesh);
    EXPECT_EQ(request->assets[0].assetId, meshId);

    const auto root = std::filesystem::temp_directory_path() / "tina_staticmesh_cube";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 1U);

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, meshId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto mesh = parseStaticMeshFromCooked(*asset);
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ(mesh->vertexCount, 24U);
    EXPECT_EQ(mesh->indexCount, 36U);
    EXPECT_EQ(mesh->submeshCount, 1U);

    std::filesystem::remove_all(root, ec);
}

// M11-E4: cook UnlitBaseColor Material via recipe `material <id> unlit r g b [a]`.
TEST(CatalogCookTests, MaterialUnlitRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto materialId = *Core::AssetId::fromBytes(idBytes(10U));
    const auto materialHex = materialId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "material ";
    recipe.append(materialHex.data(), materialHex.size());
    recipe += " unlit 0.95 0.24 0.30 1.0\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::Material);
    EXPECT_EQ(request->assets[0].assetId, materialId);

    const auto root = std::filesystem::temp_directory_path() / "tina_material_unlit";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 1U);

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, materialId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto material = parseMaterialFromCooked(*asset);
    ASSERT_TRUE(material.has_value()) << material.error().message;
    EXPECT_EQ(material->model, AssetFormat::MaterialModel::UnlitBaseColor);
    EXPECT_EQ(material->schemaVersion, AssetFormat::MaterialWire::SchemaVersion);
    EXPECT_FLOAT_EQ(material->baseColorR, 0.95F);
    EXPECT_FLOAT_EQ(material->baseColorG, 0.24F);
    EXPECT_FLOAT_EQ(material->baseColorB, 0.30F);
    EXPECT_FLOAT_EQ(material->baseColorA, 1.0F);
    EXPECT_FLOAT_EQ(material->metallicFactor, 1.0F);
    EXPECT_FLOAT_EQ(material->roughnessFactor, 1.0F);
    EXPECT_FALSE(material->hasBaseColorTexture);
    EXPECT_FALSE(material->hasMetallicRoughnessTexture);
    EXPECT_FALSE(material->hasNormalTexture);

    std::filesystem::remove_all(root, ec);
}

// M11-E5: material with optional Texture2D dependency.
TEST(CatalogCookTests, MaterialUnlitWithTextureRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(11U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(12U));
    const auto textureHex = textureId.canonicalText();
    const auto materialHex = materialId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "texture2d ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += " 2 2 FFFFFFFF 000000FF FFFFFFFF 000000FF\n";
    recipe += "material ";
    recipe.append(materialHex.data(), materialHex.size());
    recipe += " unlit 1 1 1 1 ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += "\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 2U);

    const auto root = std::filesystem::temp_directory_path() / "tina_material_tex";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, materialId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().dependencyCount, 1U);
    auto dep = asset->dependency(0);
    ASSERT_TRUE(dep.has_value());
    EXPECT_EQ(dep->assetId, textureId);

    auto material = parseMaterialFromCooked(*asset);
    ASSERT_TRUE(material.has_value()) << material.error().message;
    EXPECT_TRUE(material->hasBaseColorTexture);

    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace Tina::Asset
