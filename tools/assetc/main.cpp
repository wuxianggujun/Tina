#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
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
    std::string materialPayload = "mat-payload";
    std::string texturePayload = "tex-payload";
};

void printUsage()
{
    std::cerr
        << "tina_assetc --out <catalogRoot> [options]\n"
        << "  Fixture/recipe cooker for Catalog packages.\n"
        << "  --recipe <path>            cook from line recipe (preferred)\n"
        << "  --texture-payload <text>   default fixture: tex-payload\n"
        << "  --material-payload <text>  default fixture: mat-payload\n"
        << "  --help\n"
        << "\n"
        << "Recipe lines:\n"
        << "  platform WindowsX64\n"
        << "  asset Texture2D <32hex> <payloadPath>\n"
        << "  asset Material <32hex> <payloadPath> <dep32hex:Texture2D>\n";
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
        if (arg == "--texture-payload")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.texturePayload.assign(value);
            continue;
        }
        if (arg == "--material-payload")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.materialPayload.assign(value);
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

[[nodiscard]] std::vector<std::byte> asByteVector(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(text[index]));
    }
    return bytes;
}

void printError(const Tina::Core::Error& error)
{
    std::cout << "{\"status\":\"error\",\"domain\":" << static_cast<unsigned>(error.code.domain)
              << ",\"code\":" << error.code.value << ",\"message\":\"" << error.message << "\"}\n";
}

[[nodiscard]] Tina::Core::Result<Tina::Asset::CatalogCookRequest> buildDefaultRequest(const Options& options)
{
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    Tina::Asset::CatalogCookRequest request{
        .targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64,
    };
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = asByteVector(options.texturePayload),
    });
    request.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .payload = asByteVector(options.materialPayload),
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
    if (!options.recipePath.empty())
    {
        request = Tina::Asset::loadCatalogCookRecipeFile(options.recipePath);
    } else
    {
        request = buildDefaultRequest(options);
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
            },
    };
    auto catalog = Tina::Asset::openCatalogPackage(options.outRoot, openConfig);
    if (!catalog)
    {
        printError(catalog.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"tool\":\"tina_assetc\",\"entries\":" << catalog->entryCount()
              << ",\"dependencies\":" << catalog->dependencyCount()
              << ",\"mode\":\"" << (options.recipePath.empty() ? "fixture" : "recipe") << "\""
              << ",\"out\":\"" << options.outRoot << "\"}\n";
    return 0;
}
