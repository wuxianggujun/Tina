#include <tina/desktop/DesktopEngine.hpp>

#include <tina/audio/AudioEngine.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>
#include <tina/ui/UIContext.hpp>

#if defined(TINA_HAS_UI_FREETYPE)
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#endif

#include "render/bgfx/BgfxRenderDevice.hpp"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Desktop {
namespace {

[[nodiscard]] Core::Error createBootstrapBoundaryError(Core::ErrorCode errorCode)
{
    Core::Error error{errorCode, "An exception crossed the Desktop bootstrap boundary"};
    error.addContext("Desktop::CreateEngine");
    return error;
}

#if defined(TINA_HAS_UI_FREETYPE)
[[nodiscard]] std::shared_ptr<std::vector<std::byte>> loadFontFixtureBytes(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    auto bytes = std::make_shared<std::vector<std::byte>>(size);
    if (size > 0)
    {
        input.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size));
    }
    if (!input)
    {
        return {};
    }
    return bytes;
}
#endif

[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>> createEngineImpl(const EngineConfig& config,
                                                                         CreateEngineOptions options) noexcept
{
    try
    {
        WindowSurfaceRenderDeviceWrap wrap = std::move(options.wrapWindowSurfaceRenderDevice);

        EngineCompositionFactories factories{
            .createMonotonicClock = []() -> Core::Result<std::unique_ptr<Core::IMonotonicClock>> {
                std::unique_ptr<Core::IMonotonicClock> clock = std::make_unique<Core::SteadyMonotonicClock>();
                return clock;
            },
            .createTaskSystem =
                [](const Task::TaskSystemCreateParams& params) {
                    // Production Desktop: bounded IO + interactive CPU workers (ADR 0017 / TASK-001).
                    // Samples/tests may still inject DisabledTaskSystem or call createBoundedTaskSystem
                    // with cpuWorkerCount=0 for IO-only graphs.
                    const unsigned hardwareConcurrency = std::thread::hardware_concurrency();
                    const Task::TaskSystemCreateParams effective = Task::resolveDesktopTaskSystemParams(
                        params,
                        hardwareConcurrency == 0U ? 1U : static_cast<Core::u32>(hardwareConcurrency));
                    return Task::createBoundedTaskSystem(effective);
                },
            .platformRender =
                WindowSurfacePlatformRenderFactories{
                    .createWindowSurfacePlatformBackend =
                        [](const Platform::PlatformBackendCreateParams& params) {
                            return Platform::createGlfwWindowSurfacePlatformBackend(params);
                        },
                    .createWindowSurfaceRenderDevice =
                        [wrap = std::move(wrap)](const Render::RenderDeviceCreateParams& params,
                                                 Integration::NativeWindowSurfaceLease lease) mutable
                            -> Core::Result<std::unique_ptr<Render::IRenderDevice>> {
                            auto device = Render::Bgfx::createBgfxRenderDevice(params, std::move(lease));
                            if (!device)
                            {
                                return device;
                            }
                            if (!wrap)
                            {
                                return device;
                            }
                            // move_only_function::operator() is non-const; lambda must be mutable.
                            return wrap(std::move(*device));
                        },
                },
            // M11-A15: production Desktop always owns a Disabled AudioEngine so
            // Gameplay phases can playOneShotPcm without a separate factory.
            // miniaudio device attach remains optional product wiring.
            .createAudioEngine =
                []() -> Core::Result<Audio::AudioEngine> {
                    return Audio::AudioEngine::Create(Audio::AudioEngineConfig{
                        .voiceCapacity = 32,
                        .commandCapacity = 64,
                        .completionCapacity = 64,
                    });
                },
            // RENDER-FENCE prep: typed Bgfx ledger injection point. Still present-sync
            // (complete on present-return); not a true GPU fence completion path.
            .createSubmissionCompletionLedger =
                []() -> Core::Result<std::unique_ptr<Render::ISubmissionCompletionLedger>> {
                    std::unique_ptr<Render::ISubmissionCompletionLedger> ledger =
                        std::make_unique<Render::BgfxSubmissionCompletionLedger>();
                    return ledger;
                },
        };

#if defined(TINA_HAS_UI_FREETYPE)
        // Optional fixture only: TINA_DESKTOP_UI_FONT_PATH / TINA_UI_FONT_PATH / env / repo file.
        // Games should inject their own font bytes; empty path keeps placeholder UI text path.
        std::shared_ptr<std::vector<std::byte>> fontBytes{};
#if defined(TINA_DESKTOP_UI_FONT_PATH)
        fontBytes = loadFontFixtureBytes(TINA_DESKTOP_UI_FONT_PATH);
#elif defined(TINA_UI_FONT_PATH)
        fontBytes = loadFontFixtureBytes(TINA_UI_FONT_PATH);
#else
        if (const char* envPath = std::getenv("TINA_UI_FONT_PATH"); envPath != nullptr && envPath[0] != '\0')
        {
            fontBytes = loadFontFixtureBytes(envPath);
        }
#endif
        if (fontBytes && !fontBytes->empty())
        {
            factories.createPrimaryWindowUIContext =
                [fontBytes](
                    Platform::WindowId ownerWindow,
                    const UI::UIContextCapacityConfig& capacities,
                    std::pmr::memory_resource& resource) -> Core::Result<std::unique_ptr<UI::UIContext>> {
                    auto rasterizer = UI::createFreeTypeTextRasterizer({}, resource);
                    if (!rasterizer)
                    {
                        return Core::failure(std::move(rasterizer.error()));
                    }
                    auto context = UI::UIContext::Create(
                        ownerWindow, capacities, std::move(*rasterizer), resource);
                    if (!context)
                    {
                        return Core::failure(std::move(context.error()));
                    }
                    const auto open = (*context)->openTextFont(
                        std::span<const std::byte>(fontBytes->data(), fontBytes->size()));
                    if (!open)
                    {
                        return Core::failure(std::move(open.error()));
                    }
                    return std::move(*context);
                };
        }
#endif

        return EngineHost::Create(config, std::move(factories));
    } catch (const std::bad_alloc&)
    {
        return Core::failure(createBootstrapBoundaryError(Core::CoreErrorCode::OutOfMemory));
    } catch (const std::exception&)
    {
        return Core::failure(createBootstrapBoundaryError(RuntimeErrorCode::EngineFactoryThrewException));
    } catch (...)
    {
        return Core::failure(createBootstrapBoundaryError(RuntimeErrorCode::EngineFactoryThrewException));
    }
}

} // namespace

Core::Result<std::unique_ptr<EngineHost>> CreateEngine(const EngineConfig& config) noexcept
{
    return createEngineImpl(config, CreateEngineOptions{});
}

Core::Result<std::unique_ptr<EngineHost>> CreateEngine(const EngineConfig& config,
                                                      CreateEngineOptions options) noexcept
{
    return createEngineImpl(config, std::move(options));
}

} // namespace Tina::Desktop
