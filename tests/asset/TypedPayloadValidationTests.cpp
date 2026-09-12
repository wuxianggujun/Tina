#include <tina/asset/CatalogCook.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/EnvironmentMapPayload.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory_resource>
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

TEST(TypedPayloadValidationTests, MaterialRequiresCurrentOuterSchemaAndExactRequiredTextureRoles)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto materialId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto textureId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto extraId = *Core::AssetId::fromBytes(idBytes(3U));
    const auto payload = AssetFormat::writeMaterialPayloadBytes(AssetFormat::MaterialPayloadDesc{
        .alphaMode = AssetFormat::MaterialAlphaMode::Mask,
        .alphaCutoff = 0.25F,
        .emissiveFactorR = 4.0F,
        .emissiveTextureId = textureId,
    });
    ASSERT_TRUE(payload) << payload.error().message;
    const std::array valid{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    auto wrongKind = valid;
    wrongKind[0].expectedKind = AssetFormat::AssetKind::StaticMesh;
    auto invalidFlags = valid;
    invalidFlags[0].flags = AssetFormat::DependencyFlags::None;
    const std::array extra{
        valid[0],
        AssetFormat::CookedAssetWriteDependency{
            .assetId = extraId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    struct Case final {
        Core::u16 version;
        std::span<const AssetFormat::CookedAssetWriteDependency> dependencies;
        bool writable;
        bool accepted;
    };
    const std::array cases{
        Case{AssetFormat::MaterialWire::SchemaVersion, valid, true, true},
        Case{2U, valid, true, false},
        Case{AssetFormat::MaterialWire::SchemaVersion, {}, true, false},
        Case{AssetFormat::MaterialWire::SchemaVersion, wrongKind, true, false},
        Case{AssetFormat::MaterialWire::SchemaVersion, invalidFlags, false, false},
        Case{AssetFormat::MaterialWire::SchemaVersion, extra, true, false},
    };
    for (Tina::Core::usize index = 0; index < cases.size(); ++index)
    {
        SCOPED_TRACE(index);
        const auto& test = cases[index];
        auto bytes = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
            .assetKind = AssetFormat::AssetKind::Material,
            .assetTypeVersion = test.version,
            .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
            .assetId = materialId,
            .dependencies = test.dependencies,
            .payload = *payload,
            .payloadAlignment = 4,
            .computeContentHash = true,
        });
        if (!test.writable)
        {
            ASSERT_FALSE(bytes);
            EXPECT_EQ(bytes.error().code, AssetFormat::AssetFormatErrorCode::InvalidIdentity);
            continue;
        }
        ASSERT_TRUE(bytes) << bytes.error().message;
        auto file = makeCookedAssetFileFromBytes(
            std::pmr::vector<std::byte>{bytes->begin(), bytes->end(), &memory},
            CookedAssetFileLoadConfig{.memoryResource = &memory});
        ASSERT_TRUE(file) << file.error().message;
        auto view = parseMaterialFromCooked(*file);
        if (test.accepted)
        {
            ASSERT_TRUE(view) << view.error().message;
            EXPECT_TRUE(view->hasEmissiveTexture);
            EXPECT_FLOAT_EQ(view->alphaCutoff, 0.25F);
            EXPECT_FLOAT_EQ(view->emissiveFactorR, 4.0F);
        }
        else
        {
            ASSERT_FALSE(view);
            EXPECT_EQ(view.error().code, AssetErrorCode::CatalogEntryMismatch);
        }
    }
}

TEST(TypedPayloadValidationTests, AcceptsTypedTextureWhenEnabled)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    std::vector<std::byte> pixels(4, std::byte{9});
    auto payload = AssetFormat::writeTexture2DPayloadBytesRgba8(1, 1, pixels);
    ASSERT_TRUE(payload.has_value());

    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .assetTypeVersion = AssetFormat::Texture2DWire::SchemaVersion,
        .payload = std::move(*payload),
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed_validate_ok";
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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    std::filesystem::remove_all(root, ec);
}

TEST(TypedPayloadValidationTests, RejectsRawTextureWhenTypedRequired)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = {std::byte{'r'}, std::byte{'a'}, std::byte{'w'}},
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed_validate_bad";
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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_FALSE(catalog.has_value());

    // Without typed check, raw payload still opens.
    openConfig.validation.verifyTypedPayload = false;
    auto catalogRaw = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalogRaw.has_value()) << catalogRaw.error().message;

    std::filesystem::remove_all(root, ec);
}

TEST(TypedPayloadValidationTests, RejectsMalformedEnvironmentMapWhenTypedRequired)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto environmentMapId = *Core::AssetId::fromBytes(idBytes(9U));
    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::EnvironmentMap,
        .assetId = environmentMapId,
        .assetTypeVersion = AssetFormat::EnvironmentMapWire::SchemaVersion,
        .payload = std::vector<std::byte>(AssetFormat::EnvironmentMapWire::HeaderBytes, std::byte{0}),
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed_validate_environment_map_bad";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), request).has_value());

    auto catalog = openCatalogPackage(
        toUtf8(root),
        CatalogPackageOpenConfig{
            .manifest = CatalogFileLoadConfig{.catalog = CatalogConfig{.maxEntries = 8,
                                                                        .maxDependencies = 8,
                                                                        .maxDependenciesPerAsset = 4,
                                                                        .memoryResource = &memory}},
            .validateOnOpen = true,
            .validation = CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
        });
    ASSERT_FALSE(catalog.has_value());
    ASSERT_FALSE(catalog.error().message.empty());
    const auto typedContext = std::find_if(
        catalog.error().context.begin(), catalog.error().context.end(),
        [](const Core::ErrorContext& context) {
            return context.operation == "validateCatalogPackage";
        });
    ASSERT_NE(typedContext, catalog.error().context.end());
    EXPECT_EQ(typedContext->detail, "typedEnvironmentMap");

    std::filesystem::remove_all(root, ec);
}

TEST(TypedPayloadValidationTests, RejectsPrefabDependencySetMissingPayloadReferences)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto prefabId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto meshId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(3U));
    const std::array nodes{AssetFormat::PrefabNodeDesc{
        .stableNodeId = 1,
        .nodeKind = AssetFormat::PrefabNodeKind::Mesh3D,
        .meshId = meshId,
        .materialId = materialId,
    }};
    auto payload = AssetFormat::writePrefabPayloadBytes(AssetFormat::PrefabPayloadDesc{.nodes = nodes});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;

    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Prefab,
        .assetId = prefabId,
        .assetTypeVersion = AssetFormat::PrefabWire::SchemaVersion,
        .payload = std::move(*payload),
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed_validate_prefab_deps";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), request).has_value());

    auto catalog = openCatalogPackage(
        toUtf8(root),
        CatalogPackageOpenConfig{
            .manifest = CatalogFileLoadConfig{.catalog = CatalogConfig{.maxEntries = 8,
                                                                        .maxDependencies = 8,
                                                                        .maxDependenciesPerAsset = 4,
                                                                        .memoryResource = &memory}},
            .validateOnOpen = true,
            .validation = CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
        });
    ASSERT_FALSE(catalog.has_value());
    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace Tina::Asset
