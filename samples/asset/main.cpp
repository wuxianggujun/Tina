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

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;
using Tina::Core::usize;
using Bytes = std::vector<std::byte>;

void putU8(Bytes& bytes, usize offset, u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}
void putU16(Bytes& bytes, usize offset, u16 value)
{
    putU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    putU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
}
void putU32(Bytes& bytes, usize offset, u32 value)
{
    for (usize index = 0; index < 4U; ++index)
    {
        putU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}
void putU64(Bytes& bytes, usize offset, u64 value)
{
    for (usize index = 0; index < 8U; ++index)
    {
        putU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}
template <usize Size> void putFixed(Bytes& bytes, usize offset, const std::array<std::byte, Size>& value)
{
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

[[nodiscard]] u64 alignUp(u64 value, u32 alignment)
{
    return (value + alignment - 1U) & ~(static_cast<u64>(alignment) - 1U);
}

[[nodiscard]] Bytes makeCooked(u8 seed, Tina::AssetFormat::AssetKind kind)
{
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    constexpr u32 PayloadAlignment = 16U;
    const auto payloadOffset = alignUp(Tina::AssetFormat::Wire::CookedAssetHeaderBytes, PayloadAlignment);
    const auto fileBytes = payloadOffset + Payload.size();
    Bytes bytes(static_cast<usize>(fileBytes), std::byte{0});

    putFixed(bytes, 0U, Tina::AssetFormat::Wire::CookedAssetMagic);
    putU16(bytes, 8U, Tina::AssetFormat::Wire::SchemaMajor);
    putU16(bytes, 10U, Tina::AssetFormat::Wire::SchemaMinor);
    putU32(bytes, 12U, Tina::AssetFormat::Wire::CookedAssetHeaderBytes);
    putU16(bytes, 16U, static_cast<u16>(kind));
    putU16(bytes, 18U, 1U);
    putU16(bytes, 20U, static_cast<u16>(Tina::AssetFormat::TargetPlatform::WindowsX64));
    putU8(bytes, 22U, static_cast<u8>(Tina::AssetFormat::EndianTag::Little));
    putU8(bytes, 23U, static_cast<u8>(Tina::AssetFormat::HashAlgorithm::Xxh3_128V1));
    putFixed(bytes, 32U, idBytes(seed));
    putU64(bytes, 64U, Tina::AssetFormat::Wire::CookedAssetHeaderBytes);
    putU32(bytes, 72U, 0U);
    putU32(bytes, 76U, Tina::AssetFormat::Wire::DependencyEntryBytes);
    putU64(bytes, 80U, payloadOffset);
    putU64(bytes, 88U, Payload.size());
    putU32(bytes, 96U, PayloadAlignment);
    putU64(bytes, 104U, fileBytes);
    putFixed(bytes, static_cast<usize>(payloadOffset), Payload);

    const auto digest = Tina::Core::digestContentHashV1(Payload);
    if (!digest)
    {
        return {};
    }
    putFixed(bytes, 48U, digest->bytes());
    return bytes;
}

[[nodiscard]] Bytes makeManifest(u64 textureBytes, Tina::Core::ContentHash hash, u64 materialBytes)
{
    const auto entryTable = Tina::AssetFormat::Wire::CookedManifestHeaderBytes;
    const auto dependencyOffset = entryTable + 2U * Tina::AssetFormat::Wire::ManifestEntryBytes;
    const auto fileBytes = dependencyOffset + Tina::AssetFormat::Wire::DependencyEntryBytes;
    Bytes bytes(static_cast<usize>(fileBytes), std::byte{0});

    putFixed(bytes, 0U, Tina::AssetFormat::Wire::CookedManifestMagic);
    putU16(bytes, 8U, Tina::AssetFormat::Wire::SchemaMajor);
    putU16(bytes, 10U, Tina::AssetFormat::Wire::SchemaMinor);
    putU32(bytes, 12U, Tina::AssetFormat::Wire::CookedManifestHeaderBytes);
    putU16(bytes, 16U, static_cast<u16>(Tina::AssetFormat::TargetPlatform::WindowsX64));
    putU8(bytes, 18U, static_cast<u8>(Tina::AssetFormat::EndianTag::Little));
    putU8(bytes, 19U, static_cast<u8>(Tina::AssetFormat::HashAlgorithm::Xxh3_128V1));
    putU32(bytes, 24U, 2U);
    putU32(bytes, 28U, Tina::AssetFormat::Wire::ManifestEntryBytes);
    putU32(bytes, 32U, 1U);
    putU32(bytes, 36U, Tina::AssetFormat::Wire::DependencyEntryBytes);
    putU64(bytes, 40U, entryTable);
    putU64(bytes, 48U, dependencyOffset);
    putU64(bytes, 56U, fileBytes);

    putFixed(bytes, entryTable, idBytes(1U));
    putFixed(bytes, entryTable + 16U, hash.bytes());
    putU16(bytes, entryTable + 32U, static_cast<u16>(Tina::AssetFormat::AssetKind::Texture2D));
    putU16(bytes, entryTable + 34U, 1U);
    putU32(bytes, entryTable + 40U, 0U);
    putU32(bytes, entryTable + 44U, 0U);
    putU64(bytes, entryTable + 48U, textureBytes);

    const auto materialOffset = entryTable + Tina::AssetFormat::Wire::ManifestEntryBytes;
    putFixed(bytes, materialOffset, idBytes(2U));
    putFixed(bytes, materialOffset + 16U, hash.bytes());
    putU16(bytes, materialOffset + 32U, static_cast<u16>(Tina::AssetFormat::AssetKind::Material));
    putU16(bytes, materialOffset + 34U, 1U);
    putU32(bytes, materialOffset + 40U, 0U);
    putU32(bytes, materialOffset + 44U, 1U);
    putU64(bytes, materialOffset + 48U, materialBytes);

    putFixed(bytes, dependencyOffset, idBytes(1U));
    putU16(bytes, dependencyOffset + 16U, static_cast<u16>(Tina::AssetFormat::AssetKind::Texture2D));
    putU16(bytes, dependencyOffset + 18U, static_cast<u16>(Tina::AssetFormat::DependencyFlags::Required));
    return bytes;
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes)
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
    const auto digest = Tina::Core::digestContentHashV1(Payload);
    if (!digest)
    {
        writeError(digest.error());
        return 1;
    }

    const auto textureBytes = makeCooked(1U, Tina::AssetFormat::AssetKind::Texture2D);
    const auto materialBytes = makeCooked(2U, Tina::AssetFormat::AssetKind::Material);
    if (textureBytes.empty() || materialBytes.empty())
    {
        std::cerr << "{\"status\":\"error\",\"message\":\"failed to synthesize cooked assets\"}\n";
        return 1;
    }

    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    const auto root = std::filesystem::temp_directory_path() / "tina_sample_asset_catalog";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    writeBytes(root / "manifest.tmnft", makeManifest(textureBytes.size(), *digest, materialBytes.size()));
    writeBytes(root / std::filesystem::u8path(
                   Tina::AssetFormat::makeCookedArtifactPath(Tina::AssetFormat::AssetKind::Texture2D, textureId)
                       ->view()),
               textureBytes);
    writeBytes(root / std::filesystem::u8path(
                   Tina::AssetFormat::makeCookedArtifactPath(Tina::AssetFormat::AssetKind::Material, materialId)
                       ->view()),
               materialBytes);

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
              << ",\"upload\":\"null_ledger\"}\n";
    return 0;
}
