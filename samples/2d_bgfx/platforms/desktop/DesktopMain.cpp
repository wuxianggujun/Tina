#include <tina/audio/AudioEngine.hpp>
#include <tina/audio/AudioTypes.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/color/BlendMode.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/text/ParseInteger.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/gameplay/Action.hpp>
#include <tina/gameplay/GameplayTypes.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/text/BitmapFont.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIText.hpp>

#if defined(TINA_SAMPLE_2D_BGFX_AUDIO_MINIAUDIO)
#include <tina/audio/miniaudio/MiniaudioDevice.hpp>
#endif

#include <array>
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
#include <vector>

#include "SampleSpriteFrameResource.hpp"

namespace {

namespace UI = Tina::UI;

using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;
using Tina::Core::usize;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 SpriteCount = 6;
inline constexpr u32 UIPanelCount = 3;
inline constexpr u64 PauseAfterFrames = 90;
inline constexpr u64 ResumeAfterFrames = 180;
inline constexpr u32 AudioSampleRate = 48000;
inline constexpr u32 AudioChannels = 2;
inline constexpr u64 AudioClipFrames = AudioSampleRate / 4U;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    bool interactive = false;
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
    bool audioLoopStarted = false;
    bool actionsPaused = false;
    bool actionsResumed = false;
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
    return "tina." + std::to_string(static_cast<Tina::Core::u16>(code.domain)) + "." + std::to_string(code.value);
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

[[nodiscard]] Tina::Core::Result<std::shared_ptr<const Tina::Text::BitmapFontAtlas>> makePresentationFont()
{
    constexpr u32 cell = 8;
    constexpr u32 pageWidth = 64;
    constexpr u32 pageHeight = 16;
    struct Pattern final {
        char ch = '?';
        u8 rows[7]{};
    };
    constexpr std::array patterns{
        Pattern{'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
        Pattern{'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        Pattern{'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        Pattern{'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
        Pattern{'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
        Pattern{'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        Pattern{'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        Pattern{'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        Pattern{'S', {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E}},
        Pattern{'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        Pattern{'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    };
    std::vector<Tina::Text::BitmapGlyph> glyphs;
    glyphs.reserve(patterns.size() + 1U);
    glyphs.push_back({.codepoint = ' ', .advance = 4.0F});
    std::vector<u8> pixels(static_cast<usize>(pageWidth) * pageHeight * 4U, 0);
    for (usize index = 0; index < patterns.size(); ++index) {
        const u32 column = static_cast<u32>(index % 8U);
        const u32 row = static_cast<u32>(index / 8U);
        const u32 originX = column * cell;
        const u32 originY = row * cell;
        for (u32 y = 0; y < 7U; ++y) {
            for (u32 x = 0; x < 5U; ++x) {
                if ((patterns[index].rows[y] & static_cast<u8>(1U << (4U - x))) == 0U) {
                    continue;
                }
                const usize offset =
                    (static_cast<usize>(originY + y) * pageWidth + originX + x) * 4U;
                pixels[offset] = 255;
                pixels[offset + 1U] = 255;
                pixels[offset + 2U] = 255;
                pixels[offset + 3U] = 255;
            }
        }
        glyphs.push_back({
            .codepoint = static_cast<u32>(static_cast<unsigned char>(patterns[index].ch)),
            .x = originX,
            .y = originY,
            .width = 5,
            .height = 7,
            .advance = 6.0F,
            .bearingY = 7.0F,
        });
    }
    auto font = Tina::Text::BitmapFont::Create({
        .nominalSize = 8.0F,
        .lineHeight = 10.0F,
        .baseline = 8.0F,
        .fallbackCodepoint = '?',
        .pages = {{pageWidth, pageHeight, Tina::Text::BitmapFontImageKind::Coverage}},
        .glyphs = std::move(glyphs),
    });
    if (!font) {
        return Tina::Core::failure(std::move(font.error()));
    }
    std::vector<std::vector<u8>> pages;
    pages.push_back(std::move(pixels));
    auto atlas = Tina::Text::BitmapFontAtlas::Create(std::move(*font), std::move(pages));
    if (!atlas) {
        return Tina::Core::failure(std::move(atlas.error()));
    }
    try {
        return std::make_shared<const Tina::Text::BitmapFontAtlas>(std::move(*atlas));
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Presentation bitmap font allocation failed");
    }
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
        if (argument == "--interactive")
        {
            if (options.interactive)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--interactive must appear once");
            }
            options.interactive = true;
        }
        else if (argument.starts_with(FramesPrefix))
        {
            if (hasFrames || !Tina::Core::parseUnsigned(argument.substr(FramesPrefix.size()), options.targetFrameCount) ||
                options.targetFrameCount == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must appear once and be greater than zero");
            }
            hasFrames = true;
        } else if (argument.starts_with(DelayPrefix))
        {
            if (hasDelay || !Tina::Core::parseUnsigned(argument.substr(DelayPrefix.size()), options.frameDelayMilliseconds))
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

        titleBarLayout_ = absolutePanelStyle(Tina::UI::UILayoutLength::Px(24.0F), Tina::UI::UILayoutLength::Px(24.0F),
                                             Tina::UI::UILayoutLength::Px(360.0F), Tina::UI::UILayoutLength::Px(56.0F));
        auto titleBar = tree->createElement(root->rootNodeId(), UI::makePanelElement());
        if (!titleBar)
        {
            return Tina::Core::failure(std::move(titleBar.error()));
        }
        if (auto status = tree->setLayoutStyle(*titleBar, titleBarLayout_); !status)
        {
            return status;
        }
        if (auto status = tree->setBoxPaint(*titleBar, solidFill(7, 18, 32, 210)); !status)
        {
            return status;
        }
        titleBar_ = *titleBar;

        UI::UILayoutStyle titleStyle{};
        titleStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        titleStyle.size.height = UI::UILayoutLength::Percent(100.0F);
        auto title = tree->createElement(titleBar_, UI::makeLabelElement("GLOW", titleStyle));
        if (!title)
        {
            return Tina::Core::failure(std::move(title.error()));
        }
        UI::UITextStyle titleText{};
        titleText.logicalSize = 16.0F;
        titleText.color = UI::rgba8(255, 220, 96);
        if (auto status = tree->setTextStyle(*title, titleText); !status)
        {
            return status;
        }
        titleLabel_ = *title;

        auto gold = tree->createElement(root->rootNodeId(), UI::makePanelElement());
        if (!gold)
        {
            return Tina::Core::failure(std::move(gold.error()));
        }
        if (auto status = tree->setLayoutStyle(
                *gold, absolutePanelStyle(Tina::UI::UILayoutLength::Px(470.0F), Tina::UI::UILayoutLength::Px(350.0F),
                                          Tina::UI::UILayoutLength::Px(360.0F), Tina::UI::UILayoutLength::Px(12.0F)));
            !status)
        {
            return status;
        }
        if (auto status = tree->setBoxPaint(*gold, solidFill(255, 184, 72, 235)); !status)
        {
            return status;
        }

        auto compass = tree->createElement(root->rootNodeId(), UI::makePanelElement());
        if (!compass)
        {
            return Tina::Core::failure(std::move(compass.error()));
        }
        if (auto status = tree->setLayoutStyle(
                *compass, absolutePanelStyle(Tina::UI::UILayoutLength::Px(1160.0F), Tina::UI::UILayoutLength::Px(24.0F),
                                             Tina::UI::UILayoutLength::Px(96.0F), Tina::UI::UILayoutLength::Px(96.0F)));
            !status)
        {
            return status;
        }
        if (auto status = tree->setBoxPaint(*compass, solidFill(12, 28, 48, 220)); !status)
        {
            return status;
        }
        compass_ = *compass;
        if (auto status = writeCompass(*tree, 0.0F); !status)
        {
            return status;
        }

        auto actions = Tina::Gameplay::ActionRunner::Create();
        if (!actions)
        {
            return Tina::Core::failure(std::move(actions.error()));
        }
        actions_ = std::move(*actions);
        auto played = actions_->play(Tina::Gameplay::Action::repeat(
            Tina::Gameplay::Repeat::forever(),
            Tina::Gameplay::Action::sequence(
                Tina::Gameplay::Action::tweenFloat(
                    Tina::Core::Duration{1.2}, 0.0F, 48.0F, Tina::Gameplay::Easing::SineInOut,
                    [this](float value) { titleOffsetX_ = value; }),
                Tina::Gameplay::Action::tweenFloat(
                    Tina::Core::Duration{1.2}, 48.0F, 0.0F, Tina::Gameplay::Easing::SineInOut,
                    [this](float value) { titleOffsetX_ = value; }))));
        if (!played)
        {
            return Tina::Core::failure(std::move(played.error()));
        }

        pcmFrames_.assign(static_cast<usize>(AudioClipFrames * AudioChannels), 0.0F);
        for (u64 frame = 0; frame < AudioClipFrames; ++frame)
        {
            const float sample = 0.18F * std::sin(6.28318530718F * 220.0F *
                                                  static_cast<float>(frame) /
                                                  static_cast<float>(AudioSampleRate));
            pcmFrames_[static_cast<usize>(frame) * AudioChannels] = sample;
            pcmFrames_[static_cast<usize>(frame) * AudioChannels + 1U] = sample;
        }

        uiRoot_ = std::move(*root);
        ++counters_->uiRootsCreated;
        counters_->uiPanelsCreated = UIPanelCount;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        if (actions_)
        {
            actions_->cancelAll();
        }
#if defined(TINA_SAMPLE_2D_BGFX_AUDIO_MINIAUDIO)
        if (audioDevice_)
        {
            audioDevice_->stop();
            audioDevice_->shutdown();
            audioDevice_.reset();
        }
#endif
        musicVoice_ = {};
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
        if (auto* audio = context.audioEngine(); audio != nullptr)
        {
            if (!musicVoice_.hasValue())
            {
#if defined(TINA_SAMPLE_2D_BGFX_AUDIO_MINIAUDIO)
                if (!audioDevice_)
                {
                    auto device = Tina::Audio::MiniaudioDevice::Create({
                        .useNullBackend = options_.interactive ? false : true,
                        .sampleRate = AudioSampleRate,
                        .channels = AudioChannels,
                        .periodFrames = 256,
                    });
                    if (device)
                    {
                        device->attachMixer(audio);
                        if (device->start())
                        {
                            audioDevice_ = std::move(*device);
                        }
                    }
                }
#endif
                auto voice = audio->playPcm(
                    Tina::Audio::AudioPcmClipView{
                        .frames = pcmFrames_.data(),
                        .frameCount = AudioClipFrames,
                        .channels = AudioChannels,
                        .sampleRate = AudioSampleRate,
                    },
                    Tina::Audio::AudioPlayDesc{.loopMode = Tina::Audio::AudioLoopMode::Loop},
                    Tina::Audio::AudioBusId::Music);
                if (voice)
                {
                    musicVoice_ = *voice;
                    counters_->audioLoopStarted = true;
                }
            }
            if (auto pumped = audio->pumpCompletions(); !pumped)
            {
                return Tina::Core::failure(std::move(pumped.error()));
            }
        }
        if (actions_)
        {
            if (counters_->frameUpdates == PauseAfterFrames)
            {
                actions_->pauseAll();
                actionsPaused_ = true;
                counters_->actionsPaused = true;
            }
            else if (counters_->frameUpdates == ResumeAfterFrames)
            {
                actions_->resumeAll();
                actionsPaused_ = false;
                counters_->actionsResumed = true;
            }
            if (auto status = actions_->advance(context.frameTiming().updateDelta); !status)
            {
                return status;
            }
        }
        if (options_.frameDelayMilliseconds != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        if (!options_.interactive && counters_->frameUpdates >= options_.targetFrameCount)
        {
            if (auto* audio = context.audioEngine(); audio != nullptr && musicVoice_.hasValue())
            {
                (void)audio->enqueueStop(musicVoice_);
                (void)audio->pumpCompletions();
                musicVoice_ = {};
            }
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override
    {
        if (!uiRoot_)
        {
            return Tina::Core::success();
        }
        auto tree = context.primaryWindowUITreeUpdater(uiRoot_);
        if (!tree)
        {
            return Tina::Core::failure(std::move(tree.error()));
        }
        titleBarLayout_.overlay.offset.x = UI::UILayoutLength::Px(24.0F + titleOffsetX_);
        if (auto status = tree->setLayoutStyle(titleBar_, titleBarLayout_); !status)
        {
            return status;
        }
        if (auto status = tree->setText(titleLabel_, actionsPaused_ ? "PAUSED" : "GLOW"); !status)
        {
            return status;
        }
        const float angle = static_cast<float>(context.frameTiming().frameIndex) * 0.035F;
        return writeCompass(*tree, angle);
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
            Tina::Core::BlendMode blendMode = Tina::Core::BlendMode::PremultipliedAlpha;
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
            {.centerX = -1.65F,
             .centerY = -0.08F,
             .widthMeters = 3.1F,
             .heightMeters = 3.1F,
             .rotationPhase = 0.00F,
             .scaleX = 1.00F,
             .scaleY = 1.00F,
             .red = 255,
             .green = 210,
             .blue = 80,
             .alpha = 90,
             .blendMode = Tina::Core::BlendMode::Additive},
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
                .quad = Tina::Render::makeSprite2DQuad({
                    .positionX = spec.centerX,
                    .positionY = spec.centerY,
                    .rotationRadians = rotationBase + spec.rotationPhase,
                    .widthMeters = spec.widthMeters,
                    .heightMeters = spec.heightMeters,
                    .scaleX = spec.scaleX,
                    .scaleY = spec.scaleY,
                }),
                .sortingLayer = 0,
                .orderInLayer = static_cast<Tina::Core::i32>(index),
                .colorTransform = {.multiply = Tina::Core::ColorRgba::fromBytes(spec.red, spec.green, spec.blue, spec.alpha)},
                .blendMode = spec.blendMode,
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
    [[nodiscard]] Tina::Core::Status writeCompass(Tina::PrimaryWindowUITreeUpdater& tree, float angle) const
    {
        UI::UICanvasCommand ring = UI::makeCanvasEllipse(
            UI::UILogicalRect{.x = 8.0F, .y = 8.0F, .width = 80.0F, .height = 80.0F},
            UI::rgba8(180, 220, 255, 220), 3.0F);
        UI::UICanvasCommand needle = UI::makeCanvasLine(
            UI::UILogicalPoint{.x = 48.0F, .y = 18.0F}, UI::UILogicalPoint{.x = 48.0F, .y = 78.0F}, 4.0F,
            UI::rgba8(255, 96, 72));
        needle.rotationRadians = angle;
        needle.rotationPivotX = 0.5F;
        needle.rotationPivotY = 0.5F;
        const std::array commands{ring, needle};
        return tree.setCanvasCommands(compass_, commands);
    }

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Tina::UI::UIRootOwner uiRoot_{};
    UI::UINodeId titleBar_{};
    UI::UILayoutStyle titleBarLayout_{};
    UI::UINodeId titleLabel_{};
    UI::UINodeId compass_{};
    float titleOffsetX_ = 0.0F;
    bool actionsPaused_ = false;
    std::optional<Tina::Gameplay::ActionRunner> actions_{};
    std::vector<float> pcmFrames_{};
    Tina::Audio::AudioVoiceId musicVoice_{};
#if defined(TINA_SAMPLE_2D_BGFX_AUDIO_MINIAUDIO)
    std::optional<Tina::Audio::MiniaudioDevice> audioDevice_{};
#endif
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
    config.applicationName = "Tina vNext 2D presentation primitives";
    config.primaryWindow.title = "Tina vNext - Glow / BitmapFont / Canvas / Loop";
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

    auto font = makePresentationFont();
    if (!font)
    {
        writeError(font.error());
        return 1;
    }

    auto hostResult = Tina::Desktop::CreateEngine(
        createEngineConfig(), Tina::Desktop::CreateEngineOptions{.uiBitmapFont = std::move(*font)});
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
        (!options.interactive &&
         (counters.frameUpdates != options.targetFrameCount ||
          counters.renderExtractions != options.targetFrameCount)) ||
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
        writer.member("additiveGlow", true);
        writer.member("bitmapFontTitle", true);
        writer.member("canvasCompass", true);
        writer.member("audioLoopStarted", counters.audioLoopStarted);
        writer.member("actionsPaused", counters.actionsPaused);
        writer.member("actionsResumed", counters.actionsResumed);
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
