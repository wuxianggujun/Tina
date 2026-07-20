#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options final {
    std::string outRoot;
    std::string materialPayload = "mat-payload";
    std::string texturePayload = "tex-payload";
};

void printUsage()
{
    std::cerr
        << "tina_assetc --out <catalogRoot> [options]\n"
        << "  Minimal fixture cooker: writes a 2-entry Texture2D+Material catalog package.\n"
        << "  --texture-payload <text>   default: tex-payload\n"
        << "  --material-payload <text>  default: mat-payload\n"
        << "  --help\n";
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

[[nodiscard]] std::span<const std::byte> asBytes(std::string_view text)
{
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

void printError(const Tina::Core::Error& error)
{
    std::cout << "{\"status\":\"error\",\"domain\":" << static_cast<unsigned>(error.code.domain)
              << ",\"code\":" << error.code.value << ",\"message\":\"" << error.message << "\"}\n";
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (const int parseResult = parseArgs(argc, argv, options); parseResult != 0)
    {
        return parseResult == 2 ? 2 : 1;
    }

    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    const std::array materialDeps{Tina::AssetFormat::CookedAssetWriteDependency{
        .assetId = textureId,
        .expectedKind = Tina::AssetFormat::AssetKind::Texture2D,
        .flags = Tina::AssetFormat::DependencyFlags::Required,
    }};

    auto textureBytes = Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
        .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = asBytes(options.texturePayload),
        .computeContentHash = true,
    });
    auto materialBytes = Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
        .assetKind = Tina::AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .dependencies = materialDeps,
        .payload = asBytes(options.materialPayload),
        .computeContentHash = true,
    });
    if (!textureBytes || !materialBytes)
    {
        printError(textureBytes ? materialBytes.error() : textureBytes.error());
        return 1;
    }

    const auto textureHash = *Tina::Core::digestContentHashV1(asBytes(options.texturePayload));
    const auto materialHash = *Tina::Core::digestContentHashV1(asBytes(options.materialPayload));
    const std::array entries{
        Tina::AssetFormat::CookedManifestWriteEntry{
            .assetId = textureId,
            .contentHash = textureHash,
            .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
            .cookedFileBytes = textureBytes->size(),
        },
        Tina::AssetFormat::CookedManifestWriteEntry{
            .assetId = materialId,
            .contentHash = materialHash,
            .assetKind = Tina::AssetFormat::AssetKind::Material,
            .cookedFileBytes = materialBytes->size(),
            .dependencies = materialDeps,
        },
    };
    auto manifestBytes = Tina::AssetFormat::writeCookedManifestBytes(Tina::AssetFormat::CookedManifestWriteDesc{
        .targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64,
        .entries = entries,
    });
    if (!manifestBytes)
    {
        printError(manifestBytes.error());
        return 1;
    }

    const std::array objects{
        Tina::Asset::CatalogPackageObjectBlob{
            .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
            .assetId = textureId,
            .bytes = *textureBytes,
        },
        Tina::Asset::CatalogPackageObjectBlob{
            .assetKind = Tina::AssetFormat::AssetKind::Material,
            .assetId = materialId,
            .bytes = *materialBytes,
        },
    };

    if (const auto status = Tina::Asset::publishCatalogPackage(options.outRoot, "manifest.tmnft", *manifestBytes,
                                                               objects);
        !status)
    {
        printError(status.error());
        return 1;
    }

    // Validate by reopening.
    std::pmr::unsynchronized_pool_resource memory;
    Tina::Asset::CatalogPackageOpenConfig openConfig{
        .manifest =
            Tina::Asset::CatalogFileLoadConfig{
                .catalog =
                    Tina::Asset::CatalogConfig{
                        .maxEntries = 16,
                        .maxDependencies = 16,
                        .maxDependenciesPerAsset = 8,
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
              << ",\"out\":\"" << options.outRoot << "\"}\n";
    return 0;
}
