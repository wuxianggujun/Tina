#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "../common/SampleSpriteFrameResource.hpp"

namespace {

namespace UI = Tina::UI;

using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 SpriteCount = 5;
inline constexpr u32 UIPanelCount = 2;

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

[[nodiscard]] Tina::UI::UILayoutStyle absolutePanelStyle(Tina::UI::UILayoutLength left, Tina::UI::UILayoutLength top,
                                                         Tina::UI::UILayoutLength width,
                                                         Tina::UI::UILayoutLength height) noexcept
{
    Tina::UI::UILayoutStyle style{};
    style.placement = Tina::UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = left;
    style.overlay.offset.y = top;
    style.size.width = width;
    style.size.height = height;
    return style;
}

[[nodiscard]] Tina::UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha) noexcept
{
    return Tina::UI::UIBoxPaint{
        .solidFill =
            Tina::UI::UISolidFill{
                .color =
                    {
                        .red = red,
                        .green = green,
                        .blue = blue,
                        .alpha = alpha,
                    },
            },
    };
}

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "." + std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_2d_infrastructure_bgfx");
    writer.member("code", errorCodeName(error.code));
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
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
        } else if (argument.starts_with(DelayPrefix))
        {
            if (hasDelay || !parseUnsigned(argument.substr(DelayPrefix.size()), options.frameDelayMilliseconds))
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must appear once and be unsigned");
            }
            hasDelay = true;
        } else
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Unsupported command-line argument"};
            error.addContext("parseOptions", argument);
            return Tina::Core::failure(std::move(error));
        }
    }
    return options;
}

class Visible2DState final : public Tina::IGameState {
  public:
    Visible2DState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;

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
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(24.0F), Tina::UI::UILayoutLength::Px(24.0F),
                                             Tina::UI::UILayoutLength::Px(360.0F), Tina::UI::UILayoutLength::Px(56.0F)),
                .paint = solidFill(7, 18, 32, 210),
            },
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(470.0F), Tina::UI::UILayoutLength::Px(350.0F),
                                             Tina::UI::UILayoutLength::Px(360.0F), Tina::UI::UILayoutLength::Px(12.0F)),
                .paint = solidFill(255, 184, 72, 235),
            },
        };
        for (const PanelSpec& panelSpec : panels)
        {
            auto panel = tree->createElement(root->rootNodeId(), UI::makePanelElement());
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
        const Tina::Render::RenderCamera2DInput camera{
            .stableCameraKey = 1,
            .centerX = 0.0F,
            .centerY = 0.0F,
            .rotationRadians = 0.0F,
            .worldWidth = 16.0F,
            .worldHeight = 9.0F,
            .actualPixelsPerMeter = 64.0F,
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled,
        };
        if (auto status = writer.setCamera2D(camera); !status)
        {
            return status;
        }

        struct SpriteSpec final {
            float centerX = 0.0F;
            float centerY = 0.0F;
            float widthMeters = 1.0F;
            float heightMeters = 1.0F;
            float rotationPhase = 0.0F;
            float scaleX = 1.0F;
            float scaleY = 1.0F;
            u8 red = 255;
            u8 green = 255;
            u8 blue = 255;
            u8 alpha = 255;
            bool flipX = false;
            bool flipY = false;
        };

        constexpr std::array<SpriteSpec, SpriteCount> Sprites{{
            {.centerX = -1.65F,
             .centerY = -0.08F,
             .widthMeters = 2.3F,
             .heightMeters = 2.3F,
             .rotationPhase = 0.00F,
             .scaleX = 1.00F,
             .scaleY = 1.00F,
             .red = 255,
             .green = 85,
             .blue = 96,
             .alpha = 218},
            {.centerX = -0.60F,
             .centerY = 0.20F,
             .widthMeters = 2.0F,
             .heightMeters = 2.0F,
             .rotationPhase = 0.50F,
             .scaleX = 0.85F,
             .scaleY = 1.15F,
             .red = 255,
             .green = 188,
             .blue = 72,
             .alpha = 190,
             .flipX = true},
            {.centerX = 0.08F,
             .centerY = -0.04F,
             .widthMeters = 2.4F,
             .heightMeters = 2.4F,
             .rotationPhase = 0.95F,
             .scaleX = 1.05F,
             .scaleY = 1.05F,
             .red = 83,
             .green = 215,
             .blue = 255,
             .alpha = 174},
            {.centerX = 0.78F,
             .centerY = 0.16F,
             .widthMeters = 1.8F,
             .heightMeters = 2.2F,
             .rotationPhase = 1.35F,
             .scaleX = 1.20F,
             .scaleY = 0.90F,
             .red = 84,
             .green = 237,
             .blue = 154,
             .alpha = 204,
             .flipY = true},
            {.centerX = 1.70F,
             .centerY = -0.14F,
             .widthMeters = 2.1F,
             .heightMeters = 2.1F,
             .rotationPhase = 1.80F,
             .scaleX = 0.95F,
             .scaleY = 0.95F,
             .red = 177,
             .green = 116,
             .blue = 255,
             .alpha = 220,
             .flipX = true,
             .flipY = true},
        }};

        const float rotationBase = static_cast<float>(context.frameTiming().frameIndex) * 0.02F;
        auto texture = spriteFrameResource_.intern(context.frameResourceSink(), 1);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        for (u32 index = 0; index < SpriteCount; ++index)
        {
            const SpriteSpec& spec = Sprites[index];
            const Tina::Render::RenderSprite2DInput sprite{
                .texture = *texture,
                .stableEntityKey = static_cast<u64>(index) + 1U,
                .centerX = spec.centerX,
                .centerY = spec.centerY,
                .rotationRadians = rotationBase + spec.rotationPhase,
                .widthMeters = spec.widthMeters,
                .heightMeters = spec.heightMeters,
                .scaleX = spec.scaleX,
                .scaleY = spec.scaleY,
                .sortingLayer = 0,
                .orderInLayer = static_cast<Tina::Core::i32>(index),
                .red = spec.red,
                .green = spec.green,
                .blue = spec.blue,
                .alpha = spec.alpha,
                .flipX = spec.flipX,
                .flipY = spec.flipY,
                .visible = true,
            };
            if (auto status = writer.addSprite2D(sprite); !status)
            {
                return status;
            }
        }
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Tina::UI::UIRootOwner uiRoot_{};
    mutable Tina::Samples::SampleSpriteFrameResource spriteFrameResource_{};
};

class Visible2DApplication final : public Tina::IGameApplication {
  public:
    Visible2DApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{std::make_unique<Visible2DState>(options_, *counters_)};
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
    config.applicationName = "Tina vNext 2D Infrastructure bgfx";
    config.primaryWindow.title = "Tina vNext - Sprite2D / UI";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 16;
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
    Visible2DApplication application{options, counters};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount || counters.renderExtractions != options.targetFrameCount ||
        counters.stateEnters != 1 || counters.stateExits != 1 || counters.applicationShutdowns != 1 ||
        counters.uiRootsCreated != 1 || counters.uiPanelsCreated != UIPanelCount || counters.uiRootsReleased != 1)
    {
        {
            Tina::Core::JsonWriter writer(std::cerr);
            writer.beginObject();
            writer.member("status", "error");
            writer.member("sample", "tina_sample_2d_infrastructure_bgfx");
            writer.member("message", "lifecycle counters did not match");
            writer.endObject();
        }
        std::cerr << '\n';
        return 1;
    }

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", "tina_sample_2d_infrastructure_bgfx");
        writer.member("frames", counters.frameUpdates);
        writer.member("spritesPerFrame", SpriteCount);
        writer.member("uiPanels", counters.uiPanelsCreated);
        writer.member("uiRootsReleased", counters.uiRootsReleased);
        writer.member("applicationShutdowns", counters.applicationShutdowns);
        writer.member("engineHostDestroyed", true);
        writer.member("renderResourceLedgerBalanced", true);
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The 2D bgfx infrastructure sample ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the 2D bgfx infrastructure sample boundary"};
        error.addContext("main", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the 2D bgfx infrastructure sample boundary"};
        writeError(error);
        return 1;
    }
}
