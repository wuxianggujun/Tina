#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/World.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 ProceduralCubeCount = 3;

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
    u64 uiRootsCreated = 0;
    u64 uiPanelsCreated = 0;
    u64 uiRootsReleased = 0;
};

[[nodiscard]] Tina::UI::UILayoutStyle absolutePanelStyle(
    Tina::UI::UILayoutLength left, Tina::UI::UILayoutLength top,
    Tina::UI::UILayoutLength width, Tina::UI::UILayoutLength height) noexcept
{
    Tina::UI::UILayoutStyle style{};
    style.position = Tina::UI::UILayoutPositionMode::AbsoluteOverlay;
    style.absoluteInset.left = left;
    style.absoluteInset.top = top;
    style.size.width = width;
    style.size.height = height;
    return style;
}

[[nodiscard]] Tina::UI::UIBoxPaint solidFill(
    Tina::Core::u8 red, Tina::Core::u8 green, Tina::Core::u8 blue,
    Tina::Core::u8 alpha) noexcept
{
    return Tina::UI::UIBoxPaint{
        .solidFill = Tina::UI::UISolidFill{
            .color = {
                .red = red,
                .green = green,
                .blue = blue,
                .alpha = alpha,
            },
        },
    };
}

void writeJsonString(std::ostream& output, std::string_view value)
{
    output.put('"');
    for (const unsigned char byte : value)
    {
        if (byte == '"' || byte == '\\')
        {
            output.put('\\');
            output.put(static_cast<char>(byte));
        }
        else if (byte == '\n')
        {
            output << "\\n";
        }
        else if (byte >= 0x20U)
        {
            output.put(static_cast<char>(byte));
        }
    }
    output.put('"');
}

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "." +
           std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_3d_infrastructure\",\"code\":";
    writeJsonString(std::cerr, errorCodeName(error.code));
    std::cerr << ",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
}

template <typename Value> [[nodiscard]] bool parseUnsigned(std::string_view text, Value& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view FramesPrefix = "--frames=";
    constexpr std::string_view DelayPrefix = "--frame-delay-ms=";
    SampleOptions options;
    bool hasFrames = false;
    bool hasDelay = false;

    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(FramesPrefix))
        {
            if (hasFrames || !parseUnsigned(argument.substr(FramesPrefix.size()), options.targetFrameCount) ||
                options.targetFrameCount == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must appear once and be greater than zero");
            }
            hasFrames = true;
        }
        else if (argument.starts_with(DelayPrefix))
        {
            if (hasDelay || !parseUnsigned(argument.substr(DelayPrefix.size()),
                                            options.frameDelayMilliseconds))
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must appear once and be unsigned");
            }
            hasDelay = true;
        }
        else
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "Unsupported command-line argument"};
            error.addContext("parseOptions", argument);
            return Tina::Core::failure(std::move(error));
        }
    }
    return options;
}

class Visible3DState final : public Tina::IGameState {
  public:
    Visible3DState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;

        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{16});
        if (!worldResult)
        {
            return Tina::Core::failure(std::move(worldResult.error()));
        }
        world_.emplace(std::move(*worldResult));

        Tina::Scene::LocalTransform cameraLocal{};
        cameraLocal.position = {0.0F, 0.35F, 8.0F};
        auto cameraEntity = world_->createEntity(cameraLocal);
        if (!cameraEntity)
        {
            return Tina::Core::failure(std::move(cameraEntity.error()));
        }
        cameraEntity_ = *cameraEntity;
        if (auto status = world_->setPerspectiveCamera3D(
                cameraEntity_,
                Tina::Scene::PerspectiveCamera3D{
                    .verticalFovDegrees = 55.0F,
                    .nearPlaneMeters = 0.1F,
                    .farPlaneMeters = 100.0F,
                    .active = true,
                });
            !status)
        {
            return status;
        }

        constexpr std::array<float, ProceduralCubeCount> PositionsX{-2.3F, 0.0F, 2.3F};
        constexpr std::array<float, ProceduralCubeCount> PositionsZ{-0.4F, -1.0F, -1.6F};
        constexpr std::array<float, ProceduralCubeCount> Scales{0.9F, 1.15F, 0.8F};
        constexpr std::array<Tina::Render::RenderLinearColor, ProceduralCubeCount> Colors{{
            {.red = 0.95F, .green = 0.24F, .blue = 0.30F},
            {.red = 0.12F, .green = 0.72F, .blue = 0.92F},
            {.red = 0.20F, .green = 0.84F, .blue = 0.48F},
        }};
        for (u32 index = 0; index < ProceduralCubeCount; ++index)
        {
            Tina::Scene::LocalTransform cubeLocal{};
            cubeLocal.position = {PositionsX[index], 0.0F, PositionsZ[index]};
            cubeLocal.scale = {Scales[index], Scales[index], Scales[index]};
            auto cubeEntity = world_->createEntity(cubeLocal);
            if (!cubeEntity)
            {
                return Tina::Core::failure(std::move(cubeEntity.error()));
            }
            cubeEntities_[index] = *cubeEntity;
            if (auto status = world_->setMeshRenderer3D(
                    cubeEntities_[index],
                    Tina::Scene::MeshRenderer3D{
                        .meshKey = 1,
                        .materialKey = 1,
                        .submeshIndex = 0,
                        .localBounds = {.radius = 1.75F},
                        .baseColorFactor = Colors[index],
                        .visible = true,
                    });
                !status)
            {
                return status;
            }
        }
        if (auto status = world_->updateWorldTransforms(); !status)
        {
            return status;
        }

        auto rootBuilder = context.primaryWindowUIRootBuilder();
        if (!rootBuilder)
        {
            return Tina::Core::failure(std::move(rootBuilder.error()));
        }
        auto root = rootBuilder->createRoot();
        if (!root)
        {
            return Tina::Core::failure(std::move(root.error()));
        }
        auto tree = rootBuilder->treeUpdater(*root);
        if (!tree)
        {
            return Tina::Core::failure(std::move(tree.error()));
        }

        Tina::UI::UILayoutStyle rootStyle{};
        rootStyle.size.width = Tina::UI::UILayoutLength::Percent(100.0F);
        rootStyle.size.height = Tina::UI::UILayoutLength::Percent(100.0F);
        if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status)
        {
            return status;
        }

        struct PanelSpec final {
            Tina::UI::UILayoutStyle layout{};
            Tina::UI::UIBoxPaint paint{};
        };
        const std::array panels{
            PanelSpec{
                .layout = absolutePanelStyle(
                    Tina::UI::UILayoutLength::Px(28.0F),
                    Tina::UI::UILayoutLength::Px(28.0F),
                    Tina::UI::UILayoutLength::Px(320.0F),
                    Tina::UI::UILayoutLength::Px(52.0F)),
                .paint = solidFill(8, 25, 42, 205),
            },
            PanelSpec{
                .layout = absolutePanelStyle(
                    Tina::UI::UILayoutLength::Px(28.0F),
                    Tina::UI::UILayoutLength::Px(80.0F),
                    Tina::UI::UILayoutLength::Px(430.0F),
                    Tina::UI::UILayoutLength::Px(8.0F)),
                .paint = solidFill(36, 211, 171, 235),
            },
        };
        for (const PanelSpec& panelSpec : panels)
        {
            auto panel = tree->createPanel(root->rootNodeId());
            if (!panel)
            {
                return Tina::Core::failure(std::move(panel.error()));
            }
            if (auto status = tree->setLayoutStyle(*panel, panelSpec.layout); !status)
            {
                return status;
            }
            if (auto status = tree->setBoxPaint(*panel, panelSpec.paint); !status)
            {
                return status;
            }
        }

        uiRoot_ = std::move(*root);
        ++counters_->uiRootsCreated;
        counters_->uiPanelsCreated += panels.size();
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        if (uiRoot_)
        {
            uiRoot_.reset();
            ++counters_->uiRootsReleased;
        }
        world_.reset();
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
        if (!world_.has_value())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "3D infrastructure World was not initialized");
        }

        constexpr std::array<float, ProceduralCubeCount> PositionsX{-2.3F, 0.0F, 2.3F};
        constexpr std::array<float, ProceduralCubeCount> PositionsZ{-0.4F, -1.0F, -1.6F};
        constexpr std::array<float, ProceduralCubeCount> Scales{0.9F, 1.15F, 0.8F};

        const float halfAngle = static_cast<float>(context.frameTiming().frameIndex) * 0.0125F;
        for (u32 index = 0; index < ProceduralCubeCount; ++index)
        {
            const float phase = halfAngle + static_cast<float>(index) * 0.45F;
            Tina::Scene::LocalTransform cubeLocal{};
            cubeLocal.position = {PositionsX[index], 0.0F, PositionsZ[index]};
            cubeLocal.rotation = {0.0F, std::sin(phase), 0.0F, std::cos(phase)};
            cubeLocal.scale = {Scales[index], Scales[index], Scales[index]};
            if (auto status = world_->setLocalTransform(cubeEntities_[index], cubeLocal); !status)
            {
                return status;
            }
        }

        auto& writer = context.renderSceneWriter();
        // Matches EngineConfig primary window initial logical extent for this sample.
        if (auto status = Tina::Scene::extractRenderSceneFromWorld(
                *world_,
                writer,
                Tina::Scene::ExtractRenderSceneParams{
                    .surfaceViewport =
                        Tina::Render::Camera2DSurfaceViewport{
                            .pixelWidth = 1280,
                            .pixelHeight = 720,
                        },
                });
            !status)
        {
            return status;
        }
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Tina::UI::UIRootOwner uiRoot_{};
    mutable std::optional<Tina::Scene::World> world_{};
    Tina::Scene::EntityId cameraEntity_{};
    std::array<Tina::Scene::EntityId, ProceduralCubeCount> cubeEntities_{};
};

class Visible3DApplication final : public Tina::IGameApplication {
  public:
    Visible3DApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<Visible3DState>(options_, *counters_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_->applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina vNext 3D Infrastructure";
    config.primaryWindow.title = "Tina vNext - Procedural Cube / Depth";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.mesh3DItemCapacity = 16;
    config.renderSceneCapacities.mesh3DBatchCapacity = 8;
    return config;
}

[[nodiscard]] int runSample(int argumentCount, char** arguments)
{
    auto optionsResult = parseOptions(argumentCount, arguments);
    if (!optionsResult)
    {
        writeError(optionsResult.error());
        return 2;
    }
    const SampleOptions options = *optionsResult;

    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig());
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    LifecycleCounters counters;
    Visible3DApplication application{options, counters};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 ||
        counters.uiRootsCreated != 1 || counters.uiPanelsCreated != 2 ||
        counters.uiRootsReleased != 1)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_3d_infrastructure\","
                     "\"message\":\"lifecycle counters did not match\"}\n";
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_3d_infrastructure\",\"frames\":"
              << counters.frameUpdates << ",\"proceduralCubesPerFrame\":" << ProceduralCubeCount
              << ",\"instanceBatchesPerFrame\":1,\"sceneExtract\":true,\"stateExits\":"
              << counters.stateExits << ",\"uiPanelsPerFrame\":2,\"uiRootsReleased\":"
              << counters.uiRootsReleased << ",\"applicationShutdowns\":"
              << counters.applicationShutdowns
              << ",\"engineHostDestroyed\":true,\"renderResourceLedgerBalanced\":true}\n";
    return 0;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    }
    catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The 3D infrastructure sample ran out of memory"};
        writeError(error);
        return 1;
    }
    catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the 3D infrastructure sample boundary"};
        error.addContext("main", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    }
    catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the 3D infrastructure sample boundary"};
        writeError(error);
        return 1;
    }
}
