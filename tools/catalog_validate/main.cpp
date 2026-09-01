#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageLoad.hpp>
#include <tina/asset/CatalogPackageSummary.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>

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
        if (scanner.flag("--metadata-only"))
        {
            options.metadataOnly = true;
            continue;
        }
        if (scanner.flag("--no-validate"))
        {
            options.skipValidate = true;
            continue;
        }
        if (scanner.flag("--list-entries"))
        {
            options.listEntries = true;
            continue;
        }
        if (scanner.flag("--plan-loads"))
        {
            options.planLoads = true;
            continue;
        }
        if (scanner.flag("--load-assets"))
        {
            options.loadAssets = true;
            continue;
        }
        if (scanner.flag("--typed-payloads"))
        {
            options.typedPayloads = true;
            continue;
        }
        if (const auto value = scanner.value("--root"))
        {
            options.root.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--manifest"))
        {
            options.manifestRelative.assign(*value);
            continue;
        }
        if (const auto value = scanner.value("--asset-id"))
        {
            options.assetIdTexts.emplace_back(*value);
            continue;
        }
        if (const auto value = scanner.value("--max-entries"))
        {
            if (!Tina::Core::parseArgUnsigned(*value, options.maxEntries))
            {
                std::cerr << "invalid --max-entries\n";
                return 2;
            }
            continue;
        }
        // Tested before --max-dependencies-per-asset, which it is a prefix of. ArgScanner only
        // claims a longer token when an '=' follows the name, so the longer option falls through
        // to its own test below.
        if (const auto value = scanner.value("--max-dependencies"))
        {
            if (!Tina::Core::parseArgUnsigned(*value, options.maxDependencies))
            {
                std::cerr << "invalid --max-dependencies\n";
                return 2;
            }
            continue;
        }
        if (const auto value = scanner.value("--max-dependencies-per-asset"))
        {
            if (!Tina::Core::parseArgUnsigned(*value, options.maxDependenciesPerAsset))
            {
                std::cerr << "invalid --max-dependencies-per-asset\n";
                return 2;
            }
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
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("domain", static_cast<unsigned>(error.code.domain));
    writer.member("code", error.code.value);
    writer.member("message", error.message);
    writer.endObject();
    std::cout << '\n';
}

void printErrorMessage(std::string_view message)
{
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("domain", 1);
    writer.member("code", 1);
    writer.member("message", message);
    writer.endObject();
    std::cout << '\n';
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

    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("entries", summary->entryCount);
    writer.member("dependencies", summary->dependencyCount);
    writer.member("validated", !options.skipValidate);
    writer.member("contentHash", !options.skipValidate && !options.metadataOnly);
    writer.member("loadedAssets", loadedAssets ? loadedAssets->size() : std::size_t{0});
    if (plannedBytes)
    {
        writer.member("plannedCookedFileBytes", *plannedBytes);
    }
    if (options.listEntries)
    {
        writer.beginArrayMember("items");
        for (const auto& row : summary->entries)
        {
            writer.beginObjectElement();
            writer.member("id", std::string_view(row.assetIdText.data(), row.assetIdText.size()));
            writer.member("kind", static_cast<unsigned>(row.assetKind));
            writer.member("typeVersion", row.assetTypeVersion);
            writer.member("dependencyCount", row.dependencyCount);
            writer.member("cookedFileBytes", row.cookedFileBytes);
            writer.endObject();
        }
        writer.endArray();
    }
    if (planRows)
    {
        writer.beginArrayMember("loadPlan");
        for (std::size_t index = 0; index < planRows->size(); ++index)
        {
            const auto& row = (*planRows)[index];
            const auto idText = row.assetId.canonicalText();
            writer.beginObjectElement();
            writer.member("order", index);
            writer.member("entryIndex", row.entryIndex);
            writer.member("id", std::string_view(idText.data(), idText.size()));
            writer.member("kind", static_cast<unsigned>(row.assetKind));
            writer.member("dependencyCount", row.dependencyCount);
            writer.member("cookedFileBytes", row.cookedFileBytes);
            writer.member("path", row.relativePath.view());
            writer.endObject();
        }
        writer.endArray();
    }
    if (loadedAssets)
    {
        writer.beginArrayMember("loaded");
        for (std::size_t index = 0; index < loadedAssets->size(); ++index)
        {
            const auto& asset = (*loadedAssets)[index];
            const auto idText = asset.header().assetId.canonicalText();
            writer.beginObjectElement();
            writer.member("order", index);
            writer.member("id", std::string_view(idText.data(), idText.size()));
            writer.member("kind", static_cast<unsigned>(asset.header().assetKind));
            writer.member("payloadBytes", asset.header().payloadBytes);
            writer.endObject();
        }
        writer.endArray();
    }
    writer.endObject();
    std::cout << '\n';
    return 0;
}
