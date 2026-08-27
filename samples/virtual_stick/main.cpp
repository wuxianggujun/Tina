// On-screen thumbstick driven by pointer drag or WASD, in one control.
//
// Two things are being checked here. First, that a virtual stick can be built as a
// product-side composition -- two Elements plus routed pointer listeners -- without
// a new engine widget or new pointer plumbing (ADR 0023 defers a custom Behavior
// SPI, so the drag state machine belongs to the product). Second, that WASD and a
// finger produce the *same* values, which is the only way to test a touch control
// on a desktop without a touchscreen.
//
// The stick math itself lives in <tina/ui/UIVirtualStick.hpp> with unit tests; this
// sample is the consumer that proves it reaches the screen.
//
// Run it and drag inside the ring, or hold WASD. --auto-demo scripts a fixed
// direction sequence so the same behaviour has automated evidence.

#include <tina/core/error/Error.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UIVirtualStick.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

// Bumped whenever a field is added, removed or redefined.
inline constexpr u32 EvidenceSchema = 1;
inline constexpr u64 DefaultFrameCount = 300;

// Actions, one per direction rather than one signed axis per pair: the stick takes
// four independent digital inputs so opposite keys can cancel rather than one
// winning by evaluation order.
inline constexpr Tina::InputActionId MoveUpAction{1};
inline constexpr Tina::InputActionId MoveDownAction{2};
inline constexpr Tina::InputActionId MoveLeftAction{3};
inline constexpr Tina::InputActionId MoveRightAction{4};
// Geometry. 56 is a thumb-sized base at 1x; the knob is deliberately well under
// half of it so the travel ring reads as a ring rather than as two nested discs.
inline constexpr float BaseRadius = 56.0F;
inline constexpr float KnobRadius = 24.0F;
inline constexpr float StickMargin = 32.0F;

inline constexpr Tina::UI::UIVirtualStickConfig StickConfig{
    .baseRadius = BaseRadius,
    .knobRadius = KnobRadius,
    // Same value the GLFW backend applies to physical sticks, so a virtual stick
    // and a real one feel identical rather than one being twitchier.
    .deadzone = 0.18F,
};

// The scripted directions --auto-demo walks, one per segment. Chosen to cover both
// cardinals and a diagonal, plus a rest segment that must return the stick to zero.
struct DemoSegment final {
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    const char* name = "idle";
};

inline constexpr DemoSegment DemoSegments[]{
    {.name = "idle"},
    {.right = true, .name = "right"},
    {.up = true, .name = "up"},
    {.left = true, .name = "left"},
    {.down = true, .name = "down"},
    {.right = true, .up = true, .name = "up-right"},
    // Opposite keys held together must cancel, not pick a winner.
    {.left = true, .right = true, .name = "left+right"},
    {.name = "idle"},
};
inline constexpr u64 DemoSegmentCount = sizeof(DemoSegments) / sizeof(DemoSegments[0]);

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    bool autoDemo = false;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 uiUpdates = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;

    // Pointer path
    u64 pointerPresses = 0;
    u64 pointerDrags = 0;
    u64 pointerReleases = 0;
    u64 pointerCancels = 0;
    u64 pointerPressesOutsideBase = 0;
    u64 secondPointerRejected = 0;

    // Digital path
    u64 digitalEngagedFrames = 0;
    u64 demoSegmentsObserved = 0;

    // Output witnesses. A stick that never leaves zero would otherwise look like a
    // passing run.
    float maximumMagnitude = 0.0F;
    float maximumKnobDistance = 0.0F;
    bool observedUp = false;
    bool observedDown = false;
    bool observedLeft = false;
    bool observedRight = false;
    bool observedDiagonal = false;
    bool observedOppositeKeysCancel = false;
    bool knobStayedInsideRing = true;
    bool magnitudeStayedNormalized = true;
    bool recentredAfterRelease = true;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Virtual Stick";
    config.primaryWindow.title = "Tina Virtual Stick — drag the ring or hold WASD";
    config.primaryWindow.initialLogicalExtent = {720, 480};
    config.primaryWindow.initiallyVisible = true;

    const auto bindKey = [&config](Tina::Platform::Key key, Tina::InputActionId action) {
        config.inputActions.bindings.push_back(Tina::InputActionBinding{
            .input = Tina::PrimaryWindowKeyBinding{.key = key},
            .action = action,
            .domain = Tina::InputActionDomain::Frame,
        });
    };
    bindKey(Tina::Platform::Key::W, MoveUpAction);
    bindKey(Tina::Platform::Key::S, MoveDownAction);
    bindKey(Tina::Platform::Key::A, MoveLeftAction);
    bindKey(Tina::Platform::Key::D, MoveRightAction);
    // Arrows too, so the sample is usable without remembering it is WASD.
    bindKey(Tina::Platform::Key::Up, MoveUpAction);
    bindKey(Tina::Platform::Key::Down, MoveDownAction);
    bindKey(Tina::Platform::Key::Left, MoveLeftAction);
    bindKey(Tina::Platform::Key::Right, MoveRightAction);
    return config;
}

[[nodiscard]] Tina::UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
{
    return Tina::UI::UIBoxPaint{
        .solidFill = Tina::UI::UISolidFill{
            .color = {.red = red, .green = green, .blue = blue, .alpha = alpha},
        },
    };
}

[[nodiscard]] Tina::UI::UILayoutStyle overlayStyle(float left, float top, float width,
                                                  float height) noexcept
{
    Tina::UI::UILayoutStyle style{};
    style.placement = Tina::UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = Tina::UI::UILayoutLength::Px(left);
    style.overlay.offset.y = Tina::UI::UILayoutLength::Px(top);
    style.size.width = Tina::UI::UILayoutLength::Px(width);
    style.size.height = Tina::UI::UILayoutLength::Px(height);
    return style;
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_virtual_stick\",\"code\":"
              << error.code.value << ",\"message\":\"" << error.message << "\"}\n";
}

class VirtualStickState final : public Tina::IGameState {
  public:
    VirtualStickState(const SampleOptions& options, LifecycleCounters& counters) noexcept
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
        root_ = std::move(*root);
        auto tree = rootBuilder->treeUpdater(root_);
        if (!tree)
        {
            return Tina::Core::failure(std::move(tree.error()));
        }

        Tina::UI::UILayoutStyle rootStyle{};
        rootStyle.size.width = Tina::UI::UILayoutLength::Percent(100.0F);
        rootStyle.size.height = Tina::UI::UILayoutLength::Percent(100.0F);
        if (auto status = tree->setLayoutStyle(root_.rootNodeId(), rootStyle); !status)
        {
            return status;
        }
        if (auto status = buildStick(*tree); !status)
        {
            return status;
        }
        return registerPointerListeners(*tree);
    }
    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++counters_->stateExits;
        for (auto& listener : pointerListeners_)
        {
            listener = {};
        }
        root_ = {};
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        // Only a *pointer* engagement blocks the digital path. Testing
        // stick_.engaged instead would latch: the digital path sets engaged itself,
        // so after the first key press the guard would skip every later frame and
        // the stick would freeze at whatever direction it first saw.
        if (!pointerEngaged_)
        {
            const bool left = digitalHeld(context, MoveLeftAction, 0);
            const bool right = digitalHeld(context, MoveRightAction, 1);
            const bool up = digitalHeld(context, MoveUpAction, 2);
            const bool down = digitalHeld(context, MoveDownAction, 3);
            stick_ = Tina::UI::virtualStickFromDigital(StickConfig, left, right, up, down);
            if (stick_.engaged)
            {
                ++counters_->digitalEngagedFrames;
            }
            if (((left && right) || (up && down)) && stick_.magnitude == 0.0F)
            {
                counters_->observedOppositeKeysCancel = true;
            }
        }
        observe();
        if (context.frameTiming().frameIndex + 1U == options_.targetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override
    {
        ++counters_->uiUpdates;
        if (!context.hasPrimaryWindowUI())
        {
            return Tina::Core::success();
        }
        auto tree = context.primaryWindowUITreeUpdater(root_);
        if (!tree)
        {
            return Tina::Core::failure(std::move(tree.error()));
        }
        // Presentation follows state rather than the reverse, so pointer and
        // keyboard drive exactly the same visual.
        if (auto status = tree->setLayoutStyle(knobNode_, knobStyle(stick_.knobOffset)); !status)
        {
            return status;
        }
        return tree->setText(readoutNode_, readoutText());
    }

  private:
    Tina::Core::Status buildStick(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        // The base ring. Pointer hit policy must be set explicitly: a bare Panel
        // defaults to Ignore and is only auto-promoted when it carries a standard
        // behavior, which a custom drag control does not.
        auto base = tree.createElement(root_.rootNodeId(), Tina::UI::makePanelElement());
        if (!base)
        {
            return Tina::Core::failure(std::move(base.error()));
        }
        baseNode_ = *base;
        baseRect_ = Tina::UI::UILogicalRect{
            .x = StickMargin,
            .y = StickMargin,
            .width = BaseRadius * 2.0F,
            .height = BaseRadius * 2.0F,
        };
        if (auto status = tree.setLayoutStyle(
                baseNode_,
                overlayStyle(baseRect_.x, baseRect_.y, baseRect_.width, baseRect_.height));
            !status)
        {
            return status;
        }
        // A translucent ring rather than a filled disc, so the knob reads against
        // it. The stroke is inward from the bounds, so the outer radius is exact.
        if (auto status = tree.setBoxPaint(
                baseNode_,
                Tina::UI::makeEllipseOutline({.red = 210, .green = 220, .blue = 235, .alpha = 150},
                                             6.0F));
            !status)
        {
            return status;
        }
        if (auto status =
                tree.setPointerHitPolicy(baseNode_, Tina::UI::UIPointerHitPolicy::Targetable);
            !status)
        {
            return status;
        }

        // The knob is a sibling, not a child: a child would be laid out inside the
        // base's box, and the knob has to sit wherever the drag puts it.
        auto knob = tree.createElement(root_.rootNodeId(), Tina::UI::makePanelElement());
        if (!knob)
        {
            return Tina::Core::failure(std::move(knob.error()));
        }
        knobNode_ = *knob;
        if (auto status = tree.setLayoutStyle(knobNode_, knobStyle(Tina::UI::UILogicalPoint{}));
            !status)
        {
            return status;
        }
        if (auto status = tree.setBoxPaint(
                knobNode_,
                Tina::UI::makeSolidEllipse({.red = 76, .green = 154, .blue = 255, .alpha = 235}));
            !status)
        {
            return status;
        }
        // The knob must not eat the drag: it sits under the pointer for the whole
        // gesture, so if it were targetable it would steal the press from the base.
        if (auto status = tree.setPointerHitPolicy(knobNode_, Tina::UI::UIPointerHitPolicy::Ignore);
            !status)
        {
            return status;
        }

        auto readout = tree.createElement(root_.rootNodeId(), Tina::UI::makeLabelElement());
        if (!readout)
        {
            return Tina::Core::failure(std::move(readout.error()));
        }
        readoutNode_ = *readout;
        if (auto status = tree.setLayoutStyle(
                readoutNode_,
                overlayStyle(StickMargin, StickMargin + BaseRadius * 2.0F + 24.0F, 420.0F, 28.0F));
            !status)
        {
            return status;
        }
        return tree.setBoxPaint(readoutNode_, solidFill(24, 28, 36, 200));
    }

    [[nodiscard]] Tina::UI::UILayoutStyle knobStyle(Tina::UI::UILogicalPoint offset) const noexcept
    {
        // The offset is centre-to-centre, but layout positions the top-left corner.
        const Tina::UI::UILogicalPoint c = Tina::UI::virtualStickCenter(baseRect_);
        return overlayStyle(c.x + offset.x - KnobRadius, c.y + offset.y - KnobRadius,
                            KnobRadius * 2.0F, KnobRadius * 2.0F);
    }

    // In --auto-demo a scripted segment replaces real key state, so the same
    // behaviour has automated evidence on a host with no one at the keyboard.
    [[nodiscard]] bool digitalHeld(Tina::FrameUpdateContext& context, Tina::InputActionId action,
                                  int direction) noexcept
    {
        if (!options_.autoDemo)
        {
            return context.frameActions().value(action) > 0.5F;
        }
        const u64 segment =
            (context.frameTiming().frameIndex * DemoSegmentCount) / options_.targetFrameCount;
        const u64 index = segment < DemoSegmentCount ? segment : DemoSegmentCount - 1U;
        if (index != observedSegment_)
        {
            observedSegment_ = index;
            ++counters_->demoSegmentsObserved;
        }
        const DemoSegment& scripted = DemoSegments[index];
        switch (direction)
        {
        case 0:
            return scripted.left;
        case 1:
            return scripted.right;
        case 2:
            return scripted.up;
        default:
            return scripted.down;
        }
    }

    void observe() noexcept
    {
        const float distance = std::sqrt(stick_.knobOffset.x * stick_.knobOffset.x
                                        + stick_.knobOffset.y * stick_.knobOffset.y);
        counters_->maximumMagnitude = std::max(counters_->maximumMagnitude, stick_.magnitude);
        counters_->maximumKnobDistance = std::max(counters_->maximumKnobDistance, distance);
        // Half a pixel of tolerance: the travel radius is exact, so anything past it
        // means the clamp failed rather than that floats drifted.
        if (distance > Tina::UI::virtualStickTravelRadius(StickConfig) + 0.5F)
        {
            counters_->knobStayedInsideRing = false;
        }
        if (stick_.magnitude < 0.0F || stick_.magnitude > 1.0F + 1.0e-4F)
        {
            counters_->magnitudeStayedNormalized = false;
        }
        if (!stick_.engaged && (stick_.magnitude != 0.0F || distance != 0.0F))
        {
            counters_->recentredAfterRelease = false;
        }
        constexpr float Witness = 0.5F;
        counters_->observedUp = counters_->observedUp || stick_.y < -Witness;
        counters_->observedDown = counters_->observedDown || stick_.y > Witness;
        counters_->observedLeft = counters_->observedLeft || stick_.x < -Witness;
        counters_->observedRight = counters_->observedRight || stick_.x > Witness;
        counters_->observedDiagonal = counters_->observedDiagonal
            || (std::abs(stick_.x) > 0.3F && std::abs(stick_.y) > 0.3F);
    }

    [[nodiscard]] std::string readoutText() const
    {
        char buffer[96]{};
        const int written =
            std::snprintf(buffer, sizeof(buffer), "x %+.2f  y %+.2f  mag %.2f  %s", stick_.x,
                          stick_.y, stick_.magnitude, stick_.engaged ? "ACTIVE" : "idle");
        return written > 0 ? std::string(buffer, static_cast<std::size_t>(written)) : std::string{};
    }

    Tina::Core::Status registerPointerListeners(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const auto add = [&](u32 index, Tina::UI::UIRoutedPointerEventKind kind,
                             Tina::UI::UIRoutedPointerCallback callback) -> Tina::Core::Status {
            auto listener = tree.addRoutedPointerListener(
                {
                    .node = baseNode_,
                    .kind = kind,
                    .phases = Tina::UI::UIEventPhaseMask::Target,
                },
                std::move(callback));
            if (!listener)
            {
                return Tina::Core::failure(std::move(listener.error()));
            }
            pointerListeners_[index] = std::move(*listener);
            return Tina::Core::success();
        };

        if (auto status =
                add(0, Tina::UI::UIRoutedPointerEventKind::ButtonDown,
                    Tina::UI::UIRoutedPointerCallback{
                        [this](Tina::UI::UIRoutedPointerEvent& event) noexcept { onDown(event); }});
            !status)
        {
            return status;
        }
        if (auto status =
                add(1, Tina::UI::UIRoutedPointerEventKind::Move,
                    Tina::UI::UIRoutedPointerCallback{
                        [this](Tina::UI::UIRoutedPointerEvent& event) noexcept { onMove(event); }});
            !status)
        {
            return status;
        }
        if (auto status =
                add(2, Tina::UI::UIRoutedPointerEventKind::ButtonUp,
                    Tina::UI::UIRoutedPointerCallback{
                        [this](Tina::UI::UIRoutedPointerEvent& event) noexcept { onUp(event); }});
            !status)
        {
            return status;
        }
        return add(3, Tina::UI::UIRoutedPointerEventKind::PointerCancel,
                   Tina::UI::UIRoutedPointerCallback{
                       [this](Tina::UI::UIRoutedPointerEvent& event) noexcept { onCancel(event); }});
    }

    void onDown(Tina::UI::UIRoutedPointerEvent& event) noexcept
    {
        const Tina::UI::UIPointerInputEvent& input = event.input();
        if (input.button != Tina::Platform::PointerButton::Primary)
        {
            return;
        }
        const bool alreadyEngaged = stick_.engaged;
        if (!Tina::UI::pressVirtualStick(stick_, StickConfig,
                                        Tina::UI::virtualStickCenter(baseRect_), input.pointer,
                                        input.position))
        {
            // Either the press landed in the Element's box but outside the disc, or
            // another pointer owns the stick. Counted separately so the sample can
            // show the circular hit test and the single-owner rule really fire.
            if (alreadyEngaged)
            {
                ++counters_->secondPointerRejected;
            }
            else
            {
                ++counters_->pointerPressesOutsideBase;
            }
            return;
        }
        pointerEngaged_ = true;
        ++counters_->pointerPresses;
        // Capture keeps Move arriving here after the pointer leaves the ring, which
        // is exactly the case where a naive implementation freezes the knob.
        event.capturePointer();
        static_cast<void>(event.claimPointerButton(input.button));
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void onMove(Tina::UI::UIRoutedPointerEvent& event) noexcept
    {
        const Tina::UI::UIPointerInputEvent& input = event.input();
        if (!Tina::UI::dragVirtualStick(stick_, StickConfig,
                                       Tina::UI::virtualStickCenter(baseRect_), input.pointer,
                                       input.position))
        {
            return;
        }
        ++counters_->pointerDrags;
        static_cast<void>(event.claimPointerButton(Tina::Platform::PointerButton::Primary));
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void onUp(Tina::UI::UIRoutedPointerEvent& event) noexcept
    {
        if (!Tina::UI::releaseVirtualStick(stick_, event.input().pointer))
        {
            return;
        }
        pointerEngaged_ = false;
        ++counters_->pointerReleases;
        event.releasePointerCapture();
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void onCancel(Tina::UI::UIRoutedPointerEvent& event) noexcept
    {
        // Focus loss and device loss do not name a pointer, so a cancel recentres
        // unconditionally. A knob left deflected keeps steering after the window
        // has stopped receiving input.
        if (!stick_.engaged)
        {
            return;
        }
        Tina::UI::cancelVirtualStick(stick_);
        pointerEngaged_ = false;
        ++counters_->pointerCancels;
        event.releasePointerCapture();
    }

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Tina::UI::UIRootOwner root_{};
    Tina::UI::UINodeId baseNode_{};
    Tina::UI::UINodeId knobNode_{};
    Tina::UI::UINodeId readoutNode_{};
    Tina::UI::UILogicalRect baseRect_{};
    Tina::UI::UIVirtualStickState stick_{};
    // Tracked separately from stick_.engaged, which the digital path also sets.
    bool pointerEngaged_ = false;
    Tina::UI::UIRoutedPointerListenerToken pointerListeners_[4]{};
    u64 observedSegment_ = 0;
};

class VirtualStickApplication final : public Tina::IGameApplication {
  public:
    VirtualStickApplication(const SampleOptions& options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<VirtualStickState>(options_, *counters_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_->applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
};

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    SampleOptions options{};
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument == "--auto-demo")
        {
            options.autoDemo = true;
            continue;
        }
        constexpr std::string_view framesPrefix = "--frames=";
        if (argument.starts_with(framesPrefix))
        {
            const std::string_view text = argument.substr(framesPrefix.size());
            u64 value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() || value == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must be a positive integer");
            }
            options.targetFrameCount = value;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Unknown argument; expected --frames=N or --auto-demo");
    }
    if (options.autoDemo && options.targetFrameCount < DemoSegmentCount * 2U)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--auto-demo needs at least two frames per scripted segment");
    }
    return options;
}

void printEvidence(std::ostream& out, const char* status, const SampleOptions& options,
                   const LifecycleCounters& counters)
{
    out << "{\"status\":\"" << status << "\",\"sample\":\"tina_sample_virtual_stick\""
        << ",\"evidenceSchema\":" << EvidenceSchema << ",\"frames\":" << options.targetFrameCount
        << ",\"autoDemo\":" << (options.autoDemo ? "true" : "false")
        << ",\"frameUpdates\":" << counters.frameUpdates << ",\"uiUpdates\":" << counters.uiUpdates
        << ",\"baseRadius\":" << BaseRadius << ",\"knobRadius\":" << KnobRadius
        << ",\"travelRadius\":" << Tina::UI::virtualStickTravelRadius(StickConfig)
        << ",\"deadzone\":" << StickConfig.deadzone
        << ",\"pointerPresses\":" << counters.pointerPresses
        << ",\"pointerDrags\":" << counters.pointerDrags
        << ",\"pointerReleases\":" << counters.pointerReleases
        << ",\"pointerCancels\":" << counters.pointerCancels
        << ",\"pointerPressesOutsideBase\":" << counters.pointerPressesOutsideBase
        << ",\"secondPointerRejected\":" << counters.secondPointerRejected
        << ",\"digitalEngagedFrames\":" << counters.digitalEngagedFrames
        << ",\"demoSegmentsObserved\":" << counters.demoSegmentsObserved
        << ",\"maximumMagnitude\":" << counters.maximumMagnitude
        << ",\"maximumKnobDistance\":" << counters.maximumKnobDistance
        << ",\"observedUp\":" << (counters.observedUp ? "true" : "false")
        << ",\"observedDown\":" << (counters.observedDown ? "true" : "false")
        << ",\"observedLeft\":" << (counters.observedLeft ? "true" : "false")
        << ",\"observedRight\":" << (counters.observedRight ? "true" : "false")
        << ",\"observedDiagonal\":" << (counters.observedDiagonal ? "true" : "false")
        << ",\"observedOppositeKeysCancel\":"
        << (counters.observedOppositeKeysCancel ? "true" : "false")
        << ",\"knobStayedInsideRing\":" << (counters.knobStayedInsideRing ? "true" : "false")
        << ",\"magnitudeStayedNormalized\":"
        << (counters.magnitudeStayedNormalized ? "true" : "false")
        << ",\"recentredAfterRelease\":" << (counters.recentredAfterRelease ? "true" : "false")
        << ",\"stateEnters\":" << counters.stateEnters << ",\"stateExits\":" << counters.stateExits
        << ",\"applicationShutdowns\":" << counters.applicationShutdowns << "}\n";
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
    VirtualStickApplication application{options, counters};
    auto runResult = (*hostResult)->run(application);
    if (!runResult)
    {
        writeError(runResult.error());
        printEvidence(std::cerr, "error", options, counters);
        return 1;
    }

    // Invariants that must hold whether or not anyone touched the stick.
    bool ok = *runResult == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame
        && counters.frameUpdates == options.targetFrameCount && counters.uiUpdates > 0
        && counters.stateEnters == 1 && counters.stateExits == 1
        && counters.applicationShutdowns == 1 && counters.knobStayedInsideRing
        && counters.magnitudeStayedNormalized && counters.recentredAfterRelease
        && counters.maximumMagnitude <= 1.0F + 1.0e-4F
        && counters.maximumKnobDistance <= Tina::UI::virtualStickTravelRadius(StickConfig) + 0.5F;
    if (options.autoDemo)
    {
        // The scripted run must actually exercise every direction, or a stick stuck
        // at zero would pass. Opposite keys must cancel, and the final segment is
        // idle so the stick must have recentred by the end.
        ok = ok && counters.demoSegmentsObserved + 1U == DemoSegmentCount
            && counters.digitalEngagedFrames > 0 && counters.observedUp && counters.observedDown
            && counters.observedLeft && counters.observedRight && counters.observedDiagonal
            && counters.observedOppositeKeysCancel && counters.maximumMagnitude >= 1.0F - 1.0e-4F;
    }

    if (!ok)
    {
        printEvidence(std::cerr, "error", options, counters);
        return 1;
    }
    printEvidence(std::cout, "ok", options, counters);
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
        writeError(Tina::Core::Error{Tina::Core::CoreErrorCode::OutOfMemory,
                                     "The virtual stick sample ran out of memory"});
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the virtual stick sample boundary"};
        error.addContext("tina_sample_virtual_stick",
                         exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        writeError(Tina::Core::Error{
            Tina::Core::CoreErrorCode::Internal,
            "A non-standard exception crossed the virtual stick sample boundary"});
        return 1;
    }
}
