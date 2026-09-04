#include "GalleryActions.hpp"
#include "GalleryScene.hpp"

#include <tina/runtime/PhaseContexts.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIVirtualStick.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace Tina::Gallery {
namespace {

constexpr UI::UIStraightSrgba8Color SceneBackground{.red = 18, .green = 22, .blue = 30, .alpha = 255};
constexpr UI::UIStraightSrgba8Color AccentFill{.red = 224, .green = 138, .blue = 54, .alpha = 255};
constexpr UI::UIStraightSrgba8Color CalmFill{.red = 72, .green = 160, .blue = 132, .alpha = 255};

// What every scene shares: a root that fills the window, and a way back to the menu.
//
// A base class rather than a helper function because the root and the back handling are *state* -- the
// root must be released in onExit, and the back action must be edge-detected across frames. Repeating
// both in each scene is how one of them ends up subtly different, and a missed release leaves nodes
// pointing at a destroyed tree.
class GallerySceneBase : public IGameState {
  public:
    Core::Status onEnter(GameStateEnterContext& context) override
    {
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

        UI::UILayoutStyle rootStyle{};
        rootStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        rootStyle.size.height = UI::UILayoutLength::Percent(100.0F);
        if (Core::Status status = tree->setLayoutStyle(root_.rootNodeId(), rootStyle); !status)
        {
            return status;
        }
        UI::UIBoxPaint background{};
        background.solidFill = UI::UISolidFill{.color = SceneBackground};
        if (Core::Status status = tree->setBoxPaint(root_.rootNodeId(), background); !status)
        {
            return status;
        }
        return buildScene(*tree);
    }

    void onExit(GameStateExitContext&) noexcept override
    {
        releaseScene();
        root_.reset();
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        // A scene covers the menu entirely, so the menu must stop updating and drawing underneath it.
        // Without blocksRenderBelow the menu's rows paint through the scene; without the update flags it
        // keeps consuming the very Up/Down actions the scene may want.
        return GameStatePolicy{
            .blocksGameplayInputBelow = true,
            .blocksUIUpdateBelow = true,
            .blocksFixedUpdateBelow = true,
            .blocksFrameUpdateBelow = true,
            .blocksRenderBelow = true,
        };
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        // Edge-detected, and this is not optional: frameActions reports held state, so a back press that
        // is still held on the frame the menu resumes would immediately pop the menu too.
        const bool backHeld = context.frameActions().isActive(GalleryBackAction);
        const bool backPressed = backHeld && !backWasHeld_;
        backWasHeld_ = backHeld;
        if (backPressed)
        {
            return context.requestPop();
        }
        return updateScene(context);
    }

  protected:
    [[nodiscard]] UI::UINodeId rootNode() const noexcept { return root_.rootNodeId(); }

    // Builds the scene's own content under the root. The root already fills the window and is painted.
    virtual Core::Status buildScene(PrimaryWindowUITreeUpdater& tree) = 0;
    // Drops anything holding a node from this root, before the root itself goes.
    virtual void releaseScene() noexcept {}
    // Per-frame scene logic. Back handling already ran.
    virtual Core::Status updateScene(FrameUpdateContext&) { return Core::success(); }

    UI::UIRootOwner root_{};

  private:
    bool backWasHeld_ = false;
};

// --- Scene: on-screen thumbstick ---

// The scene worth showing first on a phone, because it is the only one whose whole point is touch.
//
// UIVirtualStick is pure logic, not an engine-created widget: the scene owns the nodes and drives the
// state from its own pointer listeners. That is deliberate in the engine's design -- a custom drag
// control needs no engine change -- and it is why this scene has three listeners rather than one factory
// call.
class VirtualStickScene final : public GallerySceneBase {
  public:
    Core::Status buildScene(PrimaryWindowUITreeUpdater& tree) override
    {
        baseRect_ = UI::UILogicalRect{
            .x = StickMargin,
            .y = StickMargin,
            .width = BaseRadius * 2.0F,
            .height = BaseRadius * 2.0F,
        };

        // The base. Hit policy must be set explicitly: a bare Panel defaults to Ignore and is only
        // auto-promoted when it carries a standard behaviour, which a custom drag control does not --
        // without this every touch falls through to the root and the stick never engages.
        auto base = tree.createElement(rootNode(), UI::makePanelElement());
        if (!base)
        {
            return Core::failure(std::move(base.error()));
        }
        baseNode_ = *base;
        if (Core::Status status = tree.setLayoutStyle(baseNode_, overlayStyle(baseRect_)); !status)
        {
            return status;
        }
        UI::UIBoxPaint basePaint{};
        basePaint.solidFill = UI::UISolidFill{.color = StickBaseFill};
        if (Core::Status status = tree.setBoxPaint(baseNode_, basePaint); !status)
        {
            return status;
        }
        if (Core::Status status = tree.setPointerHitPolicy(baseNode_, UI::UIPointerHitPolicy::Targetable);
            !status)
        {
            return status;
        }

        // The knob is a *sibling* of the base, not a child, and Ignore rather than Targetable. As a child
        // it would move with the base's own coordinate space; as Targetable it would take the touch away
        // from the base mid-drag.
        auto knob = tree.createElement(rootNode(), UI::makePanelElement());
        if (!knob)
        {
            return Core::failure(std::move(knob.error()));
        }
        knobNode_ = *knob;
        if (Core::Status status = tree.setPointerHitPolicy(knobNode_, UI::UIPointerHitPolicy::Ignore);
            !status)
        {
            return status;
        }
        UI::UIBoxPaint knobPaint{};
        knobPaint.solidFill = UI::UISolidFill{.color = AccentFill};
        if (Core::Status status = tree.setBoxPaint(knobNode_, knobPaint); !status)
        {
            return status;
        }
        if (Core::Status status = applyKnobLayout(tree); !status)
        {
            return status;
        }
        return registerListeners(tree);
    }

    Core::Status updateUI(UIUpdateContext& context) override
    {
        if (!context.hasPrimaryWindowUI())
        {
            return Core::success();
        }
        auto tree = context.primaryWindowUITreeUpdater(root_);
        if (!tree)
        {
            return Core::failure(std::move(tree.error()));
        }
        return applyKnobLayout(*tree);
    }

    void releaseScene() noexcept override
    {
        for (auto& listener : listeners_)
        {
            listener = {};
        }
    }

  private:
    static constexpr float StickMargin = 48.0F;
    static constexpr float BaseRadius = 110.0F;
    static constexpr float KnobRadius = 42.0F;

    static constexpr UI::UIStraightSrgba8Color StickBaseFill{
        .red = 48, .green = 56, .blue = 74, .alpha = 255};

    [[nodiscard]] static UI::UILayoutStyle overlayStyle(const UI::UILogicalRect& rect) noexcept
    {
        UI::UILayoutStyle style{};
        style.placement = UI::UILayoutPlacement::Overlay;
        style.overlay.offset.x = UI::UILayoutLength::Px(rect.x);
        style.overlay.offset.y = UI::UILayoutLength::Px(rect.y);
        style.size.width = UI::UILayoutLength::Px(rect.width);
        style.size.height = UI::UILayoutLength::Px(rect.height);
        return style;
    }

    Core::Status applyKnobLayout(PrimaryWindowUITreeUpdater& tree)
    {
        // knobOffset rather than (x, y): the state already carries the offset clamped to the travel
        // radius, and recomputing it from the normalized value would round-trip through a division that
        // loses the clamp -- the knob would drift outside the base at full deflection.
        const UI::UILogicalPoint centre = UI::virtualStickCenter(baseRect_);
        const float knobX = centre.x + stick_.knobOffset.x - KnobRadius;
        const float knobY = centre.y + stick_.knobOffset.y - KnobRadius;
        return tree.setLayoutStyle(knobNode_, overlayStyle(UI::UILogicalRect{
                                                  .x = knobX,
                                                  .y = knobY,
                                                  .width = KnobRadius * 2.0F,
                                                  .height = KnobRadius * 2.0F,
                                              }));
    }

    Core::Status registerListeners(PrimaryWindowUITreeUpdater& tree)
    {
        const auto add = [&](usize slot, UI::UIRoutedPointerEventKind kind,
                             UI::UIRoutedPointerCallback callback) -> Core::Status {
            auto listener = tree.addRoutedPointerListener(
                {
                    .node = baseNode_,
                    .kind = kind,
                    .phases = UI::UIEventPhaseMask::Target,
                },
                std::move(callback));
            if (!listener)
            {
                return Core::failure(std::move(listener.error()));
            }
            listeners_[slot] = std::move(*listener);
            return Core::success();
        };

        if (Core::Status status = add(0, UI::UIRoutedPointerEventKind::ButtonDown,
                                      UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                                          onDown(event);
                                      }});
            !status)
        {
            return status;
        }
        if (Core::Status status = add(1, UI::UIRoutedPointerEventKind::Move,
                                      UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                                          onMove(event);
                                      }});
            !status)
        {
            return status;
        }
        if (Core::Status status = add(2, UI::UIRoutedPointerEventKind::ButtonUp,
                                      UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                                          onRelease(event);
                                      }});
            !status)
        {
            return status;
        }
        // Cancel as well as Up: a gesture taken away by the OS never delivers an Up, and without this the
        // knob would stay deflected after a task switch mid-drag.
        return add(3, UI::UIRoutedPointerEventKind::PointerCancel,
                   UI::UIRoutedPointerCallback{
                       [this](UI::UIRoutedPointerEvent& event) noexcept { onRelease(event); }});
    }

    void onDown(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (input.button != Platform::PointerButton::Primary)
        {
            return;
        }
        // pressVirtualStick enforces both the circular hit test and the single-owner rule, so a press in
        // the box but outside the disc, and a second finger on an engaged stick, are both refused here
        // rather than needing a check at this call site.
        if (!UI::pressVirtualStick(stick_, config_, UI::virtualStickCenter(baseRect_), input.pointer,
                                   input.position))
        {
            return;
        }
        // Capture keeps Move arriving after the finger leaves the ring -- exactly the case where a naive
        // implementation freezes the knob.
        event.capturePointer();
        static_cast<void>(event.claimPointerButton(input.button));
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void onMove(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (!UI::dragVirtualStick(stick_, config_, UI::virtualStickCenter(baseRect_), input.pointer,
                                  input.position))
        {
            return;
        }
        static_cast<void>(event.claimPointerButton(Platform::PointerButton::Primary));
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void onRelease(UI::UIRoutedPointerEvent& event) noexcept
    {
        if (!UI::releaseVirtualStick(stick_, event.input().pointer))
        {
            return;
        }
        event.releasePointerCapture();
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    UI::UIVirtualStickConfig config_{};
    UI::UIVirtualStickState stick_{};
    UI::UILogicalRect baseRect_{};
    UI::UINodeId baseNode_{};
    UI::UINodeId knobNode_{};
    // Down, Move, Up and Cancel. Held as tokens because dropping one unregisters that listener.
    std::array<UI::UIRoutedPointerListenerToken, 4> listeners_{};
};

// --- Scene: UI controls ---

// Buttons, a checkbox and a slider, so the gallery shows the retained UI doing ordinary widget work
// rather than only hand-painted panels.
class UIControlsScene final : public GallerySceneBase {
  public:
    Core::Status buildScene(PrimaryWindowUITreeUpdater& tree) override
    {
        UI::UILayoutStyle columnStyle{};
        columnStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        columnStyle.size.height = UI::UILayoutLength::Percent(100.0F);
        columnStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        columnStyle.flexContainer.gap = UI::UILayoutGap::All(16.0F);
        columnStyle.padding = UI::UIEdgeSpacing::All(24.0F);
        if (Core::Status status = tree.setLayoutStyle(rootNode(), columnStyle); !status)
        {
            return status;
        }

        UI::UILayoutStyle rowStyle{};
        rowStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        rowStyle.size.height = UI::UILayoutLength::Px(64.0F);

        auto button = tree.createElement(rootNode(), UI::makeButtonElement("Tap me", rowStyle));
        if (!button)
        {
            return Core::failure(std::move(button.error()));
        }
        // No label argument: makeCheckboxElement takes only a layout, because a checkbox's caption is a
        // sibling Label in this UI rather than text the control owns.
        auto checkbox = tree.createElement(rootNode(), UI::makeCheckboxElement(rowStyle));
        if (!checkbox)
        {
            return Core::failure(std::move(checkbox.error()));
        }
        auto slider = tree.createElement(rootNode(), UI::makeSliderElement(rowStyle));
        if (!slider)
        {
            return Core::failure(std::move(slider.error()));
        }
        return Core::success();
    }
};

// --- Scene: engine liveness ---

// The simplest scene, kept because it is the one that answers "is the engine running at all" without any
// content to load. A static panel looks identical whether frames advance or not, so it animates.
class PulseScene final : public GallerySceneBase {
  public:
    Core::Status buildScene(PrimaryWindowUITreeUpdater& tree) override
    {
        UI::UILayoutStyle panelStyle{};
        panelStyle.size.width = UI::UILayoutLength::Percent(60.0F);
        panelStyle.size.height = UI::UILayoutLength::Percent(30.0F);
        auto panel = tree.createElement(rootNode(), UI::makePanelElement(panelStyle));
        if (!panel)
        {
            return Core::failure(std::move(panel.error()));
        }
        panel_ = *panel;
        return Core::success();
    }

    Core::Status updateUI(UIUpdateContext& context) override
    {
        if (!context.hasPrimaryWindowUI())
        {
            return Core::success();
        }
        ++frames_;
        // Roughly a second per phase at 60fps, so a screenshot reliably lands inside one phase rather
        // than mid-transition.
        const bool on = (frames_ / 60) % 2 == 0;
        if (on == lastOn_)
        {
            // Only repaint on a change. Setting the paint every frame would dirty the tree on idle
            // frames, which is exactly the per-frame rebuild trap to avoid.
            return Core::success();
        }
        lastOn_ = on;

        auto tree = context.primaryWindowUITreeUpdater(root_);
        if (!tree)
        {
            return Core::failure(std::move(tree.error()));
        }
        UI::UIBoxPaint paint{};
        paint.solidFill = UI::UISolidFill{.color = on ? CalmFill : AccentFill};
        return tree->setBoxPaint(panel_, paint);
    }

  private:
    UI::UINodeId panel_{};
    u64 frames_ = 0;
    bool lastOn_ = false;
};

template <typename Scene>
[[nodiscard]] Core::Result<std::unique_ptr<IGameState>> makeScene()
{
    return std::unique_ptr<IGameState>{std::make_unique<Scene>()};
}

constexpr std::array<GalleryEntry, 3> Entries{{
    {
        .title = "On-screen thumbstick",
        .summary = "Drag to steer. A second finger cannot steal it.",
        .create = &makeScene<VirtualStickScene>,
    },
    {
        .title = "UI controls",
        .summary = "Button, checkbox and slider from the retained UI.",
        .create = &makeScene<UIControlsScene>,
    },
    {
        .title = "Engine pulse",
        .summary = "Animated panel: proves frames are advancing.",
        .create = &makeScene<PulseScene>,
    },
}};

} // namespace

std::span<const GalleryEntry> galleryEntries() noexcept
{
    return Entries;
}

} // namespace Tina::Gallery
