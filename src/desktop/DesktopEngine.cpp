#include <tina/desktop/DesktopEngine.hpp>

#include <tina/audio/AudioEngine.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UITextSystem.hpp>

#if defined(TINA_HAS_UI_FREETYPE)
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#endif

#include "render/bgfx/BgfxRenderDevice.hpp"

#include <exception>
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

[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>> createEngineImpl(const EngineConfig& config,
                                                                         CreateEngineOptions options) noexcept
{
    try
    {
        const bool followSystemColorScheme = options.followSystemColorScheme;
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
                        [followSystemColorScheme, acceptFileDropEvents = options.acceptFileDropEvents](
                            const Platform::PlatformBackendCreateParams& params) {
                            Platform::PlatformBackendCreateParams desktopParams = params;
                            desktopParams.publishSystemColorSchemeEvents = followSystemColorScheme;
                            desktopParams.acceptFileDropEvents = acceptFileDropEvents;
                            return Platform::createGlfwWindowSurfacePlatformBackend(desktopParams);
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
        };

#if defined(TINA_HAS_UI_FREETYPE)
        // The caller owns the font. Resolving one here would mean compiling a path
        // into the library, which is what shipped a dead build-machine path to every
        // installed game; Desktop::resolveUiFontBytes() is the opt-in helper.
        std::shared_ptr<std::vector<std::byte>> fontBytes = std::move(options.uiFontBytes);
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
                    const auto open = (*context)->text().openTextFont(
                        std::span<const std::byte>(fontBytes->data(), fontBytes->size()));
                    if (!open)
                    {
                        return Core::failure(open.error());
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
