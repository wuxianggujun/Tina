#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageSummary.hpp>
#include <tina/core/error/Error.hpp>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory_resource>
#include <string>
#include <string_view>

namespace {

struct Options final {
    std::string root;
    std::string manifestRelative = "manifest.tmnft";
    bool metadataOnly = false;
    bool skipValidate = false;
    bool listEntries = false;
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

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (const int parseResult = parseArgs(argc, argv, options); parseResult != 0)
    {
        return parseResult == 2 ? 2 : 1;
    }

    std::pmr::unsynchronized_pool_resource memoryResource;
    Tina::Asset::CatalogPackageOpenConfig config{
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
            },
        .manifestRelativePath = options.manifestRelative,
    };

    auto snapshot = Tina::Asset::openCatalogPackage(options.root, config);
    if (!snapshot)
    {
        printErrorJson(snapshot.error());
        return 1;
    }

    auto summary = Tina::Asset::buildCatalogPackageSummary(
        *snapshot,
        Tina::Asset::CatalogPackageSummaryConfig{.memoryResource = &memoryResource,
                                                 .includeEntries = options.listEntries});
    if (!summary)
    {
        printErrorJson(summary.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"entries\":" << summary->entryCount
              << ",\"dependencies\":" << summary->dependencyCount
              << ",\"validated\":" << (options.skipValidate ? "false" : "true")
              << ",\"contentHash\":" << ((!options.skipValidate && !options.metadataOnly) ? "true" : "false");
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
    std::cout << "}\n";
    return 0;
}
