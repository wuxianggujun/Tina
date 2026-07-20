#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/UploadTicket.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory_resource>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Tina::Core::u32;
using Tina::Core::u8;

struct Options final {
    u32 maxFrames = 60;
    std::string catalogRoot;
    bool deleteCatalogOnExit = true;
};

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char*>(static_cast<const void*>(bytes.data())),
                 static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"domain\":" << static_cast<unsigned>(error.code.domain)
              << ",\"code\":" << error.code.value << ",\"message\":\"" << error.message << "\"}\n";
}

[[nodiscard]] Tina::Core::Result<Options> parseOptions(int argc, char** argv)
{
    Options options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument.starts_with("--frames="))
        {
            const auto valueText = argument.substr(std::string_view{"--frames="}.size());
            u32 value = 0;
            const auto [end, err] = std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
            if (err != std::errc{} || end != valueText.data() + valueText.size() || value == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "--frames must be > 0");
            }
            options.maxFrames = value;
            continue;
        }
        if (argument.starts_with("--catalog="))
        {
            options.catalogRoot.assign(argument.substr(std::string_view{"--catalog="}.size()));
            options.deleteCatalogOnExit = false;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "usage: tina_sample_asset [--frames=N] [--catalog=path]");
    }
    return options;
}

[[nodiscard]] Tina::Core::Status synthesizeCatalog(const std::filesystem::path& root, Tina::Core::AssetId textureId,
                                                   Tina::Core::AssetId materialId)
{
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const std::array materialDeps{Tina::AssetFormat::CookedAssetWriteDependency{
        .assetId = textureId,
        .expectedKind = Tina::AssetFormat::AssetKind::Texture2D,
        .flags = Tina::AssetFormat::DependencyFlags::Required,
    }};
    auto textureBytes = Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
        .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = Payload,
        .computeContentHash = true,
    });
    auto materialBytes = Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
        .assetKind = Tina::AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .dependencies = materialDeps,
        .payload = Payload,
        .computeContentHash = true,
    });
    if (!textureBytes || !materialBytes)
    {
        return Tina::Core::failure(textureBytes ? materialBytes.error() : textureBytes.error());
    }
    const auto payloadHash = *Tina::Core::digestContentHashV1(Payload);
    const std::array entries{
        Tina::AssetFormat::CookedManifestWriteEntry{
            .assetId = textureId,
            .contentHash = payloadHash,
            .assetKind = Tina::AssetFormat::AssetKind::Texture2D,
            .cookedFileBytes = textureBytes->size(),
        },
        Tina::AssetFormat::CookedManifestWriteEntry{
            .assetId = materialId,
            .contentHash = payloadHash,
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
        return Tina::Core::failure(std::move(manifestBytes.error()));
    }
    writeBytes(root / "manifest.tmnft", *manifestBytes);
    writeBytes(root / std::filesystem::u8path(
                   Tina::AssetFormat::makeCookedArtifactPath(Tina::AssetFormat::AssetKind::Texture2D, textureId)
                       ->view()),
               *textureBytes);
    writeBytes(root / std::filesystem::u8path(
                   Tina::AssetFormat::makeCookedArtifactPath(Tina::AssetFormat::AssetKind::Material, materialId)
                       ->view()),
               *materialBytes);
    return Tina::Core::success();
}

} // namespace

int main(int argc, char** argv)
{
    auto optionsResult = parseOptions(argc, argv);
    if (!optionsResult)
    {
        writeError(optionsResult.error());
        return 2;
    }
    const Options options = *optionsResult;

    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Tina::Core::AssetId::fromBytes(idBytes(2U));

    std::filesystem::path root;
    std::error_code ec;
    if (options.catalogRoot.empty())
    {
        root = std::filesystem::temp_directory_path() / "tina_sample_asset_catalog";
        std::filesystem::remove_all(root, ec);
        if (const auto status = synthesizeCatalog(root, textureId, materialId); !status)
        {
            writeError(status.error());
            return 1;
        }
    } else
    {
        root = std::filesystem::u8path(options.catalogRoot);
    }

    auto taskSystem = Tina::Task::createBoundedTaskSystem(Tina::Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 32,
        .mainQueueCapacity = 32,
    });
    if (!taskSystem)
    {
        writeError(taskSystem.error());
        return 1;
    }

    auto ledger =
        Tina::Render::NullUploadLedger::Create(Tina::Render::UploadLedgerConfig{.capacity = 8, .memoryResource = &memory});
    if (!ledger)
    {
        writeError(ledger.error());
        return 1;
    }

    auto system = Tina::Asset::AssetSystem::Create(Tina::Asset::AssetSystemConfig{
        .storeCapacity = 16,
        .memoryResource = &memory,
        .batch =
            Tina::Asset::CookedAssetBatchLoadConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &memory},
                .memoryResource = &memory,
            },
        .queueCapacity = 16,
        .defaultPumpBudget = 4,
        .taskSystem = taskSystem->get(),
        .uploadLedger = &(*ledger),
        .autoGpuUpload = true,
    });
    if (!system)
    {
        writeError(system.error());
        return 1;
    }

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
    auto catalog = Tina::Asset::openCatalogPackage(toUtf8(root), openConfig);
    if (!catalog)
    {
        writeError(catalog.error());
        return 1;
    }

    // Prefer material id seed 2 when present; else first Material entry; else first entry.
    Tina::Core::AssetId requestId = materialId;
    if (!catalog->find(requestId))
    {
        requestId = {};
        for (Tina::Core::u32 index = 0; index < catalog->entryCount(); ++index)
        {
            const auto entry = catalog->entry(index);
            if (!entry)
            {
                continue;
            }
            if (entry->assetKind == Tina::AssetFormat::AssetKind::Material)
            {
                requestId = entry->assetId;
                break;
            }
            if (!requestId)
            {
                requestId = entry->assetId;
            }
        }
    }
    if (!requestId)
    {
        std::cerr << "{\"status\":\"error\",\"message\":\"catalog has no entries\"}\n";
        return 1;
    }

    if (const auto status = system->bindCatalog(toUtf8(root), std::move(*catalog)); !status)
    {
        writeError(status.error());
        return 1;
    }

    auto requested = system->request(std::array{requestId});
    if (!requested)
    {
        writeError(requested.error());
        return 1;
    }

    u32 frames = 0;
    u32 pumps = 0;
    bool gpuReady = false;
    for (; frames < options.maxFrames; ++frames)
    {
        auto stats = system->pump(4);
        if (!stats)
        {
            writeError(stats.error());
            return 1;
        }
        ++pumps;
        if (system->isGpuReady((*requested)[0]))
        {
            gpuReady = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!gpuReady)
    {
        std::cerr << "{\"status\":\"error\",\"message\":\"asset did not reach ReadyGpu within frame budget\","
                  << "\"frames\":" << frames << "}\n";
        (*taskSystem)->shutdownAndJoin();
        if (options.deleteCatalogOnExit)
        {
            std::filesystem::remove_all(root, ec);
        }
        return 1;
    }

    auto lease = system->acquire((*requested)[0]);
    if (!lease)
    {
        writeError(lease.error());
        (*taskSystem)->shutdownAndJoin();
        if (options.deleteCatalogOnExit)
        {
            std::filesystem::remove_all(root, ec);
        }
        return 1;
    }

    // Unload to exercise retirement ledger; lease still holds until scope end, so release first.
    lease = Tina::Asset::AssetLease{};
    const auto unloaded = system->unload((*requested)[0]);
    const auto retirement = system->retirementStats();

    (*taskSystem)->shutdownAndJoin();
    if (options.deleteCatalogOnExit)
    {
        std::filesystem::remove_all(root, ec);
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_asset\""
              << ",\"frames\":" << frames << ",\"pumps\":" << pumps
              << ",\"requestGpuReady\":true"
              << ",\"storeActive\":" << system->store().activeCount()
              << ",\"unloadOk\":" << (unloaded ? "true" : "false")
              << ",\"retirementReleased\":" << retirement.released
              << ",\"retirementLive\":" << retirement.live
              << ",\"task\":\"bounded_io\""
              << ",\"upload\":\"null_ledger\""
              << ",\"catalog\":\"" << (options.catalogRoot.empty() ? "synthetic" : "external") << "\"}\n";
    return 0;
}
