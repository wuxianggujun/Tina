#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSpriteRender.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include "render/bgfx/BgfxRenderDevice.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "../common/SampleSpriteFrameResource.hpp"
#include <vector>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 ProductSpriteBindingKey = 1;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 texturesUploaded = 0;
};

void writeJsonString(std::ostream& output, std::string_view value)
{
    output.put('"');
    for (const unsigned char byte : value)
    {
        if (byte == '"' || byte == '\\')
        {
            output.put('\\');
            output.put(static_cast<char>(byte));
        } else if (byte >= 0x20U)
        {
            output.put(static_cast<char>(byte));
        }
    }
    output.put('"');
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d_catalog\",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
}

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argc, char** argv)
{
    SampleOptions options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument.starts_with("--frames="))
        {
            const auto text = argument.substr(std::string_view{"--frames="}.size());
            u64 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size() || value == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "invalid --frames");
            }
            options.targetFrameCount = value;
            continue;
        }
        if (argument.starts_with("--frame-delay-ms="))
        {
            const auto text = argument.substr(std::string_view{"--frame-delay-ms="}.size());
            u32 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "invalid --frame-delay-ms");
            }
            options.frameDelayMilliseconds = value;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "unsupported argument");
    }
    return options;
}

// Captures the created render device so the game can upload textures after bootstrap.
class DeviceCapture final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept
    {
        device_ = device;
    }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept
    {
        return device_;
    }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

class CapturingRenderDevice final : public Tina::Render::IRenderDevice {
  public:
    CapturingRenderDevice(std::unique_ptr<Tina::Render::IRenderDevice> inner, DeviceCapture& capture) noexcept
        : inner_(std::move(inner)), capture_(&capture)
    {
        capture_->set(this);
    }

    ~CapturingRenderDevice() override
    {
        if (capture_ != nullptr && capture_->get() == this)
        {
            capture_->set(nullptr);
        }
    }

    [[nodiscard]] Tina::Core::Result<Tina::Render::RenderFrameSubmission>
    submitFrame(const Tina::Render::RenderFrame& frame) override
    {
        return inner_->submitFrame(frame);
    }
    [[nodiscard]] Tina::Core::Status present() override
    {
        return inner_->present();
    }
    [[nodiscard]] Tina::Render::RenderStatistics statistics() const noexcept override
    {
        return inner_->statistics();
    }
    void shutdown() noexcept override
    {
        inner_->shutdown();
    }
    [[nodiscard]] Tina::Core::Result<Tina::Render::GpuTextureId>
    createTexture2DRgba8(const Tina::Render::Texture2DUploadDesc& desc) override
    {
        return inner_->createTexture2DRgba8(desc);
    }
    [[nodiscard]] Tina::Core::Status destroyTexture2D(Tina::Render::GpuTextureId texture) noexcept override
    {
        return inner_->destroyTexture2D(texture);
    }
    [[nodiscard]] Tina::Core::Status retireTexture2D(
        Tina::Render::GpuTextureId texture, Tina::Render::FramePin& completionPin) noexcept override
    {
        return inner_->retireTexture2D(texture, completionPin);
    }
    [[nodiscard]] Tina::Core::Status drainGpuRetirements() noexcept override
    {
        return inner_->drainGpuRetirements();
    }
    [[nodiscard]] Tina::Core::Status setSprite2DTextureBinding(u32 spriteKey,
                                                               Tina::Render::GpuTextureId texture) noexcept override
    {
        return inner_->setSprite2DTextureBinding(spriteKey, texture);
    }

  private:
    std::unique_ptr<Tina::Render::IRenderDevice> inner_;
    DeviceCapture* capture_ = nullptr;
};

struct CatalogResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::unique_ptr<Tina::Asset::AssetSystem> system{};
    Tina::Asset::AssetHandle spriteHandle{};
    Tina::Asset::AssetHandle textureHandle{};
    Tina::Render::GpuTextureId gpuTexture{};
    std::filesystem::path catalogRoot{};
};

[[nodiscard]] Tina::Core::Status prepareCatalog(CatalogResources& resources)
{
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Tina::Core::AssetId::fromBytes(idBytes(3U));

    // 8x8 checkerboard for a visible product texture.
    constexpr u16 Size = 8;
    std::vector<std::byte> pixels(static_cast<std::size_t>(Size) * Size * 4U);
    for (u16 y = 0; y < Size; ++y)
    {
        for (u16 x = 0; x < Size; ++x)
        {
            const bool light = ((x / 2) + (y / 2)) % 2 == 0;
            const std::size_t offset = (static_cast<std::size_t>(y) * Size + x) * 4U;
            pixels[offset + 0] = light ? std::byte{255} : std::byte{40};
            pixels[offset + 1] = light ? std::byte{80} : std::byte{180};
            pixels[offset + 2] = light ? std::byte{120} : std::byte{255};
            pixels[offset + 3] = std::byte{255};
        }
    }
    auto texPayload = Tina::AssetFormat::writeTexture2DPayloadBytes(Tina::AssetFormat::Texture2DPayloadDesc{
        .width = Size,
        .height = Size,
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

    resources.catalogRoot = std::filesystem::temp_directory_path() / "tina_sample_2d_catalog_pkg";
    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);
    const auto rootUtf8 = [&] {
        const auto u8 = resources.catalogRoot.u8string();
        return std::string(u8.begin(), u8.end());
    }();
    {
        auto cookStatus = Tina::Asset::cookAndPublishCatalogPackage(rootUtf8, request);
        if (!cookStatus)
        {
            return cookStatus;
        }
    }

    auto system = Tina::Asset::AssetSystem::Create(Tina::Asset::AssetSystemConfig{
        .storeCapacity = 16,
        .memoryResource = &resources.memory,
        .batch =
            Tina::Asset::CookedAssetBatchLoadConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory},
                .memoryResource = &resources.memory,
            },
        .requireTyped2dPayloads = true,
    });
    if (!system)
    {
        return Tina::Core::failure(std::move(system.error()));
    }
    {
        auto bindStatus = system->openAndBindCatalog(rootUtf8);
        if (!bindStatus)
        {
            return bindStatus;
        }
    }
    auto loaded = system->load(std::array{spriteId});
    if (!loaded)
    {
        return Tina::Core::failure(std::move(loaded.error()));
    }
    resources.spriteHandle = (*loaded)[0];
    auto textureHandle = system->find(textureId);
    if (!textureHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle, "texture not loaded");
    }
    resources.textureHandle = *textureHandle;
    resources.system = std::make_unique<Tina::Asset::AssetSystem>(std::move(*system));
    return Tina::Core::success();
}

class Catalog2DState final : public Tina::IGameState {
  public:
    Catalog2DState(SampleOptions options, LifecycleCounters& counters, CatalogResources& resources,
                   DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        ++counters_->stateEnters;
        auto* device = capture_->get();
        if (device == nullptr || resources_->system == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "render device or catalog missing");
        }
        const auto* textureFile = resources_->system->tryGet(resources_->textureHandle);
        if (textureFile == nullptr)
        {
            return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "texture CPU payload missing");
        }
        auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, *textureFile);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        if (const auto status = device->setSprite2DTextureBinding(ProductSpriteBindingKey, *texture); !status)
        {
            (void)device->destroyTexture2D(*texture);
            return status;
        }
        resources_->gpuTexture = *texture;
        ++counters_->texturesUploaded;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        if (auto* device = capture_->get(); device != nullptr && resources_->gpuTexture)
        {
            (void)device->setSprite2DTextureBinding(ProductSpriteBindingKey, {});
            const auto retirement = resources_->system->retireTexture2D(
                *device, resources_->textureHandle, resources_->gpuTexture);
            if (!retirement)
            {
                // Capability fallback keeps the old logical-destroy behavior;
                // the bgfx device still owns the native shutdown drain.
                (void)device->destroyTexture2D(resources_->gpuTexture);
                (void)resources_->system->unload(resources_->textureHandle);
            }
            resources_->gpuTexture = {};
            resources_->textureHandle = {};
        }
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        if (options_.frameDelayMilliseconds != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        if (counters_->frameUpdates >= options_.targetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        auto& writer = context.renderSceneWriter();
        if (auto status = writer.setCamera2D(Tina::Render::RenderCamera2DInput{
                                                 .stableCameraKey = 1,
                                                 .centerX = 0.0F,
                                                 .centerY = 0.0F,
                                                 .worldWidth = 16.0F,
                                                 .worldHeight = 9.0F,
                                                 .actualPixelsPerMeter = 64.0F,
                                             });
            !status)
        {
            return status;
        }

        if (resources_->system == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "catalog system missing");
        }
        const auto* spriteFile = resources_->system->tryGet(resources_->spriteHandle);
        const auto* textureFile = resources_->system->tryGet(resources_->textureHandle);
        if (spriteFile == nullptr)
        {
            return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "sprite not ready");
        }

        const float phase = static_cast<float>(context.frameTiming().frameIndex) * 0.03F;
        auto texture = spriteFrameResource_.intern(context.frameResourceSink(), ProductSpriteBindingKey);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        auto sprite = Tina::Asset::makeSpriteRenderInput(
            *spriteFile, textureFile, *texture,
            Tina::Asset::SpriteRenderParams{
                .stableEntityKey = 1,
                .centerX = 0.0F,
                .centerY = 0.0F,
                .rotationRadians = phase,
                .scaleX = 1.5F,
                .scaleY = 1.5F,
                .red = 255,
                .green = 255,
                .blue = 255,
                .alpha = 255,
            });
        if (!sprite)
        {
            return Tina::Core::failure(std::move(sprite.error()));
        }
        if (auto status = writer.addSprite2D(*sprite); !status)
        {
            return status;
        }
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    CatalogResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
    mutable Tina::Samples::SampleSpriteFrameResource spriteFrameResource_{};
};

class Catalog2DApplication final : public Tina::IGameApplication {
  public:
    Catalog2DApplication(SampleOptions options, LifecycleCounters& counters, CatalogResources& resources,
                         DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<Catalog2DState>(options_, *counters_, *resources_, *capture_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_->applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    CatalogResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineCompositionFactories createFactories(DeviceCapture& capture)
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem =
            [](const Tina::Task::TaskSystemCreateParams& params) {
                Tina::Task::TaskSystemCreateParams effective = params;
                if (effective.ioWorkerCount == 0)
                {
                    effective.ioWorkerCount = 1;
                }
                if (effective.ioQueueCapacity == 0)
                {
                    effective.ioQueueCapacity = 64;
                }
                if (effective.mainQueueCapacity == 0)
                {
                    effective.mainQueueCapacity = 64;
                }
                return Tina::Task::createBoundedTaskSystem(effective);
            },
        .platformRender =
            Tina::WindowSurfacePlatformRenderFactories{
                .createWindowSurfacePlatformBackend = Tina::Platform::createGlfwWindowSurfacePlatformBackend,
                .createWindowSurfaceRenderDevice =
                    [&capture](const Tina::Render::RenderDeviceCreateParams& params,
                               Tina::Integration::NativeWindowSurfaceLease lease)
                    -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
                        auto device = Tina::Render::Bgfx::createBgfxRenderDevice(params, std::move(lease));
                        if (!device)
                        {
                            return device;
                        }
                        std::unique_ptr<Tina::Render::IRenderDevice> capturing =
                            std::make_unique<CapturingRenderDevice>(std::move(*device), capture);
                        return capturing;
                    },
            },
    };
}

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Catalog 2D Sample";
    config.primaryWindow.title = "Tina Catalog Texture2D + Sprite";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    return config;
}

} // namespace

int main(int argc, char** argv)
{
    auto options = parseOptions(argc, argv);
    if (!options)
    {
        writeError(options.error());
        return 2;
    }

    DeviceCapture capture{};
    auto host = Tina::EngineHost::Create(createEngineConfig(), createFactories(capture));
    if (!host)
    {
        writeError(host.error());
        return 1;
    }

    // Declared after host so AssetSystem (and any retirement pins) drains while
    // the captured RenderDevice is still alive during reverse-order teardown.
    CatalogResources resources{};
    if (const auto status = prepareCatalog(resources); !status)
    {
        writeError(status.error());
        return 1;
    }

    LifecycleCounters counters{};
    Catalog2DApplication application{*options, counters, resources, capture};
    auto run = (*host)->run(application);
    if (!run)
    {
        writeError(run.error());
        return 1;
    }

    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);
    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_2d_catalog\""
              << ",\"frames\":" << counters.frameUpdates
              << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"texturesUploaded\":" << counters.texturesUploaded
              << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns
              << ",\"exit\":\""
              << ((*run == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame)
                      ? "GameRequestedExitAfterCurrentFrame"
                      : "Other")
              << "\"}\n";
    return 0;
}
