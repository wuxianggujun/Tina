#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageChangeDetector.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options final {
    std::string outRoot;
    std::string recipePath;
    std::string gltfPath;
    std::string sourceRoot;
    std::string importStatePath;
};

struct PreCookProbe final {
    bool enabled = false;
    Tina::Asset::SourceImportProbeState state = Tina::Asset::SourceImportProbeState::NoBaseline;
    std::string_view reason = "disabled";
    Tina::Core::u32 cleanUnitCount = 0;
    Tina::Core::u32 cleanObjectCount = 0;
};

void printUsage()
{
    std::cerr
        << "tina_assetc --out <catalogRoot> [options]\n"
        << "  Fixture/recipe cooker for Catalog packages.\n"
        << "  --recipe <path>   cook from line recipe\n"
        << "  --gltf <path>     cook minimal glTF/GLB (cgltf) -> StaticMesh+Material+Prefab\n"
        << "  --source-root <path>  authoring root for canonical source provenance\n"
        << "  --import-state <path> atomically commit TINAIMPT state after package validation\n"
        << "  --help\n"
        << "\n"
        << "Default fixture (no --recipe): Texture2D 2x2 RGBA + Sprite full-UV.\n"
        << "Recipe lines:\n"
        << "  platform WindowsX64\n"
        << "  asset Texture2D <32hex> <payloadPath>\n"
        << "  asset Sprite <32hex> <payloadPath> <dep32hex:Texture2D>\n"
        << "  texture2d <32hex> <w> <h> <RRGGBBAA>...\n"
        << "  sprite <32hex> <texture32hex> [u0 v0 u1 v1 pivotX pivotY ppu]\n"
        << "  tileset <32hex> <texture32hex> <tilePxW> <tilePxH>\n"
        << "  tile <localId> <flags> <u0> <v0> <u1> <v1>\n"
        << "  tilemap <32hex> <tileset32hex> <w> <h> <cellSize>\n"
        << "  row <localId>...\n";
}

[[nodiscard]] int parseArgs(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg = argv[index];
        if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return 2;
        }
        auto requireValue = [&](std::string_view name) -> std::string_view {
            if (index + 1 >= argc)
            {
                std::cerr << "missing value for " << name << '\n';
                return {};
            }
            ++index;
            return argv[index];
        };
        if (arg == "--out")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.outRoot.assign(value);
            continue;
        }
        if (arg == "--recipe")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.recipePath.assign(value);
            continue;
        }
        if (arg == "--gltf")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.gltfPath.assign(value);
            continue;
        }
        if (arg == "--source-root")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.sourceRoot.assign(value);
            continue;
        }
        if (arg == "--import-state")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.importStatePath.assign(value);
            continue;
        }
        std::cerr << "unknown argument: " << arg << '\n';
        printUsage();
        return 2;
    }
    if (options.outRoot.empty())
    {
        std::cerr << "--out is required\n";
        printUsage();
        return 2;
    }
    if (!options.recipePath.empty() && !options.gltfPath.empty())
    {
        std::cerr << "--recipe and --gltf are mutually exclusive\n";
        return 2;
    }
    if (options.sourceRoot.empty() != options.importStatePath.empty())
    {
        std::cerr << "--source-root and --import-state must be provided together\n";
        return 2;
    }
    if (!options.importStatePath.empty() && options.recipePath.empty() && options.gltfPath.empty())
    {
        std::cerr << "--import-state requires --recipe or --gltf\n";
        return 2;
    }
    return 0;
}

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(Tina::Core::u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

void printError(const Tina::Core::Error& error)
{
    std::cout << "{\"status\":\"error\",\"domain\":" << static_cast<unsigned>(error.code.domain)
              << ",\"code\":" << error.code.value << ",\"message\":\"" << error.message << "\"}\n";
}

[[nodiscard]] constexpr std::string_view
probeStateName(Tina::Asset::SourceImportProbeState state) noexcept
{
    switch (state)
    {
    case Tina::Asset::SourceImportProbeState::Clean:
        return "clean";
    case Tina::Asset::SourceImportProbeState::Dirty:
        return "dirty";
    case Tina::Asset::SourceImportProbeState::NoBaseline:
        return "no-baseline";
    }
    return "unknown";
}

[[nodiscard]] Tina::Asset::CatalogPackageOpenConfig
catalogOpenConfig(std::pmr::memory_resource& memory, bool validateOnOpen,
                  bool verifyTypedPayload)
{
    return Tina::Asset::CatalogPackageOpenConfig{
        .manifest =
            Tina::Asset::CatalogFileLoadConfig{
                .catalog =
                    Tina::Asset::CatalogConfig{
                        .maxEntries = 1024,
                        .maxDependencies = 4096,
                        .maxDependenciesPerAsset = 64,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = validateOnOpen,
        .validation =
            Tina::Asset::CatalogPackageValidationConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = verifyTypedPayload,
            },
    };
}

[[nodiscard]] Tina::Core::Result<PreCookProbe>
probeExistingImport(const Options& options, std::pmr::memory_resource& memory)
{
    if (options.importStatePath.empty())
    {
        return PreCookProbe{};
    }

    auto revision = Tina::Asset::captureCatalogPackageRevision(
        options.outRoot,
        Tina::Asset::CatalogPackageChangeDetectorConfig{.scratchMemoryResource = &memory});
    if (!revision)
    {
        if (revision.error().code == Tina::Core::CoreErrorCode::NotFound)
        {
            return PreCookProbe{.enabled = true, .reason = "catalog-not-found"};
        }
        return Tina::Core::failure(std::move(revision.error()));
    }

    const Tina::AssetFormat::SourceImportManifestRevision importRevision{
        .manifestDigest = revision->manifestDigest,
        .manifestBytes = revision->manifestBytes,
    };
    Tina::Core::Result<Tina::Asset::SourceImportProbeResult> probe =
        options.gltfPath.empty()
            ? Tina::Asset::probeCatalogRecipeSourceImportState(
                  options.importStatePath, importRevision, options.sourceRoot, options.recipePath)
            : Tina::Asset::probeGltfSourceImportState(
                  options.importStatePath, importRevision, options.sourceRoot, options.gltfPath);
    if (!probe)
    {
        return Tina::Core::failure(std::move(probe.error()));
    }
    return PreCookProbe{
        .enabled = true,
        .state = probe->state,
        .reason = Tina::Asset::sourceImportProbeReasonName(probe->reason),
        .cleanUnitCount = probe->cleanUnitCount,
        .cleanObjectCount = probe->cleanObjectCount,
    };
}

void printSuccess(const Tina::Asset::CatalogSnapshot& catalog, std::string_view mode,
                  std::string_view cookMode, const PreCookProbe& probe,
                  Tina::Core::u32 unitsTotal, Tina::Core::u32 unitsRecooked,
                  Tina::Core::u32 objectsReused, Tina::Core::u32 objectsCooked,
                  bool importStateCommitted, std::string_view outRoot)
{
    std::cout << "{\"status\":\"ok\",\"tool\":\"tina_assetc\",\"entries\":"
              << catalog.entryCount() << ",\"dependencies\":" << catalog.dependencyCount()
              << ",\"mode\":\"" << mode << "\",\"cookMode\":\"" << cookMode
              << "\",\"probe\":\"" << (probe.enabled ? probeStateName(probe.state) : "disabled")
              << "\",\"probeReason\":\"" << probe.reason << "\",\"unitsTotal\":"
              << unitsTotal << ",\"unitsRecooked\":" << unitsRecooked
              << ",\"objectsReused\":" << objectsReused << ",\"objectsCooked\":"
              << objectsCooked << ",\"importStateCommitted\":"
              << (importStateCommitted ? "true" : "false") << ",\"out\":\"" << outRoot
              << "\"}\n";
}

[[nodiscard]] Tina::Core::Result<Tina::Asset::CatalogCookRequest> buildTyped2dRequest()
{
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Tina::Core::AssetId::fromBytes(idBytes(3U));

    // 2x2 checkerboard RGBA
    std::vector<std::byte> pixels{
        std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255}, // R
        std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255}, // G
        std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255}, // B
        std::byte{255}, std::byte{255}, std::byte{0},   std::byte{255}, // Y
    };
    auto texPayload = Tina::AssetFormat::writeTexture2DPayloadBytes(Tina::AssetFormat::Texture2DPayloadDesc{
        .width = 2,
        .height = 2,
        .pixelFormat = Tina::AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
        .pixels = pixels,
    });
    if (!texPayload)
    {
        return Tina::Core::failure(std::move(texPayload.error()));
    }
    auto spritePayload = Tina::AssetFormat::writeSpritePayloadBytes(Tina::AssetFormat::SpritePayloadDesc{
        .u0 = 0.0f,
        .v0 = 0.0f,
        .u1 = 1.0f,
        .v1 = 1.0f,
        .pivotX = 0.5f,
        .pivotY = 0.5f,
        .pixelsPerUnit = 32.0f,
        .textureId = textureId,
    });
    if (!spritePayload)
    {
        return Tina::Core::failure(std::move(spritePayload.error()));
    }

    Tina::Asset::CatalogCookRequest request{.targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .assetTypeVersion = Tina::AssetFormat::Texture2DWire::SchemaVersion,
        .payload = std::move(*texPayload),
    });
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Sprite,
        .assetId = spriteId,
        .assetTypeVersion = Tina::AssetFormat::SpriteWire::SchemaVersion,
        .payload = std::move(*spritePayload),
        .dependencies =
            {
                Tina::AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = Tina::AssetFormat::AssetKind::Texture2D,
                    .flags = Tina::AssetFormat::DependencyFlags::Required,
                },
            },
    });
    return request;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (const int parseResult = parseArgs(argc, argv, options); parseResult != 0)
    {
        return parseResult == 2 ? 2 : 1;
    }

    std::string mode = options.gltfPath.empty() ? (options.recipePath.empty() ? "typed2d" : "recipe")
                                                : "gltf";
    std::pmr::unsynchronized_pool_resource memory;
    auto preCookProbe = probeExistingImport(options, memory);
    if (!preCookProbe)
    {
        printError(preCookProbe.error());
        return 1;
    }
    if (preCookProbe->state == Tina::Asset::SourceImportProbeState::Clean)
    {
        auto catalog = Tina::Asset::openCatalogPackage(
            options.outRoot, catalogOpenConfig(memory, false, options.recipePath.empty()));
        if (!catalog)
        {
            printError(catalog.error());
            return 1;
        }
        if (catalog->entryCount() == preCookProbe->cleanObjectCount)
        {
            printSuccess(*catalog, mode, "clean-reuse", *preCookProbe,
                         preCookProbe->cleanUnitCount, 0U, catalog->entryCount(), 0U, false,
                         options.outRoot);
            return 0;
        }
        preCookProbe->state = Tina::Asset::SourceImportProbeState::Dirty;
        preCookProbe->reason = "catalog-output-count-changed";
        preCookProbe->cleanUnitCount = 0U;
        preCookProbe->cleanObjectCount = 0U;
    }

    Tina::Core::Result<Tina::Asset::CatalogCookRequest> request =
        Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig, "no request");
    std::optional<Tina::Asset::SourceImportCandidate> sourceImports;
    if (!options.gltfPath.empty())
    {
        if (options.importStatePath.empty())
        {
            request = Tina::Asset::cookGltfFileToCatalogRequest(options.gltfPath);
        }
        else
        {
            auto sourceResult = Tina::Asset::cookGltfFileToCatalogSourceResult(
                options.gltfPath,
                Tina::Asset::SourceImportCaptureConfig{.sourceRootUtf8 = options.sourceRoot});
            if (!sourceResult)
            {
                printError(sourceResult.error());
                return 1;
            }
            request = std::move(sourceResult->request);
            sourceImports.emplace(std::move(sourceResult->sourceImports));
        }
    } else if (!options.recipePath.empty())
    {
        if (options.importStatePath.empty())
        {
            request = Tina::Asset::loadCatalogCookRecipeFile(options.recipePath);
        }
        else
        {
            auto sourceResult = Tina::Asset::loadCatalogCookRecipeSourceFile(
                options.recipePath,
                Tina::Asset::SourceImportCaptureConfig{.sourceRootUtf8 = options.sourceRoot});
            if (!sourceResult)
            {
                printError(sourceResult.error());
                return 1;
            }
            request = std::move(sourceResult->request);
            sourceImports.emplace(std::move(sourceResult->sourceImports));
        }
    } else
    {
        request = buildTyped2dRequest();
    }
    if (!request)
    {
        printError(request.error());
        return 1;
    }

    if (const auto status = Tina::Asset::cookAndPublishCatalogPackage(options.outRoot, *request); !status)
    {
        printError(status.error());
        return 1;
    }

    auto catalog = Tina::Asset::openCatalogPackage(
        options.outRoot, catalogOpenConfig(memory, true, options.recipePath.empty()));
    if (!catalog)
    {
        printError(catalog.error());
        return 1;
    }

    if (sourceImports)
    {
        auto revision = Tina::Asset::captureCatalogPackageRevision(
            options.outRoot,
            Tina::Asset::CatalogPackageChangeDetectorConfig{.scratchMemoryResource = &memory});
        if (!revision)
        {
            printError(revision.error());
            return 1;
        }
        const auto stateStatus = Tina::Asset::commitSourceImportCandidate(
            options.importStatePath,
            *sourceImports,
            Tina::AssetFormat::SourceImportManifestRevision{
                .manifestDigest = revision->manifestDigest,
                .manifestBytes = revision->manifestBytes,
            });
        if (!stateStatus)
        {
            printError(stateStatus.error());
            return 1;
        }
    }

    const auto unitsCooked = sourceImports ? static_cast<Tina::Core::u32>(sourceImports->units.size()) : 0U;
    printSuccess(*catalog, mode, "full-recook", *preCookProbe, unitsCooked, unitsCooked, 0U,
                 catalog->entryCount(), sourceImports.has_value(), options.outRoot);
    return 0;
}
