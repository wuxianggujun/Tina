#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset/MediaCook.hpp>
#include <tina/asset/SourceImportPipeline.hpp>
#include <tina/asset/TextureMipChain.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include "ShaderCompile.hpp"

#include "core/io/PathUtil.hpp"

#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>

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
    Texture = 2,
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
    // Shader compile mode. Produces a Shader payload file for a recipe to cook; it does not touch a
    // catalog, so it is mutually exclusive with every option above.
    std::string shaderSource;
    std::string shaderOutput;
    std::string shaderKindName;
    std::string shadercPath;
    std::string shaderVaryingDef;
    std::vector<std::string> shaderIncludeDirs{};
    std::vector<Tina::AssetFormat::ShaderBinaryProfile> shaderProfiles{};
};

struct PreCookProbe final {
    bool enabled = false;
    Tina::Asset::SourceImportProbeState state = Tina::Asset::SourceImportProbeState::NoBaseline;
    std::string_view reason = "disabled";
    Tina::Core::u32 cleanUnitCount = 0;
    Tina::Core::u32 cleanObjectCount = 0;
};

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
        if (Tina::Core::Detail::pathsOverlap(*stageRoot, *liveRoot))
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
        if (Tina::Core::Detail::pathsOverlap(*baselineState, *liveRoot))
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
        if ((baselineState && Tina::Core::Detail::pathsOverlap(*stageState, *baselineState)) ||
            Tina::Core::Detail::pathsOverlap(*stageState, *liveRoot) ||
            (stageRoot && Tina::Core::Detail::pathsOverlap(*stageState, *stageRoot)) ||
            (stageRoot && baselineState &&
             Tina::Core::Detail::pathsOverlap(*stageRoot, *baselineState)))
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
        << "  Catalog cooker for recipes and source assets.\n"
        << "  --recipe <path>   cook from line recipe\n"
        << "  --gltf <path>     cook glTF/GLB -> meshes, materials, textures and prefab\n"
        << "  --texture <path>  cook PNG/JPEG -> mipped Texture2D\n"
        << "                    import options may be repeated to form one batch\n"
        << "  --source-root <path>  authoring root for canonical source provenance\n"
        << "  --import-state <path> commit TINAIMPT state after fresh package validation\n"
        << "  --stage-out <path> fresh candidate root when --out already has a baseline\n"
        << "  --stage-import-state <path> state file bound to --stage-out\n"
        << "  --help\n"
        << "\n"
        << "tina_assetc --shader-source <fs.sc> --shader-out <payload> --shader-kind <Sprite2D|Mesh3D>\n"
        << "            --shaderc <path> --shader-varying-def <def.sc> --shader-include <dir> ...\n"
        << "            [--shader-profile <glsl120|spv|dxbc|essl300>] ...\n"
        << "  Compiles one custom fragment shader into a Shader payload, then cook it with\n"
        << "  `asset Shader <32hex> <payload>` in a recipe.\n"
        << "  The source must #include the contract header for its kind and declare its own $input\n"
        << "  line; --shader-include must name the directory holding that header and bgfx_shader.sh.\n"
        << "  --shader-profile may repeat and defaults to the host set (glsl120, spv, and dxbc on\n"
        << "  Windows). Pass essl300 when cooking for the GLES renderer that\n"
        << "  TINA_RENDER_BGFX_MOBILE_SHADERS builds the engine's own shaders for.\n"
        << "  This mode writes no catalog, so it cannot be combined with --out.\n"
        << "\n"
        << "Default fixture (no --recipe): Texture2D 2x2 RGBA mipped + Sprite full-UV.\n"
        << "Recipe lines:\n"
        << "  platform WindowsX64\n"
        << "  asset Texture2D <32hex> <payloadPath>\n"
        << "  asset Sprite <32hex> <payloadPath> <dep32hex:Texture2D>\n"
        << "  texture2d <32hex> <w> <h> <RRGGBBAA>...\n"
        << "      Mipped unless a sprite UV rect or a tileset carves a sub-rect out of it.\n"
        << "  sprite <32hex> <texture32hex> [u0 v0 u1 v1 pivotX pivotY ppu]\n"
        << "  tileset <32hex> <texture32hex> <tilePxW> <tilePxH>\n"
        << "  tile <localId> <flags> <u0> <v0> <u1> <v1>\n"
        << "  tilemap <32hex> <tileset32hex> <w> <h> <cellSize>\n"
        << "  row <localId>...\n"
        << "  staticmesh <32hex> cube [shader <shader32hex>]\n"
        << "      The optional shader is the mesh's own default Mesh3D fragment stage;\n"
        << "      MeshRenderer3D::shader still overrides it per instance.\n";
}

[[nodiscard]] int parseArgs(int argc, char** argv, Options& options)
{
    Tina::Core::ArgScanner scanner(argc, argv);
    while (scanner.next())
    {
        if (scanner.flag("--help") || scanner.flag("-h"))
        {
            printUsage();
            return 2;
        }
        if (const auto value = scanner.value("--out"))
        {
            options.outRoot.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--recipe"))
        {
            options.imports.push_back(ImportOption{.kind = ImportKind::Recipe,
                                                   .path = std::string(*value)});
            continue;
        }
        if (const auto value = scanner.value("--gltf"))
        {
            options.imports.push_back(ImportOption{.kind = ImportKind::Gltf,
                                                   .path = std::string(*value)});
            continue;
        }
        if (const auto value = scanner.value("--texture"))
        {
            options.imports.push_back(ImportOption{.kind = ImportKind::Texture,
                                                   .path = std::string(*value)});
            continue;
        }
        if (const auto value = scanner.value("--source-root"))
        {
            options.sourceRoot.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--import-state"))
        {
            options.importStatePath.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--stage-out"))
        {
            options.stageOutRoot.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--stage-import-state"))
        {
            options.stageImportStatePath.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--shaderc"))
        {
            options.shadercPath.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--shader-source"))
        {
            options.shaderSource.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--shader-out"))
        {
            options.shaderOutput.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--shader-kind"))
        {
            options.shaderKindName.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--shader-varying-def"))
        {
            options.shaderVaryingDef.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--shader-include"))
        {
            options.shaderIncludeDirs.emplace_back(*value);
            continue;
        }
        if (const auto value = scanner.value("--shader-profile"))
        {
            const auto profile = Tina::AssetFormat::parseShaderBinaryProfileName(*value);
            if (!profile.has_value())
            {
                std::cerr << "unknown --shader-profile: " << *value << '\n';
                return 2;
            }
            options.shaderProfiles.push_back(*profile);
            continue;
        }
        if (scanner.failed())
        {
            std::cerr << "missing value for " << scanner.failedOption() << '\n';
            return 2;
        }
        std::cerr << "unknown argument: " << scanner.token() << '\n';
        printUsage();
        return 2;
    }
    const bool anyShaderOption =
        !options.shaderSource.empty() || !options.shaderOutput.empty() ||
        !options.shaderKindName.empty() || !options.shadercPath.empty() ||
        !options.shaderVaryingDef.empty() || !options.shaderIncludeDirs.empty() ||
        !options.shaderProfiles.empty();
    if (anyShaderOption)
    {
        // Rejected rather than ignored: a shader compile that also published a catalog would give
        // two very different meanings to one exit code.
        if (!options.outRoot.empty() || !options.imports.empty() || !options.sourceRoot.empty() ||
            !options.importStatePath.empty() || !options.stageOutRoot.empty() ||
            !options.stageImportStatePath.empty())
        {
            std::cerr << "shader compile options cannot be combined with catalog cook options\n";
            return 2;
        }
        if (options.shaderSource.empty() || options.shaderOutput.empty() ||
            options.shaderKindName.empty() || options.shadercPath.empty() ||
            options.shaderVaryingDef.empty() || options.shaderIncludeDirs.empty())
        {
            std::cerr << "shader compile requires --shader-source, --shader-out, --shader-kind, "
                         "--shaderc, --shader-varying-def and at least one --shader-include\n";
            return 2;
        }
        if (Tina::AssetC::parseShaderKindName(options.shaderKindName) ==
            Tina::AssetFormat::ShaderKind::Invalid)
        {
            std::cerr << "--shader-kind must be Sprite2D or Mesh3D\n";
            return 2;
        }
        // Sorted ascending and duplicate-free is the payload's own encoding rule, so a recipe may
        // list --shader-profile in any order and still cook one canonical payload.
        std::sort(options.shaderProfiles.begin(), options.shaderProfiles.end());
        if (std::adjacent_find(options.shaderProfiles.begin(), options.shaderProfiles.end()) !=
            options.shaderProfiles.end())
        {
            std::cerr << "--shader-profile was repeated\n";
            return 2;
        }
        for (const auto profile : options.shaderProfiles)
        {
            if (!Tina::AssetC::isShaderProfileSupportedOnHost(profile))
            {
                std::cerr << "--shader-profile is not supported on this host: "
                          << Tina::AssetFormat::shaderBinaryProfileName(profile) << '\n';
                return 2;
            }
        }
        return 0;
    }
    if (options.outRoot.empty())
    {
        std::cerr << "--out is required\n";
        printUsage();
        return 2;
    }
    if (!options.importStatePath.empty() && options.sourceRoot.empty())
    {
        std::cerr << "--import-state requires --source-root\n";
        return 2;
    }
    const bool hasTexture = std::any_of(
        options.imports.begin(), options.imports.end(),
        [](const ImportOption& input) { return input.kind == ImportKind::Texture; });
    if (hasTexture && options.sourceRoot.empty())
    {
        std::cerr << "--texture requires --source-root for stable AssetId derivation\n";
        return 2;
    }
    if (!options.sourceRoot.empty() && options.imports.empty())
    {
        std::cerr << "--source-root requires at least one import input\n";
        return 2;
    }
    if (!options.importStatePath.empty() && options.imports.empty())
    {
        std::cerr << "--import-state requires at least one import input\n";
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
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("domain", static_cast<unsigned>(error.code.domain));
    writer.member("code", error.code.value);
    writer.member("message", error.message);
    writer.endObject();
    std::cout << '\n';
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
    switch (options.imports.front().kind)
    {
    case ImportKind::Recipe:
        return "recipe";
    case ImportKind::Gltf:
        return "gltf";
    case ImportKind::Texture:
        return "texture";
    }
    return "unknown";
}

void printSuccess(const Tina::Asset::CatalogSnapshot& catalog, std::string_view mode,
                  std::string_view cookMode, const PreCookProbe& probe,
                  Tina::Core::u32 unitsTotal, Tina::Core::u32 unitsRecooked,
                  Tina::Core::u32 unitsRemoved,
                  Tina::Core::u32 objectsReused, Tina::Core::u32 objectsCooked,
                  bool importStateCommitted, std::string_view outRoot)
{
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("tool", "tina_assetc");
    writer.member("entries", catalog.entryCount());
    writer.member("dependencies", catalog.dependencyCount());
    writer.member("mode", mode);
    writer.member("cookMode", cookMode);
    writer.member("probe", probe.enabled ? probeStateName(probe.state) : std::string_view("disabled"));
    writer.member("probeReason", probe.reason);
    writer.member("unitsTotal", unitsTotal);
    writer.member("unitsRecooked", unitsRecooked);
    writer.member("unitsRemoved", unitsRemoved);
    writer.member("objectsReused", objectsReused);
    writer.member("objectsCooked", objectsCooked);
    writer.member("importStateCommitted", importStateCommitted);
    writer.member("out", outRoot);
    writer.endObject();
    std::cout << '\n';
}

void printPipelineSuccess(const Tina::Asset::SourceImportPipelineResult& result,
                          std::string_view mode)
{
    const auto cookMode = result.mode == Tina::Asset::SourceImportPipelineMode::CleanReuse
                              ? "clean-reuse"
                          : result.mode == Tina::Asset::SourceImportPipelineMode::IncrementalRecook
                              ? "incremental-recook"
                              : "full-recook";
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("tool", "tina_assetc");
    writer.member("entries", result.catalogEntries);
    writer.member("dependencies", result.catalogDependencies);
    writer.member("mode", mode);
    writer.member("cookMode", cookMode);
    writer.member("probe", probeStateName(result.probeState));
    writer.member("probeReason", Tina::Asset::sourceImportProbeReasonName(result.probeReason));
    writer.member("unitsTotal", result.unitsTotal);
    writer.member("unitsRecooked", result.unitsRecooked);
    writer.member("unitsRemoved", result.unitsRemoved);
    writer.member("objectsReused", result.objectsReused);
    writer.member("objectsCooked", result.objectsCooked);
    writer.member("importStateCommitted", result.importStateCommitted);
    writer.member("out", result.catalogRootUtf8);
    writer.endObject();
    std::cout << '\n';
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
    // The sprite below samples the whole image, so this fixture is a whole-image
    // producer and gets a chain -- matching what the same content cooks to when it is
    // written as a recipe instead.
    auto texPayload = Tina::Asset::writeMippedTexture2DPayloadBytesRgba8(
        2, 2, pixels, Tina::AssetFormat::Texture2DColorSpace::Srgb);
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
cookImportRequest(const ImportOption& input,
                  Tina::AssetFormat::TargetPlatform targetPlatform,
                  std::string_view sourceRoot)
{
    const Tina::Asset::SourceImportCaptureConfig capture{.sourceRootUtf8 = sourceRoot};
    switch (input.kind)
    {
    case ImportKind::Recipe:
        if (sourceRoot.empty())
        {
            return Tina::Asset::loadCatalogCookRecipeFile(input.path);
        }
        if (auto cooked = Tina::Asset::loadCatalogCookRecipeSourceFile(input.path, capture))
        {
            return std::move(cooked->request);
        }
        else
        {
            return Tina::Core::failure(std::move(cooked.error()));
        }
    case ImportKind::Gltf:
        if (sourceRoot.empty())
        {
            return Tina::Asset::cookGltfFileToCatalogRequest(input.path, targetPlatform);
        }
        if (auto cooked = Tina::Asset::cookGltfFileToCatalogSourceResult(
                input.path, targetPlatform, capture))
        {
            return std::move(cooked->request);
        }
        else
        {
            return Tina::Core::failure(std::move(cooked.error()));
        }
    case ImportKind::Texture:
        if (auto cooked = Tina::Asset::cookTextureFileToCatalogSourceResult(
                input.path, targetPlatform, capture))
        {
            return std::move(cooked->request);
        }
        else
        {
            return Tina::Core::failure(std::move(cooked.error()));
        }
    }
    return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                               "source import kind is unsupported");
}

[[nodiscard]] constexpr Tina::Asset::SourceImportPipelineUnitKind
pipelineUnitKind(ImportKind kind) noexcept
{
    switch (kind)
    {
    case ImportKind::Recipe:
        return Tina::Asset::SourceImportPipelineUnitKind::CatalogRecipe;
    case ImportKind::Gltf:
        return Tina::Asset::SourceImportPipelineUnitKind::Gltf;
    case ImportKind::Texture:
        return Tina::Asset::SourceImportPipelineUnitKind::Texture;
    }
    return Tina::Asset::SourceImportPipelineUnitKind::CatalogRecipe;
}

[[nodiscard]] constexpr Tina::AssetFormat::TargetPlatform hostTargetPlatform() noexcept
{
#if defined(_WIN32)
    return Tina::AssetFormat::TargetPlatform::WindowsX64;
#else
    return Tina::AssetFormat::TargetPlatform::LinuxX64;
#endif
}

[[nodiscard]] Tina::Core::Result<Tina::AssetFormat::TargetPlatform>
resolveRecipeTargetPlatform(const Options& options)
{
    Tina::AssetFormat::TargetPlatform targetPlatform =
        Tina::AssetFormat::TargetPlatform::Invalid;
    for (const auto& input : options.imports)
    {
        if (input.kind != ImportKind::Recipe)
        {
            continue;
        }
        auto recipeTarget = Tina::Asset::loadCatalogCookRecipeTargetPlatform(input.path);
        if (!recipeTarget)
        {
            return Tina::Core::failure(std::move(recipeTarget.error()));
        }
        if (targetPlatform == Tina::AssetFormat::TargetPlatform::Invalid)
        {
            targetPlatform = *recipeTarget;
        }
        else if (targetPlatform != *recipeTarget)
        {
            return Tina::Core::failure(
                Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                "source import recipes target different platforms");
        }
    }
    return targetPlatform == Tina::AssetFormat::TargetPlatform::Invalid
               ? hostTargetPlatform()
               : targetPlatform;
}

[[nodiscard]] Tina::Core::Result<std::optional<Tina::AssetFormat::TargetPlatform>>
committedImportTargetPlatform(const Options& options,
                              std::pmr::memory_resource& memory)
{
    if (options.importStatePath.empty())
    {
        return std::optional<Tina::AssetFormat::TargetPlatform>{};
    }
    auto bytes = Tina::Core::readFile(
        options.importStatePath,
        Tina::Core::ReadFileConfig{.maxBytes = Tina::Core::MaxReadFileBytes,
                                   .memoryResource = &memory});
    if (!bytes)
    {
        if (bytes.error().code == Tina::Core::CoreErrorCode::NotFound)
        {
            return std::optional<Tina::AssetFormat::TargetPlatform>{};
        }
        return Tina::Core::failure(std::move(bytes.error()));
    }
    auto metadata = Tina::AssetFormat::parseSourceImportMetadataView(*bytes);
    if (!metadata)
    {
        if (metadata.error().code == Tina::AssetFormat::AssetFormatErrorCode::UnsupportedSchema)
        {
            return std::optional<Tina::AssetFormat::TargetPlatform>{};
        }
        return Tina::Core::failure(std::move(metadata.error()));
    }
    return std::optional<Tina::AssetFormat::TargetPlatform>{
        metadata->header().targetPlatform};
}

[[nodiscard]] Tina::Core::Result<Tina::AssetFormat::TargetPlatform>
resolveImportTargetPlatform(const Options& options,
                            std::pmr::memory_resource& memory)
{
    const bool hasRecipe = std::any_of(
        options.imports.begin(), options.imports.end(),
        [](const ImportOption& input) { return input.kind == ImportKind::Recipe; });
    if (!hasRecipe)
    {
        return hostTargetPlatform();
    }
    auto committed = committedImportTargetPlatform(options, memory);
    if (!committed)
    {
        return Tina::Core::failure(std::move(committed.error()));
    }
    if (committed->has_value())
    {
        return **committed;
    }
    return resolveRecipeTargetPlatform(options);
}

[[nodiscard]] Tina::Asset::CatalogPackageStageConfig
stageConfig(std::pmr::memory_resource& memory, bool verifyTypedPayload)
{
    return Tina::Asset::CatalogPackageStageConfig{
        .validation = catalogOpenConfig(memory, false, verifyTypedPayload),
    };
}

[[nodiscard]] int runShaderCompile(const Options& options)
{
    auto compiled = Tina::AssetC::compileShaderPayload(Tina::AssetC::ShaderCompileRequest{
        .shadercPath = options.shadercPath,
        .sourcePath = options.shaderSource,
        .varyingDefPath = options.shaderVaryingDef,
        .outputPath = options.shaderOutput,
        .shaderKind = Tina::AssetC::parseShaderKindName(options.shaderKindName),
        .includeDirs = options.shaderIncludeDirs,
        .profiles = options.shaderProfiles,
    });
    if (!compiled)
    {
        printError(compiled.error());
        return 1;
    }
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("mode", "shader");
    writer.member("shaderKind", options.shaderKindName);
    writer.member("payloadPath", options.shaderOutput);
    writer.member("payloadBytes", compiled->payloadByteCount);
    writer.beginArrayMember("profiles");
    for (const auto& profile : compiled->profiles)
    {
        writer.beginObjectElement();
        writer.member("profile", Tina::AssetFormat::shaderBinaryProfileName(profile.profile));
        writer.member("bytes", profile.byteCount);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    std::cout << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (const int parseResult = parseArgs(argc, argv, options); parseResult != 0)
    {
        return parseResult == 2 ? 2 : 1;
    }

    if (!options.shaderSource.empty())
    {
        return runShaderCompile(options);
    }

    const std::string_view mode = modeName(options);
    std::pmr::unsynchronized_pool_resource memory;
    auto importTargetPlatform = resolveImportTargetPlatform(options, memory);
    if (!importTargetPlatform)
    {
        printError(importTargetPlatform.error());
        return 1;
    }

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
                auto unit = cookImportRequest(input, *importTargetPlatform, options.sourceRoot);
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

    std::vector<Tina::Asset::SourceImportPipelineUnit> units;
    units.reserve(options.imports.size());
    for (const auto& input : options.imports)
    {
        units.push_back(Tina::Asset::SourceImportPipelineUnit{
            .kind = pipelineUnitKind(input.kind),
            .sourceUtf8Path = input.path,
        });
    }
    const auto executePipeline = [&](Tina::AssetFormat::TargetPlatform targetPlatform) {
        return Tina::Asset::executeSourceImportPipeline(Tina::Asset::SourceImportPipelineRequest{
            .sourceRootUtf8 = options.sourceRoot,
            .targetPlatform = targetPlatform,
            .units = units,
            .baselineCatalogRootUtf8 = options.outRoot,
            .baselineStateUtf8Path = options.importStatePath,
            .stageCatalogRootUtf8 = options.stageOutRoot,
            .stageStateUtf8Path = options.stageImportStatePath,
            .stageConfig = stageConfig(memory, verifyTypedPayloads(options)),
        });
    };
    auto imported = executePipeline(*importTargetPlatform);
    if (!imported &&
        imported.error().code == Tina::Asset::AssetErrorCode::SourceImportTargetPlatformMismatch)
    {
        auto currentRecipeTarget = resolveRecipeTargetPlatform(options);
        if (!currentRecipeTarget)
        {
            printError(currentRecipeTarget.error());
            return 1;
        }
        if (*currentRecipeTarget != *importTargetPlatform)
        {
            importTargetPlatform = *currentRecipeTarget;
            imported = executePipeline(*importTargetPlatform);
        }
    }
    if (!imported)
    {
        printError(imported.error());
        return 1;
    }
    printPipelineSuccess(*imported, mode);
    return 0;
}
