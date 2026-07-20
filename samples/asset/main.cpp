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

[[nodiscard]] Tina::Core::Result<u32> parseMaxFrames(int argc, char** argv)
{
    constexpr std::string_view optionPrefix = "--frames=";
    u32 frames = 60;
    if (argc == 1)
    {
        return frames;
    }
    if (argc != 2)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "usage: tina_sample_asset [--frames=N]");
    }
    const std::string_view argument{argv[1]};
    if (!argument.starts_with(optionPrefix))
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "unsupported argument");
    }
    const auto valueText = argument.substr(optionPrefix.size());
    u32 value = 0;
    const auto [end, err] = std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
    if (err != std::errc{} || end != valueText.data() + valueText.size() || value == 0)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "--frames must be > 0");
    }
    return value;
}

} // namespace

int main(int argc, char** argv)
{
    auto framesResult = parseMaxFrames(argc, argv);
    if (!framesResult)
    {
        writeError(framesResult.error());
        return 2;
    }
    const u32 maxFrames = *framesResult;

    std::pmr::unsynchronized_pool_resource memory;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
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
        writeError(textureBytes ? materialBytes.error() : textureBytes.error());
        return 1;
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
        writeError(manifestBytes.error());
        return 1;
    }

    const auto root = std::filesystem::temp_directory_path() / "tina_sample_asset_catalog";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    writeBytes(root / "manifest.tmnft", *manifestBytes);
    writeBytes(root / std::filesystem::u8path(
                   Tina::AssetFormat::makeCookedArtifactPath(Tina::AssetFormat::AssetKind::Texture2D, textureId)
                       ->view()),
               *textureBytes);
    writeBytes(root / std::filesystem::u8path(
                   Tina::AssetFormat::makeCookedArtifactPath(Tina::AssetFormat::AssetKind::Material, materialId)
                       ->view()),
               *materialBytes);

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
    if (const auto status = system->bindCatalog(toUtf8(root), std::move(*catalog)); !status)
    {
        writeError(status.error());
        return 1;
    }

    auto requested = system->request(std::array{materialId});
    if (!requested)
    {
        writeError(requested.error());
        return 1;
    }

    u32 frames = 0;
    u32 pumps = 0;
    bool gpuReady = false;
    for (; frames < maxFrames; ++frames)
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
        std::cerr << "{\"status\":\"error\",\"message\":\"material did not reach ReadyGpu within frame budget\","
                  << "\"frames\":" << frames << "}\n";
        (*taskSystem)->shutdownAndJoin();
        std::filesystem::remove_all(root, ec);
        return 1;
    }

    auto lease = system->acquire((*requested)[0]);
    if (!lease)
    {
        writeError(lease.error());
        (*taskSystem)->shutdownAndJoin();
        std::filesystem::remove_all(root, ec);
        return 1;
    }

    const auto texture = system->find(textureId);
    const bool textureGpu = texture && system->isGpuReady(*texture);

    (*taskSystem)->shutdownAndJoin();
    std::filesystem::remove_all(root, ec);

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_asset\""
              << ",\"frames\":" << frames << ",\"pumps\":" << pumps
              << ",\"materialGpuReady\":true"
              << ",\"textureGpuReady\":" << (textureGpu ? "true" : "false")
              << ",\"storeActive\":" << system->store().activeCount()
              << ",\"leaseHeld\":true"
              << ",\"task\":\"bounded_io\""
              << ",\"upload\":\"null_ledger\""
              << ",\"writer\":\"asset_format_write\"}\n";
    return 0;
}
