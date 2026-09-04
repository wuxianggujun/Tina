#include "TinaAndroidGame.hpp"

#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/io/ContentRoot.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UIPaint.hpp>

#include <android/log.h>

#include <array>
#include <optional>

#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Platform::Android {
namespace {

// Binds the catalog the APK shipped and reads one asset out of it.
//
// Bound, not cooked. The desktop template cooks at startup because that always works with nothing but
// a recipe beside the executable; here the catalog arrives already cooked, by a host tina_assetc during
// the Gradle build. That is the one decision in this file worth defending: cooking on a phone would put
// image processing on the device's CPU at every first launch, for output that is identical on every
// device -- the cook is deterministic, so the only thing a device can add is latency and heat.
//
// The path comes from the root rather than from anything here, which is what makes this the same code
// desktop and the browser run. On Android the root is the extraction directory the activity copied
// assets/ into, so "content" below is assets/content/ inside the APK.
//
// A failure is reported and swallowed: content that will not load is not a reason to refuse to start,
// and the counters this writes are how the failure is observed from outside the process.
//
// Returns nullopt rather than an error, and by value rather than through an out-parameter: AssetSystem
// is move-constructible but not move-assignable, so an out-parameter could only be filled by emplace.
[[nodiscard]] std::optional<Asset::AssetSystem> openShippedContent(const Core::ContentRoot& root,
                                                                   AndroidGameTelemetry& telemetry)
{
    if (root.empty())
    {
        // Legal, and the honest reading is "this build shipped no content" -- which is what a build
        // without -Ptina.assetc produces. Not logged as an error, or every such build would report a
        // fault it does not have.
        __android_log_print(ANDROID_LOG_INFO, "Tina", "no content root; this build ships no catalog");
        return std::nullopt;
    }

    auto catalogRoot = root.resolve("content");
    if (!catalogRoot)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Tina", "resolving the catalog root failed: %s",
                            catalogRoot.error().message.c_str());
        return std::nullopt;
    }

    auto system = Asset::AssetSystem::Create(Asset::AssetSystemConfig{
        .storeCapacity = 32,
        .memoryResource = std::pmr::get_default_resource(),
    });
    if (!system)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Tina", "creating the asset system failed: %s",
                            system.error().message.c_str());
        return std::nullopt;
    }
    // Validated on open by default, which is what makes this a real test of the packaging chain: every
    // object's size is checked against the manifest's cookedFileBytes, so an asset the APK packager or
    // the extraction step altered by even one byte fails right here instead of surfacing as corrupt
    // pixels much later.
    if (auto bound = system->openAndBindCatalog(*catalogRoot); !bound)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Tina", "opening the catalog at %s failed: %s",
                            catalogRoot->c_str(), bound.error().message.c_str());
        return std::nullopt;
    }

    const auto* catalog = system->catalog();
    telemetry.contentCatalogEntries.store(catalog == nullptr ? 0U : catalog->entryCount(),
                                          std::memory_order_release);

    // One texture, loaded and parsed. Binding alone only proves the manifest was readable; nothing had
    // opened an object file underneath it, so a catalog whose objects were all unreadable would still
    // have looked like a working content chain.
    if (const auto textureId = system->catalogFirstIdOfKind(AssetFormat::AssetKind::Texture2D))
    {
        auto handle = system->loadOne(*textureId);
        if (!handle)
        {
            __android_log_print(ANDROID_LOG_ERROR, "Tina", "loading the shipped texture failed: %s",
                                handle.error().message.c_str());
        } else if (const auto* file = system->tryGet(*handle); file != nullptr)
        {
            telemetry.contentAssetsLoaded.fetch_add(1, std::memory_order_relaxed);
            if (auto texture = Asset::parseTexture2DFromCooked(*file))
            {
                telemetry.contentTextureExtent.store(
                    (static_cast<u32>(texture->width) << 16U) | static_cast<u32>(texture->height),
                    std::memory_order_release);
            } else
            {
                __android_log_print(ANDROID_LOG_ERROR, "Tina", "parsing the shipped texture failed: %s",
                                    texture.error().message.c_str());
            }
        }
    }

    return std::optional<Asset::AssetSystem>{std::move(*system)};
}

// Panel colours. Chosen to be unmistakable in a screenshot histogram: neither matches the
// RenderDevice clear colour or any Android system background, so a pixel sample can tell "the engine
// drew this" from "something else is on screen".
constexpr UI::UIStraightSrgba8Color PanelFill{.red = 220, .green = 90, .blue = 40, .alpha = 255};
constexpr UI::UIStraightSrgba8Color PulseFill{.red = 60, .green = 190, .blue = 120, .alpha = 255};

class AndroidGameState final : public IGameState {
  public:
    AndroidGameState(AndroidGameTelemetry& telemetry, std::optional<Asset::AssetSystem> content) noexcept
        : telemetry_(&telemetry), content_(std::move(content))
    {
    }

    Core::Status onEnter(GameStateEnterContext& context) override
    {
        telemetry_->stateEntered.store(true, std::memory_order_release);

        // The root is created here, not lazily in updateUI: a default-constructed UIRootOwner has no
        // root, and the tree updater rejects it with "UI tree updater requires a root owner". Building
        // it at enter time also means the tree exists before the first frame is drawn.
        auto rootBuilder = context.primaryWindowUIRootBuilder();
        if (!rootBuilder)
        {
            return Core::failure(std::move(rootBuilder.error()));
        }
        auto root = rootBuilder->createRoot();
        if (!root)
        {
            return Core::failure(std::move(root.error()));
        }
        root_ = std::move(*root);

        auto tree = rootBuilder->treeUpdater(root_);
        if (!tree)
        {
            return Core::failure(std::move(tree.error()));
        }
        // The root must fill the window, or percentage-sized children resolve against an auto-sized
        // parent and collapse to nothing -- visible only as "the panel never appears".
        UI::UILayoutStyle rootStyle{};
        rootStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        rootStyle.size.height = UI::UILayoutLength::Percent(100.0F);
        if (auto status = tree->setLayoutStyle(root_.rootNodeId(), rootStyle); !status)
        {
            return status;
        }
        return buildContent(*tree);
    }

    void onExit(GameStateExitContext&) noexcept override
    {
        // Listeners first: they hold nodes from this root, and outliving the root would leave
        // registrations pointing at a tree that no longer exists.
        for (auto& listener : pointerListeners_)
        {
            listener = {};
        }
        // Released before the UI context goes away. Holding root nodes past the context that owns them
        // is what turns a clean shutdown into a stale-handle failure.
        root_.reset();
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        // Nothing is blocked: this is the only state on the stack, so every "blocks below" flag would
        // describe a layer that does not exist.
        return GameStatePolicy{};
    }

    Core::Status fixedUpdate(FixedUpdateContext&) override
    {
        telemetry_->fixedUpdates.fetch_add(1, std::memory_order_relaxed);
        return Core::success();
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        telemetry_->frameUpdates.fetch_add(1, std::memory_order_relaxed);

        // A bound key is read as an action, which is how key input reaches game code at all. Without
        // this the key would reach WindowInputSnapshot and stop there, indistinguishable from a bridge
        // that does not work.
        const bool keyHeld = context.frameActions().isActive(AndroidHighlightAction);
        if (keyHeld && !keyWasHeld_)
        {
            telemetry_->keyPresses.fetch_add(1, std::memory_order_relaxed);
            // The same press asks for the soft keyboard, so text input is reachable on a device with no
            // hardware keys at all. The request is only latched here; the host performs it, because only
            // Java can call InputMethodManager.
            telemetry_->softKeyboardToggleRequested.store(true, std::memory_order_release);
        }
        keyWasHeld_ = keyHeld;
        keyHighlighted_ = keyHeld;
        // No requestExitAfterFrame(): unlike a headless sample with a target frame count, an activity
        // runs until Android tears it down. Exiting on a frame budget here would make the app close
        // itself for no reason a user could see.
        return Core::success();
    }

    Core::Status updateUI(UIUpdateContext& context) override
    {
        // The window died mid-gesture, so no Up or Cancel will ever arrive for the held pointer.
        // Unwinding here keeps press/release balanced and stops the panel staying drawn as pressed.
        if (telemetry_->gestureStreamLost.exchange(false, std::memory_order_acq_rel) &&
            heldPointer_.has_value())
        {
            heldPointer_.reset();
            telemetry_->pointerReleases.fetch_add(1, std::memory_order_relaxed);
            pressed_ = false;
        }

        if (!context.hasPrimaryWindowUI())
        {
            // Valid, not an error: a configuration without a primary-window UI still runs every other
            // phase, and failing here would make UI mandatory for the whole engine.
            return Core::success();
        }

        auto tree = context.primaryWindowUITreeUpdater(root_);
        if (!tree)
        {
            return Core::failure(std::move(tree.error()));
        }

        // Focused here rather than at enter time, because focus is a committed-tree property: at
        // onEnter the TextEdit has been created but nothing has been committed yet, so
        // isCommittedKeyboardFocusCandidate is false and requestFocus is rejected. Attempted once and
        // then latched -- retrying every frame would clobber a focus the user moved with Tab.
        if (!textEditFocusAttempted_ && textEdit_.hasValue())
        {
            textEditFocusAttempted_ = true;
            // Failure is not fatal: the tree may legitimately not be committed yet on the very first
            // frame, and the next paragraph re-reads the real focus anyway. Latching regardless keeps
            // this from becoming a per-frame retry.
            static_cast<void>(tree->requestFocus(textEdit_));
        }

        // Read back what the UI actually holds, rather than trusting that routing worked.
        if (textEdit_.hasValue())
        {
            if (auto text = tree->text(textEdit_); text)
            {
                const auto codepoints = Core::countStrictUtf8CodepointsWithoutNul(*text);
                telemetry_->textEditCodepoints.store(codepoints.value_or(0), std::memory_order_relaxed);
            }
        }
        // Whether a composing region is being painted. The platform's stage counters cannot show this:
        // a stage routed while the TextEdit is unfocused is dropped, and the counters still climb.
        if (auto preedit = context.imeCompositionActive(); preedit)
        {
            telemetry_->uiPreeditActive.store(*preedit, std::memory_order_relaxed);
        }

        // Touch wins over the animation while held, so a tap produces an unmistakable, immediate change
        // rather than something that has to be caught mid-pulse.
        //
        // The animation itself stays because it distinguishes a live engine from a frozen one: a static
        // panel looks identical whether frames advance or not, which is exactly the failure mode that
        // cost two debugging rounds this week.
        const u64 frames = telemetry_->frameUpdates.load(std::memory_order_relaxed);
        // A held key highlights the same way a touch does, so keyboard and D-pad produce the same
        // visible result as a finger -- one state drives the presentation rather than two paths.
        const bool pulseOn = pressed_ || keyHighlighted_ || (frames / PulsePeriodFrames) % 2 == 0;
        telemetry_->uiPulseOn.store(pulseOn, std::memory_order_relaxed);
        UI::UIBoxPaint pulsePaint{};
        pulsePaint.solidFill = UI::UISolidFill{.color = pulseOn ? PulseFill : PanelFill};
        if (auto status = tree->setBoxPaint(pulse_, pulsePaint); !status)
        {
            return status;
        }

        // Pressing also grows the child, so a tap is visible as a shape change and not only a colour
        // change -- a colour-only cue is hard to tell apart from the animation in a screenshot.
        UI::UILayoutStyle pulseStyle{};
        const float extent = (pressed_ || keyHighlighted_) ? PressedPulsePercent : IdlePulsePercent;
        pulseStyle.size.width = UI::UILayoutLength::Percent(extent);
        pulseStyle.size.height = UI::UILayoutLength::Percent(extent);
        if (auto status = tree->setLayoutStyle(pulse_, pulseStyle); !status)
        {
            return status;
        }

        telemetry_->uiUpdates.fetch_add(1, std::memory_order_relaxed);
        return Core::success();
    }

  private:
    // Slow enough to survive a screenshot taken at an arbitrary moment: at ~60fps this is roughly a
    // second per state, so a capture reliably lands inside one phase rather than mid-transition.
    static constexpr u64 PulsePeriodFrames = 60;
    // Far enough apart that a pixel count tells them apart without ambiguity.
    static constexpr float IdlePulsePercent = 50.0F;
    static constexpr float PressedPulsePercent = 90.0F;

    Core::Status buildContent(PrimaryWindowUITreeUpdater& tree)
    {
        UI::UILayoutStyle panelStyle{};
        panelStyle.size.width = UI::UILayoutLength::Percent(60.0F);
        panelStyle.size.height = UI::UILayoutLength::Percent(30.0F);

        auto panel = tree.createElement(root_.rootNodeId(), UI::makePanelElement(panelStyle));
        if (!panel)
        {
            return Core::failure(std::move(panel.error()));
        }
        panel_ = *panel;

        UI::UIBoxPaint panelPaint{};
        panelPaint.solidFill = UI::UISolidFill{.color = PanelFill};
        if (auto status = tree.setBoxPaint(panel_, panelPaint); !status)
        {
            return status;
        }
        // Explicit, because a bare Panel defaults to Ignore: hit policy is only auto-promoted for
        // elements carrying a standard behavior, and this panel carries none. Without it the panel is
        // invisible to routing and every touch lands on the root instead.
        if (auto status = tree.setPointerHitPolicy(panel_, UI::UIPointerHitPolicy::Targetable); !status)
        {
            return status;
        }

        UI::UILayoutStyle pulseStyle{};
        pulseStyle.size.width = UI::UILayoutLength::Percent(IdlePulsePercent);
        pulseStyle.size.height = UI::UILayoutLength::Percent(IdlePulsePercent);
        auto pulse = tree.createElement(panel_, UI::makePanelElement(pulseStyle));
        if (!pulse)
        {
            return Core::failure(std::move(pulse.error()));
        }
        pulse_ = *pulse;
        // Ignore rather than Targetable: the child must not steal the touch from its parent, which is
        // the same rule the on-screen thumbstick follows for its knob.
        if (auto status = tree.setPointerHitPolicy(pulse_, UI::UIPointerHitPolicy::Ignore); !status)
        {
            return status;
        }

        // A TextEdit, because without a focused one the composition half of the input chain is
        // unobservable: routeTextComposition takes its "no text focus" branch, returns unconsumed, and
        // the platform's stage counters climb while nothing is ever drawn. It also has to exist for the
        // caret placement path to run at all -- the Runtime only publishes a caret when a TextEdit is
        // focused, which is exactly the path that used to stop the frame loop.
        UI::UILayoutStyle editStyle{};
        editStyle.size.width = UI::UILayoutLength::Percent(80.0F);
        editStyle.size.height = UI::UILayoutLength::Px(48.0F);
        auto textEdit = tree.createElement(root_.rootNodeId(), UI::makeTextEditElement({}, editStyle));
        if (!textEdit)
        {
            return Core::failure(std::move(textEdit.error()));
        }
        textEdit_ = *textEdit;
        return registerPointerListeners(tree);
    }

    // Touch changes what is on screen, which is what closes the loop end to end: finger -> Java ->
    // JNI -> ring buffer -> PlatformFrame -> UI routing -> paint. Counting alone would not prove the
    // routing half, since a counter can advance while nothing is actually hit.
    Core::Status registerPointerListeners(PrimaryWindowUITreeUpdater& tree)
    {
        const auto add = [&](usize slot, UI::UIRoutedPointerEventKind kind,
                             UI::UIRoutedPointerCallback callback) -> Core::Status {
            auto listener = tree.addRoutedPointerListener(
                {
                    .node = panel_,
                    .kind = kind,
                    .phases = UI::UIEventPhaseMask::Target,
                },
                std::move(callback));
            if (!listener)
            {
                return Core::failure(std::move(listener.error()));
            }
            pointerListeners_[slot] = std::move(*listener);
            return Core::success();
        };

        if (auto status = add(0, UI::UIRoutedPointerEventKind::ButtonDown,
                              UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                                  onPointerDown(event);
                              }});
            !status)
        {
            return status;
        }
        if (auto status = add(1, UI::UIRoutedPointerEventKind::ButtonUp,
                              UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                                  onPointerRelease(event);
                              }});
            !status)
        {
            return status;
        }
        // Cancel is handled as well as Up, because a gesture taken away by the OS never delivers an Up.
        // Without this the panel would stay latched as pressed after a task switch mid-touch.
        return add(2, UI::UIRoutedPointerEventKind::PointerCancel,
                   UI::UIRoutedPointerCallback{
                       [this](UI::UIRoutedPointerEvent& event) noexcept { onPointerRelease(event); }});
    }

    void onPointerDown(UI::UIRoutedPointerEvent& event) noexcept
    {
        // Only the first finger latches the visual. A second one arriving while the first is held must
        // not re-trigger it -- that is the multi-touch defect ADR 0032 cites from cocos2d-x, where a
        // second pointer stole the control the first was holding.
        if (!heldPointer_.has_value())
        {
            heldPointer_ = event.input().pointer;
            telemetry_->pointerPresses.fetch_add(1, std::memory_order_relaxed);
            pressed_ = true;
        }
        // Capture keeps Up and Cancel arriving here even if the finger leaves the panel, which is
        // exactly the case where a naive implementation never releases.
        event.capturePointer();
        static_cast<void>(event.claimPointerButton(event.input().button));
    }

    void onPointerRelease(UI::UIRoutedPointerEvent& event) noexcept
    {
        // Scoped to the pointer that latched it: another finger lifting must not release this one's
        // hold, which is the per-pointer cancel rule the platform layer already enforces.
        if (heldPointer_.has_value() && *heldPointer_ == event.input().pointer)
        {
            heldPointer_.reset();
            telemetry_->pointerReleases.fetch_add(1, std::memory_order_relaxed);
            pressed_ = false;
        }
    }

    AndroidGameTelemetry* telemetry_;
    // Held for the state's lifetime because the loaded payload is borrowed, not copied: a
    // Texture2DPayloadView's level spans point into the CookedAssetFile this system owns. Optional
    // because a build with no shipped content is legitimate.
    std::optional<Asset::AssetSystem> content_;
    UI::UIRootOwner root_{};
    UI::UINodeId panel_{};
    UI::UINodeId pulse_{};
    // Down, Up and Cancel. Held as tokens because dropping one unregisters that listener.
    std::array<UI::UIRoutedPointerListenerToken, 3> pointerListeners_{};
    UI::UINodeId textEdit_{};
    // Which pointer latched the press, so a second finger cannot re-trigger it and a third finger
    // lifting cannot release it.
    std::optional<Platform::PointerId> heldPointer_{};
    bool pressed_ = false;
    // Focus is requested once, on the first updateUI. Latched rather than retried per frame so it
    // cannot clobber a focus the user moved with Tab.
    bool textEditFocusAttempted_ = false;
    // Edge-detected so the counter reflects presses rather than frames held.
    bool keyWasHeld_ = false;
    bool keyHighlighted_ = false;
};

class AndroidGameApplication final : public IGameApplication {
  public:
    explicit AndroidGameApplication(AndroidGameTelemetry& telemetry) noexcept : telemetry_(&telemetry) {}

    Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext& context) override
    {
        // Here rather than in the constructor, because this is the first point where the engine hands
        // back the config holding the content root -- and the first hook allowed to fail at all.
        auto content = openShippedContent(context.engineConfig().contentRoot, *telemetry_);
        return std::unique_ptr<IGameState>{
            std::make_unique<AndroidGameState>(*telemetry_, std::move(content))};
    }

    void onShutdown(GameShutdownContext&) noexcept override
    {
        telemetry_->applicationShutdown.store(true, std::memory_order_release);
    }

  private:
    AndroidGameTelemetry* telemetry_;
};

} // namespace

std::unique_ptr<IGameApplication> createAndroidGameApplication(AndroidGameTelemetry& telemetry) noexcept
{
    return std::make_unique<AndroidGameApplication>(telemetry);
}

} // namespace Tina::Platform::Android
