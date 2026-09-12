// 3D Stress Test - 大规模性能压力测试
#include <tina/asset/AssetStore.hpp>
#include <tina/core/text/ParseInteger.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/PointLight3D.hpp>
#include <tina/scene/Transform.hpp>
#include <tina/scene/World.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace UI = Tina::UI;
using Tina::Core::u32;
using Tina::Core::u64;

// 3D 压力测试配置
inline constexpr u32 DefaultMeshCount = 1000;      // 1K meshes
inline constexpr u32 DefaultLightCount = 20;       // 20 点光源
inline constexpr u64 DefaultFrameCount = 600;      // 10秒@60fps
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;

struct Stress3DOptions final {
    u32 meshCount = DefaultMeshCount;
    u32 lightCount = DefaultLightCount;
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    bool enableTracy = true;
};

struct Stress3DCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 totalMeshesRendered = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    double minFrameTimeMs = 999999.0;
    double maxFrameTimeMs = 0.0;
    double totalFrameTimeMs = 0.0;
};

void releaseFixtureFrameResource(void*) noexcept {}

[[nodiscard]] Tina::Core::Result<Tina::Render::FrameResourceRef>
internFixtureFrameResource(Tina::Render::FrameResourceSink& sink,
                           Tina::Render::FrameResourceKind kind) noexcept
{
    Tina::Render::FramePin pin{
        Tina::Render::FramePinKind::Custom,
        1U,
        nullptr,
        &releaseFixtureFrameResource,
    };
    return sink.intern(
        Tina::Render::FrameResourceDescriptor{.kind = kind, .deviceBindingKey = 1U},
        std::move(pin));
}

[[nodiscard]] Tina::Core::Result<Tina::Render::FrameResourceRef>
resolveMeshFixture(void*, Tina::Asset::AssetHandle, Tina::Render::FrameResourceSink& sink) noexcept
{
    return internFixtureFrameResource(sink, Tina::Render::FrameResourceKind::Mesh3DGeometry);
}

[[nodiscard]] Tina::Core::Result<Tina::Render::FrameResourceRef>
resolveMaterialFixture(void*, Tina::Asset::AssetHandle, Tina::Render::FrameResourceSink& sink) noexcept
{
    return internFixtureFrameResource(sink, Tina::Render::FrameResourceKind::Mesh3DMaterial);
}

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<Tina::Core::u16>(code.domain)) + "." +
           std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_stress_3d");
    writer.member("code", errorCodeName(error.code));
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

[[nodiscard]] Tina::Core::Result<Stress3DOptions> parseOptions(int argc, char** argv)
{
    Stress3DOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (arg.starts_with("--meshes=")) {
            if (!Tina::Core::parseUnsigned(arg.substr(9), options.meshCount)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                         "invalid --meshes value");
            }
        } else if (arg.starts_with("--lights=")) {
            if (!Tina::Core::parseUnsigned(arg.substr(9), options.lightCount)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                         "invalid --lights value");
            }
        } else if (arg.starts_with("--frames=")) {
            if (!Tina::Core::parseUnsigned(arg.substr(9), options.targetFrameCount) ||
                options.targetFrameCount == 0) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                         "--frames must be greater than zero");
            }
        } else if (arg.starts_with("--frame-delay-ms=")) {
            if (!Tina::Core::parseUnsigned(arg.substr(17), options.frameDelayMilliseconds)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                         "invalid --frame-delay-ms value");
            }
        } else if (arg == "--no-tracy") {
            options.enableTracy = false;
        }
    }

    return options;
}

class Stress3DState final : public Tina::IGameState {
public:
    Stress3DState(Stress3DOptions options, Stress3DCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override;
    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override;
    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override;
    void onExit(Tina::GameStateExitContext&) noexcept override;

private:
    struct Vec3 {
        float x, y, z;
    };

    Stress3DOptions options_;
    Stress3DCounters* counters_;
    std::optional<Tina::Scene::World> world_;
    Tina::Scene::EntityId cameraEntity_{};
    std::vector<Tina::Scene::EntityId> meshEntities_;
    std::vector<Tina::Scene::EntityId> lightEntities_;
    std::vector<Vec3> angularVelocities_;
};

class Stress3DApplication final : public Tina::IGameApplication {
public:
    Stress3DApplication(Stress3DOptions options, Stress3DCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(
        Tina::GameStartupContext&) override
    {
        return std::make_unique<Stress3DState>(options_, *counters_);
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override {}

private:
    Stress3DOptions options_;
    Stress3DCounters* counters_;
};

Tina::Core::Status Stress3DState::onEnter(Tina::GameStateEnterContext& context)
{
        ++counters_->stateEnters;

        // 创建 World
        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{
            static_cast<u32>(options_.meshCount + options_.lightCount + 100)});
        if (!worldResult) {
            return Tina::Core::failure(std::move(worldResult.error()));
        }
        world_.emplace(std::move(*worldResult));

        // 创建相机实体
        auto cameraEntityResult = world_->createEntity();
        if (!cameraEntityResult) {
            return Tina::Core::failure(std::move(cameraEntityResult.error()));
        }
        cameraEntity_ = *cameraEntityResult;

        const Tina::Scene::LocalTransform cameraTransform{
            .position = {0.0F, 50.0F, 150.0F},
            .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
            .scale = {1.0F, 1.0F, 1.0F},
        };
        if (auto status = world_->setLocalTransform(cameraEntity_, cameraTransform); !status) {
            return status;
        }

        Tina::Scene::PerspectiveCamera3D camera{
            .verticalFovDegrees = 60.0F,
            .nearPlaneMeters = 0.1F,
            .farPlaneMeters = 1000.0F,
        };
        if (auto status = world_->setPerspectiveCamera3D(cameraEntity_, camera); !status) {
            return status;
        }

        // 创建大量 meshes - 网格分布
        std::mt19937 rng(67890);
        std::uniform_real_distribution<float> scaleDist(0.5F, 2.0F);
        std::uniform_real_distribution<float> angularVelDist(-1.0F, 1.0F);

        meshEntities_.reserve(options_.meshCount);
        angularVelocities_.reserve(options_.meshCount);

        const u32 gridSize = static_cast<u32>(std::sqrt(static_cast<float>(options_.meshCount))) + 1;
        const float spacing = 10.0F;

        for (u32 i = 0; i < options_.meshCount; ++i) {
            // 网格分布位置
            const float x = (i % gridSize) * spacing - (gridSize * spacing * 0.5F);
            const float z = (i / gridSize) * spacing - (gridSize * spacing * 0.5F);
            const float y = std::sin(x * 0.1F) * 5.0F + std::cos(z * 0.1F) * 5.0F;

            const Tina::Scene::LocalTransform transform{
                .position = {x, y, z},
                .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
                .scale = {scaleDist(rng), scaleDist(rng), scaleDist(rng)},
            };

            auto entityResult = world_->createEntity(transform);
            if (!entityResult) {
                return Tina::Core::failure(std::move(entityResult.error()));
            }
            meshEntities_.push_back(*entityResult);

            angularVelocities_.push_back({
                angularVelDist(rng),
                angularVelDist(rng),
                angularVelDist(rng)
            });

            // Mesh renderer (使用 fixture 资源)
            Tina::Scene::MeshRenderer3D mesh{
                .mesh = {},
                .material = {},
                .submeshIndex = 0,
                .localBounds = {.radius = 1.0F},
            };
            if (auto status = world_->setMeshRenderer3D(*entityResult, mesh); !status) {
                return status;
            }
        }

        // 创建点光源
        std::uniform_real_distribution<float> lightPosDist(-50.0F, 50.0F);
        std::uniform_real_distribution<float> lightColorDist(0.5F, 1.0F);
        std::uniform_real_distribution<float> intensityDist(50.0F, 200.0F);

        lightEntities_.reserve(options_.lightCount);

        for (u32 i = 0; i < options_.lightCount; ++i) {
            const Tina::Scene::LocalTransform transform{
                .position = {lightPosDist(rng), lightPosDist(rng), lightPosDist(rng)},
                .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
                .scale = {1.0F, 1.0F, 1.0F},
            };

            auto entityResult = world_->createEntity(transform);
            if (!entityResult) {
                return Tina::Core::failure(std::move(entityResult.error()));
            }
            lightEntities_.push_back(*entityResult);

            Tina::Scene::PointLight3D light{
                .color = {
                    lightColorDist(rng),
                    lightColorDist(rng),
                    lightColorDist(rng),
                    1.0F,
                },
                .intensity = intensityDist(rng),
                .influenceRadiusMeters = 50.0F,
            };
            if (auto status = world_->setPointLight3D(*entityResult, light); !status) {
                return status;
            }
        }

        if (auto status = world_->updateWorldTransforms(); !status) {
            return status;
        }

        return Tina::Core::success();
}

Tina::Core::Status Stress3DState::updateFrame(Tina::FrameUpdateContext& context)
{
        Tina::Core::SteadyMonotonicClock clock;
        const auto frameStart = clock.now();
        ++counters_->frameUpdates;

        // 更新 mesh 旋转
        const float deltaTime = 1.0F / 60.0F;

        for (size_t i = 0; i < meshEntities_.size(); ++i) {
            const Tina::Scene::LocalTransform* localTransform = world_->localTransform(meshEntities_[i]);
            if (localTransform == nullptr) {
                continue;
            }

            // 简单的四元数旋转更新
            const auto& angVel = angularVelocities_[i];
            const float angle = std::sqrt(angVel.x * angVel.x + angVel.y * angVel.y + angVel.z * angVel.z) * deltaTime;

            if (angle > 0.001F) {
                const float s = std::sin(angle * 0.5F);
                const float c = std::cos(angle * 0.5F);
                const float invLen = 1.0F / std::sqrt(angVel.x * angVel.x + angVel.y * angVel.y + angVel.z * angVel.z);

                const float qx = angVel.x * invLen * s;
                const float qy = angVel.y * invLen * s;
                const float qz = angVel.z * invLen * s;
                const float qw = c;

                // 四元数乘法
                Tina::Scene::LocalTransform newTransform = *localTransform;
                const float rx = localTransform->rotation.x;
                const float ry = localTransform->rotation.y;
                const float rz = localTransform->rotation.z;
                const float rw = localTransform->rotation.w;

                newTransform.rotation.x = rw * qx + rx * qw + ry * qz - rz * qy;
                newTransform.rotation.y = rw * qy - rx * qz + ry * qw + rz * qx;
                newTransform.rotation.z = rw * qz + rx * qy - ry * qx + rz * qw;
                newTransform.rotation.w = rw * qw - rx * qx - ry * qy - rz * qz;

                if (auto status = world_->setLocalTransform(meshEntities_[i], newTransform); !status) {
                    return status;
                }
            }
        }

        // 更新相机旋转
        const float time = counters_->frameUpdates * (1.0F / 60.0F);
        const float radius = 150.0F;
        const float height = 50.0F + std::sin(time * 0.2F) * 20.0F;

        Tina::Scene::LocalTransform cameraTransform{
            .position = {
                std::cos(time * 0.3F) * radius,
                height,
                std::sin(time * 0.3F) * radius
            },
            .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
            .scale = {1.0F, 1.0F, 1.0F},
        };
        if (auto status = world_->setLocalTransform(cameraEntity_, cameraTransform); !status) {
            return status;
        }

        if (auto status = world_->updateWorldTransforms(); !status) {
            return status;
        }

        // 记录帧时间
        const auto frameEnd = clock.now();
        const double frameTimeMs = std::chrono::duration<double, std::milli>(
            frameEnd - frameStart).count();

        counters_->minFrameTimeMs = std::min(counters_->minFrameTimeMs, frameTimeMs);
        counters_->maxFrameTimeMs = std::max(counters_->maxFrameTimeMs, frameTimeMs);
        counters_->totalFrameTimeMs += frameTimeMs;

        // 检查是否完成
        if (counters_->frameUpdates >= options_.targetFrameCount) {
            context.requestExitAfterFrame();
        }

        // 帧延迟
        if (options_.frameDelayMilliseconds > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(options_.frameDelayMilliseconds));
        }

        return Tina::Core::success();
}

Tina::Core::Status Stress3DState::extractRenderScene(Tina::RenderSceneExtractionContext& context) const
{
        Tina::Scene::ExtractRenderSceneParams params{};
        params.mesh3DBindingResolver.userData = nullptr;
        params.mesh3DBindingResolver.resolve = &resolveMeshFixture;
        params.material3DBindingResolver.userData = nullptr;
        params.material3DBindingResolver.resolve = &resolveMaterialFixture;

        if (auto status = Tina::Scene::extractRenderSceneFromWorld(
            const_cast<Tina::Scene::World&>(*world_), context.renderSceneWriter(), context.frameResourceSink(), params); !status) {
            return status;
        }

        ++counters_->renderExtractions;
        counters_->totalMeshesRendered += options_.meshCount;
        return Tina::Core::success();
}

void Stress3DState::onExit(Tina::GameStateExitContext&) noexcept
{
        ++counters_->stateExits;

        world_.reset();
        meshEntities_.clear();
        lightEntities_.clear();
        angularVelocities_.clear();
}

} // namespace

int main(int argc, char** argv)
{
    auto optionsResult = parseOptions(argc, argv);
    if (!optionsResult) {
        writeError(optionsResult.error());
        return 1;
    }
    const Stress3DOptions options = *optionsResult;

    Stress3DCounters counters{};

    try {
        Tina::EngineConfig config = Tina::EngineConfig::Defaults();
        config.primaryWindow.title = "Tina 3D Stress Test";
        config.primaryWindow.initialLogicalExtent = {1920, 1080};

        auto engineResult = Tina::Desktop::CreateEngine(config);
        if (!engineResult) {
            writeError(engineResult.error());
            return 1;
        }
        auto engine = std::move(*engineResult);

        Stress3DApplication app(options, counters);
        auto runResult = engine->run(app);
        if (!runResult) {
            writeError(runResult.error());
            return 1;
        }
        const auto reason = *runResult;
        if (reason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame) {
            Tina::Core::JsonWriter writer(std::cerr);
            writer.beginObject();
            writer.member("status", "error");
            writer.member("sample", "tina_sample_stress_3d");
            writer.member("message", "engine exited with unexpected reason");
            writer.member("exitReason", static_cast<int>(reason));
            writer.endObject();
            std::cerr << '\n';
            return 1;
        }

    } catch (const std::exception& ex) {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("sample", "tina_sample_stress_3d");
        writer.member("message", "unhandled exception");
        writer.member("what", ex.what());
        writer.endObject();
        std::cerr << '\n';
        return 1;
    }

    // 输出性能统计
    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("sample", "tina_sample_stress_3d");
    writer.member("schema", 1);

    writer.beginObjectMember("config");
    writer.member("meshCount", options.meshCount);
    writer.member("lightCount", options.lightCount);
    writer.member("targetFrames", options.targetFrameCount);
    writer.endObject();

    writer.beginObjectMember("counters");
    writer.member("frameUpdates", counters.frameUpdates);
    writer.member("renderExtractions", counters.renderExtractions);
    writer.member("totalMeshesRendered", counters.totalMeshesRendered);
    writer.member("stateEnters", counters.stateEnters);
    writer.member("stateExits", counters.stateExits);
    writer.endObject();

    writer.beginObjectMember("performance");
    writer.member("minFrameTimeMs", counters.minFrameTimeMs);
    writer.member("maxFrameTimeMs", counters.maxFrameTimeMs);
    writer.member("avgFrameTimeMs", counters.totalFrameTimeMs / counters.frameUpdates);
    writer.member("avgFPS", 1000.0 / (counters.totalFrameTimeMs / counters.frameUpdates));
    writer.endObject();

    writer.endObject();
    std::cout << '\n';

    return 0;
}
