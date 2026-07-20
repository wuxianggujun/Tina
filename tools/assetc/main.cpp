#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options final {
    std::string outRoot;
    std::string recipePath;
    // When no recipe: emit Texture2D(2x2 checker) + Sprite depending on it.
    bool useTyped2dFixture = true;
};

void printUsage()
{
    std::cerr
        << "tina_assetc --out <catalogRoot> [options]\n"
        << "  Fixture/recipe cooker for Catalog packages.\n"
        << "  --recipe <path>   cook from line recipe\n"
        << "  --legacy-text     default fixture uses raw text Material payloads (compat)\n"
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
        if (arg == "--legacy-text")
        {
            options.useTyped2dFixture = false;
            continue;
        }
        // Keep old flags as no-ops for CI scripts that still pass payloads (ignored in typed mode).
        if (arg == "--texture-payload" || arg == "--material-payload")
        {
            (void)requireValue(arg);
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

[[nodiscard]] Tina::Core::Result<Tina::Asset::CatalogCookRequest> buildLegacyTextRequest()
{
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    auto asBytes = [](std::string_view text) {
        std::vector<std::byte> bytes(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
        }
        return bytes;
    };
    Tina::Asset::CatalogCookRequest request{.targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = asBytes("tex-payload"),
    });
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .payload = asBytes("mat-payload"),
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

    Tina::Core::Result<Tina::Asset::CatalogCookRequest> request =
        Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig, "no request");
    std::string mode = "typed2d";
    if (!options.recipePath.empty())
    {
        request = Tina::Asset::loadCatalogCookRecipeFile(options.recipePath);
        mode = "recipe";
    } else if (options.useTyped2dFixture)
    {
        request = buildTyped2dRequest();
    } else
    {
        request = buildLegacyTextRequest();
        mode = "legacy_text";
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

    std::pmr::unsynchronized_pool_resource memory;
    Tina::Asset::CatalogPackageOpenConfig openConfig{
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
        .validateOnOpen = true,
        .validation =
            Tina::Asset::CatalogPackageValidationConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = options.useTyped2dFixture && options.recipePath.empty(),
            },
    };
    auto catalog = Tina::Asset::openCatalogPackage(options.outRoot, openConfig);
    if (!catalog)
    {
        printError(catalog.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"tool\":\"tina_assetc\",\"entries\":" << catalog->entryCount()
              << ",\"dependencies\":" << catalog->dependencyCount() << ",\"mode\":\"" << mode << "\""
              << ",\"out\":\"" << options.outRoot << "\"}\n";
    return 0;
}
