#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/UploadTicket.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
                                                   Tina::Core::AssetId spriteId)
{
    std::vector<std::byte> pixels{
        std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
        std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255},
        std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{0},   std::byte{255},
    };
    auto texPayload = Tina::AssetFormat::writeTexture2DPayloadBytes(Tina::AssetFormat::Texture2DPayloadDesc{
        .width = 2,
        .height = 2,
        .pixelFormat = Tina::AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
        .pixels = pixels,
    });
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
    if (!texPayload || !spritePayload)
    {
        return Tina::Core::failure(texPayload ? spritePayload.error() : texPayload.error());
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
    return Tina::Asset::cookAndPublishCatalogPackage(toUtf8(root), request);
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
    const auto spriteId = *Tina::Core::AssetId::fromBytes(idBytes(3U));

    std::filesystem::path root;
    std::error_code ec;
    if (options.catalogRoot.empty())
    {
        root = std::filesystem::temp_directory_path() / "tina_sample_asset_catalog";
        std::filesystem::remove_all(root, ec);
        if (const auto status = synthesizeCatalog(root, textureId, spriteId); !status)
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

    if (const auto status = system->openAndBindCatalog(toUtf8(root)); !status)
    {
        writeError(status.error());
        return 1;
    }

    // Prefer Sprite seed 3; else first Sprite; else first Texture2D; else first catalog entry id.
    Tina::Core::AssetId requestId = spriteId;
    if (!system->catalog() || !system->catalog()->find(requestId))
    {
        if (auto sprite = system->catalogFirstIdOfKind(Tina::AssetFormat::AssetKind::Sprite))
        {
            requestId = *sprite;
        } else if (auto tex = system->catalogFirstIdOfKind(Tina::AssetFormat::AssetKind::Texture2D))
        {
            requestId = *tex;
        } else if (system->catalog() && system->catalog()->entryCount() > 0)
        {
            requestId = system->catalog()->entry(0)->assetId;
        } else
        {
            requestId = {};
        }
    }
    if (!requestId)
    {
        std::cerr << "{\"status\":\"error\",\"message\":\"catalog has no entries\"}\n";
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

    Tina::Core::u16 texW = 0;
    Tina::Core::u16 texH = 0;
    float ppu = 0.0f;
    bool parsedTyped = false;
    if (const auto* file = lease->get())
    {
        if (file->header().assetKind == Tina::AssetFormat::AssetKind::Sprite)
        {
            if (auto sprite = Tina::Asset::parseSpriteFromCooked(*file))
            {
                ppu = sprite->pixelsPerUnit;
                parsedTyped = true;
            }
            if (auto texHandle = system->findFirstLoadedOfKind(Tina::AssetFormat::AssetKind::Texture2D))
            {
                if (const auto* texFile = system->tryGet(*texHandle))
                {
                    if (auto tex = Tina::Asset::parseTexture2DFromCooked(*texFile))
                    {
                        texW = tex->width;
                        texH = tex->height;
                        parsedTyped = true;
                    }
                }
            }
        } else if (file->header().assetKind == Tina::AssetFormat::AssetKind::Texture2D)
        {
            if (auto tex = Tina::Asset::parseTexture2DFromCooked(*file))
            {
                texW = tex->width;
                texH = tex->height;
                parsedTyped = true;
            }
        }
    }

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
              << ",\"typedPayload\":" << (parsedTyped ? "true" : "false")
              << ",\"textureWidth\":" << texW << ",\"textureHeight\":" << texH
              << ",\"spritePpu\":" << ppu
              << ",\"task\":\"bounded_io\""
              << ",\"upload\":\"null_ledger\""
              << ",\"catalog\":\"" << (options.catalogRoot.empty() ? "synthetic" : "external") << "\"}\n";
    return 0;
}
