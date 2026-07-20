#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageLoad.hpp>
#include <tina/asset/CatalogPackageSummary.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>

#include <cstdint>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Options final {
    std::string root;
    std::string manifestRelative = "manifest.tmnft";
    bool metadataOnly = false;
    bool skipValidate = false;
    bool listEntries = false;
    bool planLoads = false;
    bool loadAssets = false;
    bool typedPayloads = false;
    std::vector<std::string> assetIdTexts;
    Tina::Core::u32 maxEntries = 100000;
    Tina::Core::u32 maxDependencies = 400000;
    Tina::Core::u32 maxDependenciesPerAsset = 4096;
};

void printUsage()
{
    std::cerr
        << "tina_catalog_validate --root <catalogRoot> [options]\n"
        << "  --manifest <relativePath>   default: manifest.tmnft\n"
        << "  --metadata-only             size/presence only (no ContentHash)\n"
        << "  --no-validate               open Snapshot only; skip package validation\n"
        << "  --list-entries              include entry rows in JSON summary\n"
        << "  --plan-loads                include dependency-first load plan rows\n"
        << "  --load-assets               one-shot open+plan+load cooked assets\n"
        << "  --typed-payloads            require Texture2D/Sprite payload v1 parse\n"
        << "  --asset-id <32hex>          plan/load only these ids (repeatable; default: all)\n"
        << "  --max-entries <n>\n"
        << "  --max-dependencies <n>\n"
        << "  --max-dependencies-per-asset <n>\n"
        << "  --help\n";
}

[[nodiscard]] bool parseU32(std::string_view text, Tina::Core::u32& out)
{
    if (text.empty())
    {
        return false;
    }
    std::uint64_t value = 0;
    for (const char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            return false;
        }
        value = value * 10U + static_cast<std::uint64_t>(ch - '0');
        if (value > 0xFFFFFFFFULL)
        {
            return false;
        }
    }
    out = static_cast<Tina::Core::u32>(value);
    return true;
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
        if (arg == "--metadata-only")
        {
            options.metadataOnly = true;
            continue;
        }
        if (arg == "--no-validate")
        {
            options.skipValidate = true;
            continue;
        }
        if (arg == "--list-entries")
        {
            options.listEntries = true;
            continue;
        }
        if (arg == "--plan-loads")
        {
            options.planLoads = true;
            continue;
        }
        if (arg == "--load-assets")
        {
            options.loadAssets = true;
            continue;
        }
        if (arg == "--typed-payloads")
        {
            options.typedPayloads = true;
            continue;
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
        if (arg == "--root")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.root.assign(value);
            continue;
        }
        if (arg == "--manifest")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.manifestRelative.assign(value);
            continue;
        }
        if (arg == "--asset-id")
        {
            const auto value = requireValue(arg);
            if (value.empty())
            {
                return 2;
            }
            options.assetIdTexts.emplace_back(value);
            continue;
        }
        if (arg == "--max-entries")
        {
            const auto value = requireValue(arg);
            if (value.empty() || !parseU32(value, options.maxEntries))
            {
                std::cerr << "invalid --max-entries\n";
                return 2;
            }
            continue;
        }
        if (arg == "--max-dependencies")
        {
            const auto value = requireValue(arg);
            if (value.empty() || !parseU32(value, options.maxDependencies))
            {
                std::cerr << "invalid --max-dependencies\n";
                return 2;
            }
            continue;
        }
        if (arg == "--max-dependencies-per-asset")
        {
            const auto value = requireValue(arg);
            if (value.empty() || !parseU32(value, options.maxDependenciesPerAsset))
            {
                std::cerr << "invalid --max-dependencies-per-asset\n";
                return 2;
            }
            continue;
        }
        std::cerr << "unknown argument: " << arg << '\n';
        printUsage();
        return 2;
    }
    if (options.root.empty())
    {
        std::cerr << "--root is required\n";
        printUsage();
        return 2;
    }
    if (!options.assetIdTexts.empty() && !options.planLoads && !options.loadAssets)
    {
        std::cerr << "--asset-id requires --plan-loads or --load-assets\n";
        return 2;
    }
    return 0;
}

void printErrorJson(const Tina::Core::Error& error)
{
    std::cout << "{\"status\":\"error\",\"domain\":" << static_cast<unsigned>(error.code.domain)
              << ",\"code\":" << error.code.value << ",\"message\":\"";
    for (const char ch : error.message)
    {
        if (ch == '"' || ch == '\\')
        {
            std::cout << '\\';
        }
        if (static_cast<unsigned char>(ch) < 0x20U)
        {
            continue;
        }
        std::cout << ch;
    }
    std::cout << "\"}\n";
}

void printErrorMessage(std::string_view message)
{
    std::cout << "{\"status\":\"error\",\"domain\":1,\"code\":1,\"message\":\"";
    for (const char ch : message)
    {
        if (ch == '"' || ch == '\\')
        {
            std::cout << '\\';
        }
        std::cout << ch;
    }
    std::cout << "\"}\n";
}

[[nodiscard]] Tina::Core::Result<std::pmr::vector<Tina::Core::AssetId>>
parseRequestedIds(const Options& options, std::pmr::memory_resource& memoryResource)
{
    std::pmr::vector<Tina::Core::AssetId> requested{&memoryResource};
    if (options.assetIdTexts.empty())
    {
        return requested;
    }
    requested.reserve(options.assetIdTexts.size());
    for (const auto& text : options.assetIdTexts)
    {
        const auto id = Tina::Core::AssetId::parseCanonical(text);
        if (!id)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "invalid --asset-id (expect 32 lowercase hex)");
        }
        requested.push_back(*id);
    }
    return requested;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (const int parseResult = parseArgs(argc, argv, options); parseResult != 0)
    {
        return parseResult == 2 ? 2 : 1;
    }

    std::pmr::unsynchronized_pool_resource memoryResource;
    Tina::Asset::CatalogPackageOpenConfig openConfig{
        .manifest =
            Tina::Asset::CatalogFileLoadConfig{
                .catalog =
                    Tina::Asset::CatalogConfig{
                        .maxEntries = options.maxEntries,
                        .maxDependencies = options.maxDependencies,
                        .maxDependenciesPerAsset = options.maxDependenciesPerAsset,
                        .memoryResource = &memoryResource,
                    },
            },
        .validateOnOpen = !options.skipValidate,
        .validation =
            Tina::Asset::CatalogPackageValidationConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &memoryResource},
                .verifyContent = !options.metadataOnly,
                .verifyTypedPayload = options.typedPayloads && !options.metadataOnly,
            },
        .manifestRelativePath = options.manifestRelative,
    };

    auto requested = parseRequestedIds(options, memoryResource);
    if (!requested)
    {
        printErrorJson(requested.error());
        return 1;
    }

    Tina::Asset::CatalogSnapshot snapshot;
    std::optional<std::pmr::vector<Tina::Asset::CookedAssetFile>> loadedAssets;
    if (options.loadAssets)
    {
        Tina::Asset::CookedAssetBatchLoadConfig batchConfig{
            .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &memoryResource},
            .memoryResource = &memoryResource,
        };
        auto loaded = Tina::Asset::loadCookedAssetsFromPackage(options.root, *requested, openConfig, batchConfig);
        if (!loaded)
        {
            printErrorJson(loaded.error());
            return 1;
        }
        snapshot = std::move(loaded->catalog);
        loadedAssets = std::move(loaded->assets);
    } else
    {
        auto opened = Tina::Asset::openCatalogPackage(options.root, openConfig);
        if (!opened)
        {
            printErrorJson(opened.error());
            return 1;
        }
        snapshot = std::move(*opened);
    }

    auto summary = Tina::Asset::buildCatalogPackageSummary(
        snapshot, Tina::Asset::CatalogPackageSummaryConfig{.memoryResource = &memoryResource,
                                                           .includeEntries = options.listEntries});
    if (!summary)
    {
        printErrorJson(summary.error());
        return 1;
    }

    std::optional<std::pmr::vector<Tina::Asset::CatalogLoadPlanEntry>> planRows;
    if (options.planLoads)
    {
        Tina::Asset::CatalogLoadPlanConfig planConfig{.memoryResource = &memoryResource};
        Tina::Core::Result<std::pmr::vector<Tina::Asset::CatalogLoadPlanEntry>> plan =
            Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig, "plan not computed");
        if (requested->empty())
        {
            plan = Tina::Asset::planCatalogLoadsAll(snapshot, planConfig);
        } else
        {
            plan = Tina::Asset::planCatalogLoads(snapshot, *requested, planConfig);
        }
        if (!plan)
        {
            printErrorJson(plan.error());
            return 1;
        }
        planRows = std::move(*plan);
    }

    std::optional<Tina::Core::u64> plannedBytes;
    if (planRows)
    {
        auto total = Tina::Asset::totalCookedFileBytes(*planRows);
        if (!total)
        {
            printErrorJson(total.error());
            return 1;
        }
        plannedBytes = *total;
    }

    std::cout << "{\"status\":\"ok\",\"entries\":" << summary->entryCount
              << ",\"dependencies\":" << summary->dependencyCount
              << ",\"validated\":" << (options.skipValidate ? "false" : "true")
              << ",\"contentHash\":" << ((!options.skipValidate && !options.metadataOnly) ? "true" : "false")
              << ",\"loadedAssets\":" << (loadedAssets ? loadedAssets->size() : 0U);
    if (plannedBytes)
    {
        std::cout << ",\"plannedCookedFileBytes\":" << *plannedBytes;
    }
    if (options.listEntries)
    {
        std::cout << ",\"items\":[";
        for (std::size_t index = 0; index < summary->entries.size(); ++index)
        {
            const auto& row = summary->entries[index];
            if (index != 0U)
            {
                std::cout << ',';
            }
            std::cout << "{\"id\":\"" << std::string_view(row.assetIdText.data(), row.assetIdText.size())
                      << "\",\"kind\":" << static_cast<unsigned>(row.assetKind)
                      << ",\"typeVersion\":" << row.assetTypeVersion
                      << ",\"dependencyCount\":" << row.dependencyCount
                      << ",\"cookedFileBytes\":" << row.cookedFileBytes << '}';
        }
        std::cout << ']';
    }
    if (planRows)
    {
        std::cout << ",\"loadPlan\":[";
        for (std::size_t index = 0; index < planRows->size(); ++index)
        {
            const auto& row = (*planRows)[index];
            if (index != 0U)
            {
                std::cout << ',';
            }
            const auto idText = row.assetId.canonicalText();
            std::cout << "{\"order\":" << index << ",\"entryIndex\":" << row.entryIndex << ",\"id\":\""
                      << std::string_view(idText.data(), idText.size())
                      << "\",\"kind\":" << static_cast<unsigned>(row.assetKind)
                      << ",\"dependencyCount\":" << row.dependencyCount
                      << ",\"cookedFileBytes\":" << row.cookedFileBytes << ",\"path\":\"" << row.relativePath.view()
                      << "\"}";
        }
        std::cout << ']';
    }
    if (loadedAssets)
    {
        std::cout << ",\"loaded\":[";
        for (std::size_t index = 0; index < loadedAssets->size(); ++index)
        {
            const auto& asset = (*loadedAssets)[index];
            if (index != 0U)
            {
                std::cout << ',';
            }
            const auto idText = asset.header().assetId.canonicalText();
            std::cout << "{\"order\":" << index << ",\"id\":\""
                      << std::string_view(idText.data(), idText.size())
                      << "\",\"kind\":" << static_cast<unsigned>(asset.header().assetKind)
                      << ",\"payloadBytes\":" << asset.header().payloadBytes << '}';
        }
        std::cout << ']';
    }
    std::cout << "}\n";
    return 0;
}
