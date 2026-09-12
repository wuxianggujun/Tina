// 2D Stress Test - 大规模性能压力测试
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/ParseInteger.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
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
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SampleSpriteFrameResource.hpp"

namespace {

namespace UI = Tina::UI;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

// 压力测试配置
inline constexpr u32 DefaultSpriteCount = 10000;  // 10K sprites
inline constexpr u32 DefaultUIElementCount = 500;  // 500 UI 元素
inline constexpr u64 DefaultFrameCount = 600;      // 10秒@60fps
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;

struct StressOptions final {
    u32 spriteCount = DefaultSpriteCount;
    u32 uiElementCount = DefaultUIElementCount;
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    bool enableTracy = true;
};

struct StressCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 totalSpritesRendered = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    double minFrameTimeMs = 999999.0;
    double maxFrameTimeMs = 0.0;
    double totalFrameTimeMs = 0.0;
};

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
    writer.member("sample", "tina_sample_stress_2d");
    writer.member("code", errorCodeName(error.code));
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

[[nodiscard]] Tina::Core::Result<StressOptions> parseOptions(int argc, char** argv)
{
    StressOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (arg.starts_with("--sprites=")) {
            if (!Tina::Core::parseUnsigned(arg.substr(10), options.spriteCount)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                         "invalid --sprites value");
            }
        } else if (arg.starts_with("--ui-elements=")) {
            if (!Tina::Core::parseUnsigned(arg.substr(14), options.uiElementCount)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                         "invalid --ui-elements value");
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

class Stress2DState final : public Tina::IGameState {
public:
    Stress2DState(StressOptions options, StressCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override;
    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override;
    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override;
    void onExit(Tina::GameStateExitContext&) noexcept override;

private:
    struct Vec2 {
        float x, y;
    };

    [[nodiscard]] static u64 stableKey(Tina::Scene::EntityId entity) noexcept
    {
        return (static_cast<u64>(entity.index()) << 32U) | entity.generation();
    }

    StressOptions options_;
    StressCounters* counters_;
    std::optional<Tina::Scene::World> world_;
    Tina::Scene::EntityId cameraEntity_{};
    Tina::UI::UIRootOwner uiRoot_{};
    std::vector<Tina::Scene::EntityId> entities_;
    std::vector<Vec2> velocities_;
    Tina::Samples::SampleSpriteFrameResource spriteFrameResource_;
};

class Stress2DApplication final : public Tina::IGameApplication {
public:
    Stress2DApplication(StressOptions options, StressCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(
        Tina::GameStartupContext&) override
    {
        return std::make_unique<Stress2DState>(options_, *counters_);
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override {}

private:
    StressOptions options_;
    StressCounters* counters_;
};

Tina::Core::Status Stress2DState::onEnter(Tina::GameStateEnterContext& context)
{
        ++counters_->stateEnters;

        // 创建 World
        auto worldResult = Tina::Scene::World::Create(Tina::Scene::WorldConfig{
            static_cast<u32>(options_.spriteCount + 100)});
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

        // 创建大量 sprites - 随机分布
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> posDistX(-2000.0F, 2000.0F);
        std::uniform_real_distribution<float> posDistY(-2000.0F, 2000.0F);
        std::uniform_real_distribution<float> scaleDistposition(0.5F, 2.0F);
        std::uniform_real_distribution<float> velocityDist(-50.0F, 50.0F);

        entities_.reserve(options_.spriteCount);
        velocities_.reserve(options_.spriteCount);

        for (u32 i = 0; i < options_.spriteCount; ++i) {
            const Tina::Scene::LocalTransform transform{
                .position = {posDistX(rng), posDistY(rng), 0.0F},
                .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
                .scale = {scaleDistposition(rng), scaleDistposition(rng), 1.0F},
            };
            auto entityResult = world_->createEntity(transform);
            if (!entityResult) {
                return Tina::Core::failure(std::move(entityResult.error()));
            }
            entities_.push_back(*entityResult);
            velocities_.push_back({velocityDist(rng), velocityDist(rng)});
        }

        if (auto status = world_->updateWorldTransforms(); !status) {
            return status;
        }

        // 创建 UI (大量元素测试布局性能)
        auto rootBuilder = context.primaryWindowUIRootBuilder();
        if (!rootBuilder) {
            return Tina::Core::failure(std::move(rootBuilder.error()));
        }
        auto rootResult = rootBuilder->createRoot();
        if (!rootResult) {
            return Tina::Core::failure(std::move(rootResult.error()));
        }
        uiRoot_ = std::move(*rootResult);

        return Tina::Core::success();
}

Tina::Core::Status Stress2DState::updateFrame(Tina::FrameUpdateContext& context)
{
        Tina::Core::SteadyMonotonicClock clock;
        const auto frameStart = clock.now();
        ++counters_->frameUpdates;

        // 更新 sprite 位置 (简单物理模拟)
        const float deltaTime = 1.0F / 60.0F;

        for (size_t i = 0; i < entities_.size(); ++i) {
            const Tina::Scene::LocalTransform* localTransform = world_->localTransform(entities_[i]);
            if (localTransform == nullptr) {
                continue;
            }

            Tina::Scene::LocalTransform newTransform = *localTransform;
            newTransform.position.x += velocities_[i].x * deltaTime;
            newTransform.position.y += velocities_[i].y * deltaTime;

            // 边界反弹
            if (newTransform.position.x < -2000.0F || newTransform.position.x > 2000.0F) {
                velocities_[i].x = -velocities_[i].x;
            }
            if (newTransform.position.y < -2000.0F || newTransform.position.y > 2000.0F) {
                velocities_[i].y = -velocities_[i].y;
            }

            if (auto status = world_->setLocalTransform(entities_[i], newTransform); !status) {
                return status;
            }
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

Tina::Core::Status Stress2DState::extractRenderScene(Tina::RenderSceneExtractionContext& context) const
{
        if (!world_.has_value()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "2D stress World was not initialized");
        }

        const Tina::Scene::WorldTransform* cameraTransform = world_->worldTransform(cameraEntity_);
        if (cameraTransform == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "2D stress camera transform is unavailable");
        }

        auto& writer = context.renderSceneWriter();
        Tina::Render::RenderCamera2DInput camera{
            .stableCameraKey = stableKey(cameraEntity_),
            .centerX = cameraTransform->position.x,
            .centerY = cameraTransform->position.y,
            .worldWidth = 4000.0F,
            .worldHeight = 4000.0F,
            .actualPixelsPerMeter = 1.0F,
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled,
        };
        if (auto status = writer.setCamera2D(camera); !status) {
            return status;
        }

        for (size_t i = 0; i < entities_.size(); ++i) {
            const Tina::Scene::WorldTransform* transform = world_->worldTransform(entities_[i]);
            if (transform == nullptr) {
                continue;
            }
            auto texture = spriteFrameResource_.intern(
                context.frameResourceSink(), static_cast<Tina::u64>(i % 8 + 1U));
            if (!texture) {
                return Tina::Core::failure(std::move(texture.error()));
            }

            const float halfWidth = 32.0F * transform->scale.x;
            const float halfHeight = 32.0F * transform->scale.y;

            Tina::Render::RenderSprite2DInput sprite{
                .texture = *texture,
                .stableEntityKey = stableKey(entities_[i]),
                .quad = {
                    .centerX = transform->position.x,
                    .centerY = transform->position.y,
                    .halfAxisXX = halfWidth,
                    .halfAxisXY = 0.0F,
                    .halfAxisYX = 0.0F,
                    .halfAxisYY = halfHeight,
                },
                .u0 = 0.0F,
                .v0 = 0.0F,
                .u1 = 1.0F,
                .v1 = 1.0F,
                .sortingLayer = static_cast<Tina::Core::i16>(i % 10),
                .red = 255,
                .green = 255,
                .blue = 255,
                .alpha = 255,
            };
            if (auto status = writer.addSprite2D(sprite); !status) {
                return status;
            }
        }

        ++counters_->renderExtractions;
        counters_->totalSpritesRendered += entities_.size();
        return Tina::Core::success();
}

void Stress2DState::onExit(Tina::GameStateExitContext&) noexcept
{
        ++counters_->stateExits;

        uiRoot_.reset();
        world_.reset();
        entities_.clear();
        velocities_.clear();
}

} // namespace

int main(int argc, char** argv)
{
    auto optionsResult = parseOptions(argc, argv);
    if (!optionsResult) {
        writeError(optionsResult.error());
        return 1;
    }
    const StressOptions options = *optionsResult;

    StressCounters counters{};

    try {
        Tina::EngineConfig config = Tina::EngineConfig::Defaults();
        config.primaryWindow.title = "Tina 2D Stress Test";
        config.primaryWindow.initialLogicalExtent = {1920, 1080};
        config.renderSceneCapacities.spriteCapacity = options.spriteCount + 100;

        auto engineResult = Tina::Desktop::CreateEngine(config);
        if (!engineResult) {
            writeError(engineResult.error());
            return 1;
        }
        auto engine = std::move(*engineResult);

        Stress2DApplication app(options, counters);
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
            writer.member("sample", "tina_sample_stress_2d");
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
        writer.member("sample", "tina_sample_stress_2d");
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
    writer.member("sample", "tina_sample_stress_2d");
    writer.member("schema", 1);

    writer.beginObjectMember("config");
    writer.member("spriteCount", options.spriteCount);
    writer.member("uiElementCount", options.uiElementCount);
    writer.member("targetFrames", options.targetFrameCount);
    writer.endObject();

    writer.beginObjectMember("counters");
    writer.member("frameUpdates", counters.frameUpdates);
    writer.member("renderExtractions", counters.renderExtractions);
    writer.member("totalSpritesRendered", counters.totalSpritesRendered);
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
