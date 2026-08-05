#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageChangeDetector.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset/SourceImportExecutor.hpp>
#include <tina/asset/SourceImportPlan.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class ImportKind : Tina::Core::u8 {
    Recipe = 0,
    Gltf = 1,
};

struct ImportOption final {
    ImportKind kind = ImportKind::Recipe;
    std::string path{};
};

struct Options final {
    std::string outRoot;
    std::vector<ImportOption> imports{};
    std::string sourceRoot;
    std::string importStatePath;
    std::string stageOutRoot;
    std::string stageImportStatePath;
};

struct PreCookProbe final {
    bool enabled = false;
    Tina::Asset::SourceImportProbeState state = Tina::Asset::SourceImportProbeState::NoBaseline;
    std::string_view reason = "disabled";
    Tina::Core::u32 cleanUnitCount = 0;
    Tina::Core::u32 cleanObjectCount = 0;
};

[[nodiscard]] bool pathComponentEquals(const std::filesystem::path& left,
                                       const std::filesystem::path& right) noexcept
{
#if defined(_WIN32)
    const auto& leftText = left.native();
    const auto& rightText = right.native();
    return leftText.size() == rightText.size() &&
           std::equal(leftText.begin(), leftText.end(), rightText.begin(),
                      [](wchar_t leftCharacter, wchar_t rightCharacter) {
                          return std::towlower(leftCharacter) == std::towlower(rightCharacter);
                      });
#else
    return left == right;
#endif
}

[[nodiscard]] bool pathIsSameOrDescendant(const std::filesystem::path& candidate,
                                          const std::filesystem::path& ancestor) noexcept
{
    auto candidatePart = candidate.begin();
    for (auto ancestorPart = ancestor.begin(); ancestorPart != ancestor.end();
         ++ancestorPart, ++candidatePart)
    {
        if (candidatePart == candidate.end() || !pathComponentEquals(*candidatePart, *ancestorPart))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::filesystem::path> resolveOptionPath(std::string_view utf8Path)
{
    try
    {
        std::u8string text;
        text.reserve(utf8Path.size());
        for (const char byte : utf8Path)
        {
            text.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
        }
        std::error_code errorCode;
        auto resolved = std::filesystem::weakly_canonical(std::filesystem::path{std::move(text)},
                                                          errorCode);
        if (errorCode)
        {
            std::cerr << "failed to resolve path: " << errorCode.message() << '\n';
            return std::nullopt;
        }
        return resolved.lexically_normal();
    } catch (const std::filesystem::filesystem_error& exception)
    {
        std::cerr << "failed to resolve path: " << exception.code().message() << '\n';
        return std::nullopt;
    } catch (const std::bad_alloc&)
    {
        std::cerr << "failed to resolve path: allocation failed\n";
        return std::nullopt;
    }
}

[[nodiscard]] bool validateOptionPaths(const Options& options)
{
    const auto liveRoot = resolveOptionPath(options.outRoot);
    if (!liveRoot)
    {
        return false;
    }

    std::optional<std::filesystem::path> stageRoot;
    if (!options.stageOutRoot.empty())
    {
        stageRoot = resolveOptionPath(options.stageOutRoot);
        if (!stageRoot)
        {
            return false;
        }
        if (pathIsSameOrDescendant(*stageRoot, *liveRoot))
        {
            std::cerr << "--stage-out must be outside --out\n";
            return false;
        }
    }

    std::optional<std::filesystem::path> baselineState;
    if (!options.importStatePath.empty())
    {
        baselineState = resolveOptionPath(options.importStatePath);
        if (!baselineState)
        {
            return false;
        }
        if (pathIsSameOrDescendant(*baselineState, *liveRoot))
        {
            std::cerr << "--import-state must be outside --out\n";
            return false;
        }
    }

    if (!options.stageImportStatePath.empty())
    {
        const auto stageState = resolveOptionPath(options.stageImportStatePath);
        if (!stageState)
        {
            return false;
        }
        if ((baselineState && pathIsSameOrDescendant(*stageState, *baselineState) &&
             pathIsSameOrDescendant(*baselineState, *stageState)) ||
            pathIsSameOrDescendant(*stageState, *liveRoot) ||
            (stageRoot && pathIsSameOrDescendant(*stageState, *stageRoot)))
        {
            std::cerr << "--stage-import-state must be independent of package roots and baseline state\n";
            return false;
        }
        std::error_code errorCode;
        const bool exists = std::filesystem::exists(*stageState, errorCode);
        if (errorCode)
        {
            std::cerr << "failed to query --stage-import-state: " << errorCode.message() << '\n';
            return false;
        }
        if (exists)
        {
            std::cerr << "--stage-import-state must not already exist\n";
            return false;
        }
    }
    return true;
}

void printUsage()
{
    std::cerr
        << "tina_assetc --out <catalogRoot> [options]\n"
        << "  Fixture/recipe cooker for Catalog packages.\n"
        << "  --recipe <path>   cook from line recipe\n"
        << "  --gltf <path>     cook minimal glTF/GLB (cgltf) -> StaticMesh+Material+Prefab\n"
        << "                    --recipe/--gltf may be repeated to form one import batch\n"
        << "  --source-root <path>  authoring root for canonical source provenance\n"
        << "  --import-state <path> commit TINAIMPT state after fresh package validation\n"
        << "  --stage-out <path> fresh candidate root when --out already has a baseline\n"
        << "  --stage-import-state <path> state file bound to --stage-out\n"
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
            options.imports.push_back(ImportOption{.kind = ImportKind::Recipe,
                                                   .path = std::string(value)});
            continue;
        }
        if (arg == "--gltf")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.imports.push_back(ImportOption{.kind = ImportKind::Gltf,
                                                   .path = std::string(value)});
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
        if (arg == "--stage-out")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.stageOutRoot.assign(value);
            continue;
        }
        if (arg == "--stage-import-state")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.stageImportStatePath.assign(value);
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
    if (options.sourceRoot.empty() != options.importStatePath.empty())
    {
        std::cerr << "--source-root and --import-state must be provided together\n";
        return 2;
    }
    if (!options.importStatePath.empty() && options.imports.empty())
    {
        std::cerr << "--import-state requires --recipe or --gltf\n";
        return 2;
    }
    if (options.stageOutRoot.empty() && !options.stageImportStatePath.empty())
    {
        std::cerr << "--stage-import-state requires --stage-out\n";
        return 2;
    }
    if (!options.importStatePath.empty() &&
        (options.stageOutRoot.empty() != options.stageImportStatePath.empty()))
    {
        std::cerr << "source import staging requires --stage-out and --stage-import-state together\n";
        return 2;
    }
    if (options.importStatePath.empty() && !options.stageImportStatePath.empty())
    {
        std::cerr << "--stage-import-state requires source import state mode\n";
        return 2;
    }
    if (options.stageOutRoot == options.outRoot && !options.stageOutRoot.empty())
    {
        std::cerr << "--stage-out must differ from --out\n";
        return 2;
    }
    if (options.stageImportStatePath == options.importStatePath &&
        !options.stageImportStatePath.empty())
    {
        std::cerr << "--stage-import-state must differ from --import-state\n";
        return 2;
    }
    if (!validateOptionPaths(options))
    {
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

struct LoadedBaseline final {
    std::pmr::vector<std::byte> stateBytes{};
    Tina::AssetFormat::SourceImportMetadataView metadata{};
    Tina::Asset::CatalogSnapshot catalog{};
    Tina::AssetFormat::SourceImportManifestRevision revision{};
};

struct BaselineLoadResult final {
    std::optional<LoadedBaseline> baseline{};
    PreCookProbe probe{};
    bool catalogPresent = false;
};

[[nodiscard]] bool verifyTypedPayloads(const Options& options) noexcept
{
    return std::none_of(options.imports.begin(), options.imports.end(), [](const ImportOption& input) {
        return input.kind == ImportKind::Recipe;
    });
}

[[nodiscard]] std::string_view modeName(const Options& options) noexcept
{
    if (options.imports.empty())
    {
        return "typed2d";
    }
    if (options.imports.size() > 1U)
    {
        return "batch";
    }
    return options.imports.front().kind == ImportKind::Recipe ? "recipe" : "gltf";
}

[[nodiscard]] Tina::Core::Result<BaselineLoadResult>
loadBaseline(const Options& options, std::pmr::memory_resource& memory)
{
    if (options.importStatePath.empty())
    {
        return BaselineLoadResult{};
    }
    auto revision = Tina::Asset::captureCatalogPackageRevision(
        options.outRoot,
        Tina::Asset::CatalogPackageChangeDetectorConfig{.scratchMemoryResource = &memory});
    if (!revision)
    {
        if (revision.error().code == Tina::Core::CoreErrorCode::NotFound)
        {
            return BaselineLoadResult{
                .probe = PreCookProbe{.enabled = true,
                                      .state = Tina::Asset::SourceImportProbeState::NoBaseline,
                                      .reason = "catalog-not-found"},
            };
        }
        return Tina::Core::failure(std::move(revision.error()));
    }

    const Tina::AssetFormat::SourceImportManifestRevision importRevision{
        .manifestDigest = revision->manifestDigest,
        .manifestBytes = revision->manifestBytes,
    };
    auto catalog = Tina::Asset::openCatalogPackage(
        options.outRoot, catalogOpenConfig(memory, false, verifyTypedPayloads(options)));
    if (!catalog)
    {
        return Tina::Core::failure(std::move(catalog.error()));
    }

    auto stateBytes = Tina::Core::readFile(
        options.importStatePath,
        Tina::Core::ReadFileConfig{.maxBytes = Tina::Core::MaxReadFileBytes,
                                   .memoryResource = &memory});
    if (!stateBytes)
    {
        if (stateBytes.error().code == Tina::Core::CoreErrorCode::NotFound)
        {
            return BaselineLoadResult{
                .probe = PreCookProbe{.enabled = true,
                                      .state = Tina::Asset::SourceImportProbeState::NoBaseline,
                                      .reason = "state-not-found"},
                .catalogPresent = true,
            };
        }
        return Tina::Core::failure(std::move(stateBytes.error()));
    }
    auto metadata = Tina::AssetFormat::parseSourceImportMetadataView(*stateBytes);
    if (!metadata)
    {
        if (metadata.error().code == Tina::AssetFormat::AssetFormatErrorCode::UnsupportedSchema)
        {
            return BaselineLoadResult{
                .probe = PreCookProbe{.enabled = true,
                                      .state = Tina::Asset::SourceImportProbeState::Dirty,
                                      .reason = "state-schema-changed"},
                .catalogPresent = true,
            };
        }
        return Tina::Core::failure(std::move(metadata.error()));
    }
    if (const auto status = Tina::Asset::validateSourceImportCatalogBinding(*metadata, importRevision);
        !status)
    {
        return BaselineLoadResult{
            .probe = PreCookProbe{.enabled = true,
                                  .state = Tina::Asset::SourceImportProbeState::Dirty,
                                  .reason = "catalog-revision-changed"},
            .catalogPresent = true,
        };
    }
    if (const auto status = Tina::Asset::validateSourceImportCatalogOutputs(*metadata, *catalog);
        !status)
    {
        return BaselineLoadResult{
            .probe = PreCookProbe{.enabled = true,
                                  .state = Tina::Asset::SourceImportProbeState::Dirty,
                                  .reason = "catalog-output-changed"},
            .catalogPresent = true,
        };
    }

    return BaselineLoadResult{
        .baseline = LoadedBaseline{
            .stateBytes = std::move(*stateBytes),
            .metadata = *metadata,
            .catalog = std::move(*catalog),
            .revision = importRevision,
        },
        .probe = PreCookProbe{.enabled = true,
                              .state = Tina::Asset::SourceImportProbeState::Dirty,
                              .reason = "not-probed"},
        .catalogPresent = true,
    };
}

void printSuccess(const Tina::Asset::CatalogSnapshot& catalog, std::string_view mode,
                  std::string_view cookMode, const PreCookProbe& probe,
                  Tina::Core::u32 unitsTotal, Tina::Core::u32 unitsRecooked,
                  Tina::Core::u32 unitsRemoved,
                  Tina::Core::u32 objectsReused, Tina::Core::u32 objectsCooked,
                  bool importStateCommitted, std::string_view outRoot)
{
    std::cout << "{\"status\":\"ok\",\"tool\":\"tina_assetc\",\"entries\":"
              << catalog.entryCount() << ",\"dependencies\":" << catalog.dependencyCount()
              << ",\"mode\":\"" << mode << "\",\"cookMode\":\"" << cookMode
              << "\",\"probe\":\"" << (probe.enabled ? probeStateName(probe.state) : "disabled")
              << "\",\"probeReason\":\"" << probe.reason << "\",\"unitsTotal\":"
              << unitsTotal << ",\"unitsRecooked\":" << unitsRecooked
              << ",\"unitsRemoved\":" << unitsRemoved
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

[[nodiscard]] Tina::Core::Status
appendCookRequest(Tina::Asset::CatalogCookRequest& combined,
                  bool& hasTargetPlatform,
                  Tina::Asset::CatalogCookRequest source)
{
    if (!hasTargetPlatform)
    {
        combined.targetPlatform = source.targetPlatform;
        hasTargetPlatform = true;
    }
    else if (combined.targetPlatform != source.targetPlatform)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                                   "source import units target different platforms");
    }
    combined.assets.reserve(combined.assets.size() + source.assets.size());
    for (auto& asset : source.assets)
    {
        combined.assets.push_back(std::move(asset));
    }
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Result<Tina::Asset::CatalogCookRequest>
cookImportRequest(const ImportOption& input)
{
    return input.kind == ImportKind::Recipe
               ? Tina::Asset::loadCatalogCookRecipeFile(input.path)
               : Tina::Asset::cookGltfFileToCatalogRequest(input.path);
}

[[nodiscard]] Tina::Core::Result<Tina::Asset::CatalogCookSourceResult>
cookSourceImport(const ImportOption& input, std::string_view sourceRoot)
{
    const Tina::Asset::SourceImportCaptureConfig capture{.sourceRootUtf8 = sourceRoot};
    return input.kind == ImportKind::Recipe
               ? Tina::Asset::loadCatalogCookRecipeSourceFile(input.path, capture)
               : Tina::Asset::cookGltfFileToCatalogSourceResult(input.path, capture);
}

[[nodiscard]] Tina::Core::Result<Tina::Asset::SourceImportUnitProbeDesc>
makeProbeDesc(const ImportOption& input, std::string_view sourceRoot,
              Tina::AssetFormat::TargetPlatform targetPlatform)
{
    return input.kind == ImportKind::Recipe
               ? Tina::Asset::makeCatalogRecipeSourceImportProbeDesc(
                     sourceRoot, input.path, targetPlatform)
               : Tina::Asset::makeGltfSourceImportProbeDesc(
                     sourceRoot, input.path, Tina::Asset::GltfCookIds{});
}

[[nodiscard]] Tina::Asset::CatalogPackageStageConfig
stageConfig(std::pmr::memory_resource& memory, bool verifyTypedPayload)
{
    return Tina::Asset::CatalogPackageStageConfig{
        .validation = catalogOpenConfig(memory, false, verifyTypedPayload),
    };
}

[[nodiscard]] Tina::Core::Status
commitImportState(std::string_view root, std::string_view statePath,
                  const Tina::Asset::SourceImportCandidate& candidate,
                  std::pmr::memory_resource& memory)
{
    auto revision = Tina::Asset::captureCatalogPackageRevision(
        root, Tina::Asset::CatalogPackageChangeDetectorConfig{.scratchMemoryResource = &memory});
    if (!revision)
    {
        return Tina::Core::failure(std::move(revision.error()));
    }
    return Tina::Asset::commitSourceImportCandidate(
        statePath, candidate,
        Tina::AssetFormat::SourceImportManifestRevision{
            .manifestDigest = revision->manifestDigest,
            .manifestBytes = revision->manifestBytes,
        });
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (const int parseResult = parseArgs(argc, argv, options); parseResult != 0)
    {
        return parseResult == 2 ? 2 : 1;
    }

    const std::string_view mode = modeName(options);
    std::pmr::unsynchronized_pool_resource memory;

    if (options.importStatePath.empty())
    {
        Tina::Core::Result<Tina::Asset::CatalogCookRequest> request =
            Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig, "no request");
        if (options.imports.empty())
        {
            request = buildTyped2dRequest();
        }
        else
        {
            Tina::Asset::CatalogCookRequest combined{};
            bool hasTargetPlatform = false;
            for (const auto& input : options.imports)
            {
                auto unit = cookImportRequest(input);
                if (!unit)
                {
                    printError(unit.error());
                    return 1;
                }
                if (const auto status = appendCookRequest(combined, hasTargetPlatform,
                                                          std::move(*unit));
                    !status)
                {
                    printError(status.error());
                    return 1;
                }
            }
            request = std::move(combined);
        }
        if (!request)
        {
            printError(request.error());
            return 1;
        }
        const std::string_view targetRoot = options.stageOutRoot.empty()
                                                ? std::string_view(options.outRoot)
                                                : std::string_view(options.stageOutRoot);
        auto staged = Tina::Asset::cookAndStageCatalogPackage(
            targetRoot, *request, stageConfig(memory, verifyTypedPayloads(options)));
        if (!staged)
        {
            printError(staged.error());
            return 1;
        }
        const auto unitCount = static_cast<Tina::Core::u32>(options.imports.size());
        printSuccess(*staged, mode, "full-recook", PreCookProbe{}, unitCount, unitCount,
                     0U, 0U, staged->entryCount(), false, targetRoot);
        return 0;
    }

    auto loaded = loadBaseline(options, memory);
    if (!loaded)
    {
        printError(loaded.error());
        return 1;
    }

    if (loaded->baseline)
    {
        auto& baseline = *loaded->baseline;
        std::vector<Tina::Asset::SourceImportUnitProbeDesc> descriptions;
        descriptions.reserve(options.imports.size());
        for (const auto& input : options.imports)
        {
            auto desc = makeProbeDesc(input, options.sourceRoot,
                                      baseline.metadata.header().targetPlatform);
            if (!desc)
            {
                printError(desc.error());
                return 1;
            }
            descriptions.push_back(std::move(*desc));
        }
        auto batch = Tina::Asset::probeSourceImportUnits(
            baseline.metadata, baseline.revision, descriptions);
        if (!batch)
        {
            printError(batch.error());
            return 1;
        }

        loaded->probe.cleanUnitCount = batch->cleanUnitCount;
        loaded->probe.cleanObjectCount = batch->cleanObjectCount;
        if (batch->removedUnitCount != 0U)
        {
            loaded->probe.state = Tina::Asset::SourceImportProbeState::Dirty;
            loaded->probe.reason = "unit-set-changed";
        }
        else if (batch->dirtyUnitCount == 0U)
        {
            loaded->probe.state = Tina::Asset::SourceImportProbeState::Clean;
            loaded->probe.reason = "none";
            printSuccess(baseline.catalog, mode, "clean-reuse", loaded->probe,
                         static_cast<Tina::Core::u32>(options.imports.size()), 0U, 0U,
                         baseline.catalog.entryCount(), 0U, false, options.outRoot);
            return 0;
        }
        else
        {
            loaded->probe.state = Tina::Asset::SourceImportProbeState::Dirty;
            const auto dirty = std::find_if(batch->units.begin(), batch->units.end(), [](const auto& unit) {
                return unit.state != Tina::Asset::SourceImportProbeState::Clean;
            });
            loaded->probe.reason = dirty != batch->units.end()
                                       ? Tina::Asset::sourceImportProbeReasonName(dirty->reason)
                                       : "unit-set-changed";
        }

        if (options.stageOutRoot.empty())
        {
            printError(Tina::Core::Error{
                Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                "dirty source import requires fresh --stage-out and --stage-import-state"});
            return 1;
        }

        Tina::Asset::CatalogCookRequest dirtyRequest{};
        bool hasTargetPlatform = false;
        if (batch->cleanUnitCount != 0U)
        {
            dirtyRequest.targetPlatform = baseline.metadata.header().targetPlatform;
            hasTargetPlatform = true;
        }
        std::vector<Tina::Asset::SourceImportCandidate> recookedCandidates;
        std::vector<Tina::AssetFormat::SourceImportUnitId> retainedUnitIds;
        recookedCandidates.reserve(batch->dirtyUnitCount);
        retainedUnitIds.reserve(batch->cleanUnitCount);
        for (Tina::Core::usize index = 0; index < options.imports.size(); ++index)
        {
            if (batch->units[index].state == Tina::Asset::SourceImportProbeState::Clean)
            {
                retainedUnitIds.push_back(descriptions[index].expected.unitId);
                continue;
            }

            auto unit = cookSourceImport(options.imports[index], options.sourceRoot);
            if (!unit)
            {
                printError(unit.error());
                return 1;
            }
            if (const auto status = appendCookRequest(dirtyRequest, hasTargetPlatform,
                                                      std::move(unit->request));
                !status)
            {
                printError(status.error());
                return 1;
            }
            recookedCandidates.push_back(std::move(unit->sourceImports));
        }
        auto composed = Tina::Asset::composeSourceImportCandidate(
            Tina::Asset::SourceImportCandidateComposeDesc{
                .baseline = &baseline.metadata,
                .retainedUnitIds = retainedUnitIds,
                .recookedCandidates = recookedCandidates,
            });
        if (!composed)
        {
            printError(composed.error());
            return 1;
        }
        auto staged = Tina::Asset::cookAndStageIncrementalCatalogPackage(
            options.stageOutRoot, options.outRoot, baseline.catalog,
            composed->retainedAssetIds, dirtyRequest,
            stageConfig(memory, verifyTypedPayloads(options)));
        if (!staged)
        {
            printError(staged.error());
            return 1;
        }
        if (const auto status = commitImportState(options.stageOutRoot,
                                                  options.stageImportStatePath,
                                                  composed->candidate, memory);
            !status)
        {
            printError(status.error());
            return 1;
        }

        const auto cookMode = batch->cleanUnitCount != 0U ? "incremental-recook" : "full-recook";
        printSuccess(*staged, mode, cookMode, loaded->probe,
                     static_cast<Tina::Core::u32>(options.imports.size()),
                     batch->dirtyUnitCount, batch->removedUnitCount,
                     static_cast<Tina::Core::u32>(composed->retainedAssetIds.size()),
                     static_cast<Tina::Core::u32>(dirtyRequest.assets.size()), true,
                     options.stageOutRoot);
        return 0;
    }

    if (loaded->catalogPresent && options.stageOutRoot.empty())
    {
        printError(Tina::Core::Error{
            Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
            "full recook of an existing baseline requires fresh --stage-out and --stage-import-state"});
        return 1;
    }

    Tina::Asset::CatalogCookRequest request{};
    bool hasTargetPlatform = false;
    std::vector<Tina::Asset::SourceImportCandidate> candidates;
    candidates.reserve(options.imports.size());
    for (const auto& input : options.imports)
    {
        auto unit = cookSourceImport(input, options.sourceRoot);
        if (!unit)
        {
            printError(unit.error());
            return 1;
        }
        if (const auto status = appendCookRequest(request, hasTargetPlatform,
                                                  std::move(unit->request));
            !status)
        {
            printError(status.error());
            return 1;
        }
        candidates.push_back(std::move(unit->sourceImports));
    }
    auto composed = Tina::Asset::composeSourceImportCandidate(
        Tina::Asset::SourceImportCandidateComposeDesc{.recookedCandidates = candidates});
    if (!composed)
    {
        printError(composed.error());
        return 1;
    }

    const std::string_view targetRoot = options.stageOutRoot.empty()
                                            ? std::string_view(options.outRoot)
                                            : std::string_view(options.stageOutRoot);
    const std::string_view targetState = options.stageImportStatePath.empty()
                                             ? std::string_view(options.importStatePath)
                                             : std::string_view(options.stageImportStatePath);
    auto staged = Tina::Asset::cookAndStageCatalogPackage(
        targetRoot, request, stageConfig(memory, verifyTypedPayloads(options)));
    if (!staged)
    {
        printError(staged.error());
        return 1;
    }
    if (const auto status = commitImportState(targetRoot, targetState,
                                              composed->candidate, memory);
        !status)
    {
        printError(status.error());
        return 1;
    }

    const auto unitCount = static_cast<Tina::Core::u32>(options.imports.size());
    printSuccess(*staged, mode, "full-recook", loaded->probe, unitCount, unitCount, 0U,
                 0U, staged->entryCount(), true, targetRoot);
    return 0;
}
