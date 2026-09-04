// Gate program for the HTML5 platform backend, in the same shape as the other
// samples: a fixed frame budget, printed evidence, an exit code. The difference is
// that the browser owns the frame loop, so this drives EngineHost::start/tick from
// emscripten_set_main_loop instead of calling the blocking run().
//
// It doubles as the interactive check the backend cannot get any other way: the
// counters come from real DOM events, so a nonzero key or pointer count is evidence
// that the browser event path works end to end.

#include <tina/core/error/Error.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/Window.hpp>
#include <tina/platform/html5/Html5PlatformFactory.hpp>
#include "render/bgfx/BgfxRenderDevice.hpp"
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include "SampleSpriteFrameResource.hpp"

#include <emscripten/em_asm.h>
#include <emscripten/emscripten.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <utility>
#include <variant>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

inline constexpr Tina::InputActionId ForwardAction{1};
inline constexpr Tina::InputActionId ExitAction{2};
inline constexpr Tina::InputActionId TapAction{3};
inline constexpr Tina::InputActionId SecondaryClickAction{4};
inline constexpr Tina::InputActionId MiddleClickAction{5};
inline constexpr Tina::InputActionId LockPointerAction{6};

// The browser hands us no argv, so the frame budget is fixed here. It only bounds the
// unattended run; the Escape binding still ends the loop early.
inline constexpr u64 TargetFrameCount = 600;

inline constexpr u32 SpriteBindingKey = 1;

// A 2x2 RGBA8 texture, uploaded from here rather than loaded from a cooked catalog: this
// sample has to prove the WebGL2 shader path, and packaging asset files into the wasm
// bundle is a separate unverified mechanism that would sit between the claim and the
// evidence.
//
// Every channel is 0 or 255 on purpose. sRGB decode is a sampler-side flag, and the
// backbuffer is not sRGB (kDefaultResetFlags carries no BGFX_RESET_SRGB_BACKBUFFER), so an
// intermediate byte would leave the shader's output shifted by the decode with nothing to
// re-encode it -- making the expected screen value depend on whether the ES driver honoured
// the flag. 0 and 255 are the two fixed points of that transfer function, so the expected
// pixel is the texel byte either way and the gate can assert equality rather than a band.
//
// Row 0 is the top of the sprite on screen: writeSprite gives the +Y vertices v0 (the
// `topV` binding), mtxOrtho maps world +Y to NDC +Y, and GL samples v=0 from the first
// uploaded row.
inline constexpr std::array<u8, 16> SpriteTexels{
    // row 0: red, green
    255, 0,   0,   255, /**/ 0,   255, 0,   255,
    // row 1: blue, white
    0,   0,   255, 255, /**/ 255, 255, 255, 255,
};

// Mirrored by the shell page. Integers only, so the JS bridge never has to marshal a
// string out of wasm memory.
enum class SessionState : int {
    Running = 0,
    Finished = 1,
    CreateFailed = 2,
    StartFailed = 3,
    TickFailed = 4,
    LifecycleMismatch = 5,
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 forwardStarts = 0;
    // Primary-pointer button presses. A touch's first finger owns the primary slot, so this
    // counts taps as well as mouse clicks.
    u64 tapStarts = 0;
    // DOM button numbering is not the engine's: DOM 1 is the middle button and DOM 2 is the
    // right one, while the engine enum orders them Primary, Secondary, Middle. Counting the
    // two separately is what catches a swap -- binding only one would pass either way.
    u64 secondaryClicks = 0;
    u64 middleClicks = 0;
    // Frames in which the primary pointer reported a nonzero look delta. Motion has no
    // resting value, so it is deliberately not an Action; this is the only game-visible
    // signal that mouse movement arrived at all.
    u64 pointerMoveFrames = 0;
    // Summed |delta| in window-logical units, rounded at report time. Distinguishes "the
    // move path delivered something" from "it delivered the distance actually travelled".
    double pointerTravel = 0.0;
    // Summed wheel notches, signed. Signed on purpose: the backend negates the browser's Y to
    // match the desktop convention, so an unsigned total would pass with the sign inverted.
    double wheelNotches = 0.0;
    u64 wheelFrames = 0;
    // Highest number of pointers reported present in one frame, and the set of slots ever
    // seen. Together they separate "several fingers arrived" from "one finger arrived several
    // times", which a count of touch starts alone cannot do.
    u64 maxConcurrentPointers = 0;
    u64 pointerSlotsSeen = 0;
    bool escapeRequested = false;
    // Whether the sprite texture reached the GPU. Reported because a false here and a wrong
    // pixel would otherwise be the same observation from outside.
    bool textureUploaded = false;
    // Pointer lock cannot engage synchronously in a browser: the backend accepts
    // EMSCRIPTEN_RESULT_DEFERRED and waits for a user gesture. So the request succeeding and
    // the lock being active are two separate facts, and both get reported.
    bool lockRequested = false;
    bool lockRequestFailed = false;
    Tina::Platform::PointerCaptureMode captureMode = Tina::Platform::PointerCaptureMode::Free;
};

class WebSampleState final : public Tina::IGameState {
  public:
    explicit WebSampleState(LifecycleCounters& counters) noexcept : counters_(counters) {}

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        // Host-lifetime borrow: EngineHost destroys the device only in
        // EngineModules::shutdown, which runs after every onExit, so the address
        // recorded here stays valid for as long as this state can use it.
        device_ = &context.renderDevice();
        const std::array<Tina::Render::Texture2DUploadLevel, 1> levels{
            Tina::Render::Texture2DUploadLevel{
                .width = 2,
                .height = 2,
                .bytes = std::as_bytes(std::span{SpriteTexels}),
            },
        };
        // Point filtering is what makes each texel a flat screen quadrant. Linear would
        // interpolate across the texel boundaries, leaving only the quadrant centres at the
        // authored colour and every assertion dependent on exactly where it sampled.
        auto texture = device_->createTexture2D(Tina::Render::Texture2DUploadDesc{
            .format = Tina::Render::GpuTextureFormat::Rgba8Unorm,
            .colorSpace = Tina::Render::GpuTextureColorSpace::Srgb,
            .sampler =
                Tina::Render::GpuTextureSamplerDesc{
                    .wrapU = Tina::Render::GpuTextureWrapMode::Clamp,
                    .wrapV = Tina::Render::GpuTextureWrapMode::Clamp,
                    .minFilter = Tina::Render::GpuTextureFilterMode::Point,
                    .magFilter = Tina::Render::GpuTextureFilterMode::Point,
                    .mipFilter = Tina::Render::GpuTextureMipFilterMode::None,
                },
            .levels = levels,
        });
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        if (auto status = device_->setTexture2DBinding(SpriteBindingKey, *texture); !status)
        {
            (void)device_->destroyTexture2D(*texture);
            return status;
        }
        texture_ = *texture;
        counters_.textureUploaded = true;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        if (device_ != nullptr && texture_)
        {
            (void)device_->setTexture2DBinding(SpriteBindingKey, {});
            (void)device_->destroyTexture2D(texture_);
            texture_ = {};
        }
        ++counters_.stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_.frameUpdates;
        for (const Tina::FrameActionTransition& transition : context.frameActions().transitions)
        {
            const auto* action = std::get_if<Tina::InputActionTransition>(&transition);
            if (action == nullptr || action->kind != Tina::InputActionTransitionKind::Started)
            {
                continue;
            }
            if (action->action == ForwardAction)
            {
                ++counters_.forwardStarts;
            } else if (action->action == TapAction)
            {
                ++counters_.tapStarts;
            } else if (action->action == SecondaryClickAction)
            {
                ++counters_.secondaryClicks;
            } else if (action->action == MiddleClickAction)
            {
                ++counters_.middleClicks;
            } else if (action->action == LockPointerAction)
            {
                requestPointerLock(context);
            } else if (action->action == ExitAction)
            {
                counters_.escapeRequested = true;
                context.requestExitAfterFrame();
            }
        }

        const double deltaX = context.frameActions().pointerLookDeltaX;
        const double deltaY = context.frameActions().pointerLookDeltaY;
        if (deltaX != 0.0 || deltaY != 0.0)
        {
            ++counters_.pointerMoveFrames;
            counters_.pointerTravel += (deltaX < 0.0 ? -deltaX : deltaX) + (deltaY < 0.0 ? -deltaY : deltaY);
        }
        const double wheelY = context.frameActions().wheelDeltaY;
        if (wheelY != 0.0 || context.frameActions().wheelDeltaX != 0.0)
        {
            ++counters_.wheelFrames;
            counters_.wheelNotches += wheelY;
        }

        // Walking the whole table, not just the primary slot: this is the only path that can
        // show a second finger reaching game code.
        u64 presentPointers = 0;
        for (const Tina::FramePointerState& pointer : context.frameActions().pointers)
        {
            if (!pointer.present)
            {
                continue;
            }
            ++presentPointers;
            counters_.pointerSlotsSeen |= (u64{1} << pointer.pointer);
        }
        if (presentPointers > counters_.maxConcurrentPointers)
        {
            counters_.maxConcurrentPointers = presentPointers;
        }

        // Read every frame rather than only at request time: the browser can drop the lock
        // whenever the user presses Escape, and the page cannot prevent it.
        counters_.captureMode = context.pointerCaptureSettings().mode();

        if (counters_.frameUpdates >= TargetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    // One axis-aligned sprite covering the whole camera, drawn with the 2x2 texture. The
    // quadrant colours are what make this a check of the shader path rather than of the
    // clear: a sampler that ignored UV, a swapped texture upload, or a broken essl 300_es
    // program would all land on one flat colour, which the gate rejects.
    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        auto& writer = context.renderSceneWriter();
        // The viewport stays the full surface so the Sprite2D pass owns the clear; a partial
        // viewport would insert a separate full-surface clear pass and the pixels outside
        // would no longer be attributable to this camera.
        if (auto status = writer.setCamera2D(Tina::Render::RenderCamera2DInput{
                .stableCameraKey = 1,
                .worldWidth = 16.0F,
                .worldHeight = 9.0F,
                .actualPixelsPerMeter = 60.0F,
                .pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled,
            });
            !status)
        {
            return status;
        }
        auto texture = spriteFrameResource_.intern(context.frameResourceSink(), SpriteBindingKey);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        // Alpha 255 and vertex colour white: the fragment shader multiplies the texel by the
        // vertex colour and premultiplies by alpha, so anything else would fold a second
        // factor into the expected pixel for no added coverage.
        return writer.addSprite2D(Tina::Render::RenderSprite2DInput{
            .texture = *texture,
            .stableEntityKey = 1,
            .widthMeters = 16.0F,
            .heightMeters = 9.0F,
            .visible = true,
        });
    }

  private:
    void requestPointerLock(Tina::FrameUpdateContext& context) noexcept
    {
        const Tina::PointerCaptureSettings settings = context.pointerCaptureSettings();
        if (!settings.hasValue())
        {
            // Only the top GameState gets a handle. There is only one state here, so an
            // empty handle would mean the sample is wired wrong, not that the user lost a race.
            counters_.lockRequestFailed = true;
            return;
        }
        counters_.lockRequested = true;
        if (!settings.setMode(Tina::Platform::PointerCaptureMode::Locked))
        {
            counters_.lockRequestFailed = true;
        }
    }

    LifecycleCounters& counters_;
    Tina::Render::IRenderDevice* device_ = nullptr;
    Tina::Render::GpuTextureId texture_{};
    mutable Tina::Samples::SampleSpriteFrameResource spriteFrameResource_{};
};

class WebSampleApplication final : public Tina::IGameApplication {
  public:
    explicit WebSampleApplication(LifecycleCounters& counters) noexcept : counters_(counters) {}

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{std::make_unique<WebSampleState>(counters_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_.applicationShutdowns;
    }

  private:
    LifecycleCounters& counters_;
};

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.beginObjectMember("code");
    writer.member("domain", static_cast<std::uint16_t>(error.code.domain));
    writer.member("value", error.code.value);
    writer.endObject();
    writer.member("message", error.message);
    writer.beginArrayMember("context");
    for (const Tina::Core::ErrorContext& context : error.context)
    {
        writer.beginObjectElement();
        writer.member("operation", context.operation);
        writer.member("detail", context.detail);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    std::cerr << '\n';
}

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Web Sample";
    config.primaryWindow.title = "Tina Web 样例";
    // The canvas decides the real extent. This is only what the config must carry
    // before the backend reports the measured size.
    config.primaryWindow.initialLogicalExtent = {960, 540};
    config.primaryWindow.initiallyVisible = true;
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::W},
        .action = ForwardAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Escape},
        .action = ExitAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    // EngineConfig only accepts a pointer-button binding on the primary pointer, which is why
    // the HTML5 backend allocates touch slots from 0 rather than reserving 0 for the mouse: a
    // touch-only device would otherwise be unable to fire any pointer-button action.
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PointerButtonBinding{
            .pointer = Tina::Platform::PrimaryPointerId,
            .button = Tina::Platform::PointerButton::Primary,
        },
        .action = TapAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    // Only the pointer is restricted to primary, not the button, so the other two buttons are
    // bindable. They are here to pin the DOM-to-engine button mapping: the browser numbers
    // middle 1 and right 2, the engine orders them Secondary then Middle, so the translation
    // is a genuine swap that would otherwise fail silently.
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PointerButtonBinding{
            .pointer = Tina::Platform::PrimaryPointerId,
            .button = Tina::Platform::PointerButton::Secondary,
        },
        .action = SecondaryClickAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PointerButtonBinding{
            .pointer = Tina::Platform::PrimaryPointerId,
            .button = Tina::Platform::PointerButton::Middle,
        },
        .action = MiddleClickAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    // Keyed rather than automatic, because a browser grants pointer lock only from inside a
    // user gesture; the gate presses this after a click so the gesture requirement is met.
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::L},
        .action = LockPointerAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    return config;
}

[[nodiscard]] Tina::EngineCompositionFactories createEngineFactories()
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem = Tina::Task::createDisabledTaskSystem,
        .platformRender =
            Tina::WindowSurfacePlatformRenderFactories{
                // The canvas is the WindowSurface: bgfx binds to it through the lease, which
                // is what makes a drawn pixel provable rather than a blank canvas that could
                // mean either "no frame ran" or "frame ran, drew nothing".
                .createWindowSurfacePlatformBackend =
                    [](const Tina::Platform::PlatformBackendCreateParams& params)
                    -> Tina::Core::Result<std::unique_ptr<Tina::Integration::IWindowSurfacePlatformBackend>> {
                    return Tina::Platform::createHtml5WindowSurfacePlatformBackend(
                        Tina::Platform::Html5PlatformCreateParams{
                            .base = params,
                            .canvasSelector = "#canvas",
                        });
                },
                .createWindowSurfaceRenderDevice = Tina::Render::Bgfx::createBgfxRenderDevice,
            },
    };
}

// Everything the main-loop callback needs. It has to outlive main(), because main()
// returns while the loop keeps running.
struct WebSession final {
    LifecycleCounters counters{};
    std::unique_ptr<Tina::EngineHost> host{};
    std::unique_ptr<WebSampleApplication> application{};
    bool finished = false;
};

[[nodiscard]] WebSession& webSession()
{
    static WebSession instance;
    return instance;
}

// Field order of the block handed to the page. Kept in one place because the JS side reads
// it positionally out of the heap; adding a field means adding it at the end and widening
// the reader in shell.html.
enum class ReportField : int {
    Frames = 0,
    ForwardStarts,
    EscapeRequested,
    State,
    TapStarts,
    SecondaryClicks,
    MiddleClicks,
    PointerMoveFrames,
    PointerTravel,
    LockRequested,
    LockRequestFailed,
    CaptureLocked,
    WheelFrames,
    WheelCentinotches,
    MaxConcurrentPointers,
    PointerSlotsSeen,
    TextureUploaded,
    Count,
};

// Pushes the live counters into the page, so the interactive check needs no console.
// A shell without the hook is fine: the JSON on stdout stays the authoritative record.
void publishCounters(const LifecycleCounters& counters, SessionState state)
{
    // A heap block rather than EM_ASM varargs: the field count already outgrew a readable
    // positional list, and this keeps the marshalling to plain integers, so the bridge never
    // depends on UTF8ToString or a string surviving dead-code elimination.
    static int fields[static_cast<int>(ReportField::Count)] = {};
    const auto set = [](ReportField field, int value) noexcept { fields[static_cast<int>(field)] = value; };
    set(ReportField::Frames, static_cast<int>(counters.frameUpdates));
    set(ReportField::ForwardStarts, static_cast<int>(counters.forwardStarts));
    set(ReportField::EscapeRequested, counters.escapeRequested ? 1 : 0);
    set(ReportField::State, static_cast<int>(state));
    set(ReportField::TapStarts, static_cast<int>(counters.tapStarts));
    set(ReportField::SecondaryClicks, static_cast<int>(counters.secondaryClicks));
    set(ReportField::MiddleClicks, static_cast<int>(counters.middleClicks));
    set(ReportField::PointerMoveFrames, static_cast<int>(counters.pointerMoveFrames));
    // Rounded to whole logical units: the gate asserts motion arrived and is of the right
    // order, not an exact float, which would depend on how the browser batches moves.
    set(ReportField::PointerTravel, static_cast<int>(counters.pointerTravel + 0.5));
    set(ReportField::LockRequested, counters.lockRequested ? 1 : 0);
    set(ReportField::LockRequestFailed, counters.lockRequestFailed ? 1 : 0);
    set(ReportField::CaptureLocked, counters.captureMode == Tina::Platform::PointerCaptureMode::Locked ? 1 : 0);
    set(ReportField::WheelFrames, static_cast<int>(counters.wheelFrames));
    // Hundredths of a notch, so the bridge stays integer-only while still carrying the
    // fractional part the pixel-mode normalisation produces. Rounded to nearest rather than
    // truncated: this is a fixed-point encoding of a double, and truncation would bias every
    // value toward zero, whereas rounding keeps the error symmetric at +/-0.005 notches.
    set(ReportField::WheelCentinotches, static_cast<int>(std::llround(counters.wheelNotches * 100.0)));
    set(ReportField::MaxConcurrentPointers, static_cast<int>(counters.maxConcurrentPointers));
    set(ReportField::PointerSlotsSeen, static_cast<int>(counters.pointerSlotsSeen));
    set(ReportField::TextureUploaded, counters.textureUploaded ? 1 : 0);

    // The heap read happens here, inside the module's own JS scope, and the page receives a
    // plain array. Doing it in shell.html instead would depend on HEAP32 being reachable from
    // page scope, which is an artefact of the current output format rather than a contract.
    EM_ASM(
        {
            if (typeof globalThis.tinaReport === 'function')
            {
                const base = $0 >> 2;
                const values = [];
                for (let index = 0; index < $1; ++index)
                {
                    values.push(HEAP32[base + index]);
                }
                globalThis.tinaReport(values);
            }
        },
        static_cast<int>(reinterpret_cast<std::uintptr_t>(static_cast<const void*>(fields))),
        static_cast<int>(ReportField::Count));
}

void reportOutcome(Tina::RunExitReason exitReason)
{
    const LifecycleCounters& counters = webSession().counters;
    const bool lifecycleHeld = counters.stateExits == 1 && counters.applicationShutdowns == 1;

    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", lifecycleHeld ? "ok" : "error");
    writer.member("sample", "tina_sample_web");
    writer.member("frames", counters.frameUpdates);
    writer.member("forwardStarts", counters.forwardStarts);
    writer.member("tapStarts", counters.tapStarts);
    writer.member("secondaryClicks", counters.secondaryClicks);
    writer.member("middleClicks", counters.middleClicks);
    writer.member("pointerMoveFrames", counters.pointerMoveFrames);
    writer.member("pointerTravel", static_cast<u64>(counters.pointerTravel + 0.5));
    writer.member("wheelFrames", counters.wheelFrames);
    writer.member("wheelCentinotches", static_cast<Tina::Core::i64>(std::llround(counters.wheelNotches * 100.0)));
    writer.member("maxConcurrentPointers", counters.maxConcurrentPointers);
    writer.member("pointerSlotsSeen", counters.pointerSlotsSeen);
    writer.member("lockRequested", counters.lockRequested);
    writer.member("lockRequestFailed", counters.lockRequestFailed);
    writer.member("captureLocked", counters.captureMode == Tina::Platform::PointerCaptureMode::Locked);
    writer.member("escapeRequested", counters.escapeRequested);
    writer.member("textureUploaded", counters.textureUploaded);
    writer.member("exitReason", static_cast<unsigned>(exitReason));
    writer.member("stateExits", counters.stateExits);
    writer.member("applicationShutdowns", counters.applicationShutdowns);
    writer.endObject();
    std::cout << '\n';

    publishCounters(counters, lifecycleHeld ? SessionState::Finished : SessionState::LifecycleMismatch);
}

void frameCallback()
{
    WebSession& session = webSession();
    if (session.finished)
    {
        return;
    }

    auto outcome = session.host->tick(*session.application);
    if (!outcome)
    {
        session.finished = true;
        writeError(outcome.error());
        publishCounters(session.counters, SessionState::TickFailed);
        emscripten_cancel_main_loop();
        return;
    }
    if (outcome->has_value())
    {
        // Terminal: teardown already ran inside tick(), so neither start() nor tick()
        // may be called again.
        session.finished = true;
        reportOutcome(**outcome);
        emscripten_cancel_main_loop();
        return;
    }

    publishCounters(session.counters, SessionState::Running);
}

} // namespace

int main()
{
    WebSession& session = webSession();

    auto host = Tina::EngineHost::Create(createEngineConfig(), createEngineFactories());
    if (!host)
    {
        writeError(host.error());
        publishCounters(session.counters, SessionState::CreateFailed);
        return 1;
    }
    session.host = std::move(*host);
    session.application = std::make_unique<WebSampleApplication>(session.counters);

    if (auto started = session.host->start(*session.application); !started)
    {
        writeError(started.error());
        publishCounters(session.counters, SessionState::StartFailed);
        return 1;
    }

    // fps 0 means requestAnimationFrame, the only cadence a browser can honour.
    // simulate_infinite_loop false lets main() return so the browser event loop keeps
    // running; the session is a function-local static, so it survives that return.
    emscripten_set_main_loop(frameCallback, 0, /*simulate_infinite_loop=*/false);
    return 0;
}
