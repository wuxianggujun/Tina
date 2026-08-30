#include "GalleryActions.hpp"
#include "GalleryScene.hpp"

#include <tina/runtime/PhaseContexts.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIHitTest.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace Tina::Gallery {
namespace {

// Row heights in logical pixels. A phone at 3x density turns 72 into 216 physical pixels, which is well
// past the ~48dp minimum touch target -- the menu has to be tappable with a thumb, not just clickable.
constexpr float RowHeight = 72.0F;
constexpr float RowGap = 8.0F;

constexpr UI::UIStraightSrgba8Color MenuBackground{.red = 24, .green = 28, .blue = 38, .alpha = 255};
constexpr UI::UIStraightSrgba8Color RowFill{.red = 44, .green = 52, .blue = 68, .alpha = 255};
constexpr UI::UIStraightSrgba8Color RowSelectedFill{.red = 62, .green = 126, .blue = 186, .alpha = 255};

// The menu. Stays on the stack while a scene runs, so returning is a pop rather than a rebuild.
class GalleryMenuState final : public IGameState {
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
        return buildMenu(*tree);
    }

    void onExit(GameStateExitContext&) noexcept override
    {
        // Listeners first: they hold nodes from this root, so outliving it would leave registrations
        // pointing at a tree that no longer exists.
        for (auto& listener : rowListeners_)
        {
            listener = {};
        }
        root_.reset();
    }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        // Nothing blocked: the menu is the bottom of the stack, so every "blocks below" flag would
        // describe a layer that does not exist. A pushed scene declares its own policy.
        return GameStatePolicy{};
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        const std::span<const GalleryEntry> entries = galleryEntries();
        if (entries.empty())
        {
            return Core::success();
        }

        // Edge-detected, because frameActions reports held state: without this, holding Down would move
        // the selection at frame rate and shoot past every entry.
        const bool downHeld = context.frameActions().isActive(GalleryDownAction);
        const bool upHeld = context.frameActions().isActive(GalleryUpAction);
        const bool activateHeld = context.frameActions().isActive(GalleryActivateAction);

        // Back at the menu means leaving the app, and the menu is the only state that can do it: the
        // Android host claims BACK unconditionally (it cannot know whether the engine will use it), so
        // nothing else will close the activity. Without this the user is stuck in the gallery.
        const bool backHeld = context.frameActions().isActive(GalleryBackAction);
        if (backHeld && !backWasHeld_)
        {
            context.requestExitAfterFrame();
        }
        backWasHeld_ = backHeld;

        if (downHeld && !downWasHeld_)
        {
            selected_ = (selected_ + 1) % entries.size();
            selectionDirty_ = true;
        }
        if (upHeld && !upWasHeld_)
        {
            selected_ = selected_ == 0 ? entries.size() - 1 : selected_ - 1;
            selectionDirty_ = true;
        }
        downWasHeld_ = downHeld;
        upWasHeld_ = upHeld;

        // A pointer tap sets pendingActivation_ from the listener; a key press sets it here. Both go
        // through one path so a scene cannot be pushed twice in one frame by tapping and pressing Enter.
        if (activateHeld && !activateWasHeld_)
        {
            pendingActivation_ = selected_;
        }
        activateWasHeld_ = activateHeld;

        if (pendingActivation_.has_value())
        {
            const usize index = *pendingActivation_;
            pendingActivation_.reset();
            if (index < entries.size() && entries[index].create != nullptr)
            {
                auto scene = entries[index].create();
                if (!scene)
                {
                    return Core::failure(std::move(scene.error()));
                }
                // requestPush is deferred and committed by EngineHost after updateFrame (ADR 0014), so
                // this frame finishes on the menu and the scene's onEnter runs before the next one.
                if (Core::Status pushed = context.requestPush(std::move(*scene)); !pushed)
                {
                    return pushed;
                }
            }
        }
        return Core::success();
    }

    Core::Status updateUI(UIUpdateContext& context) override
    {
        if (!selectionDirty_ || !context.hasPrimaryWindowUI())
        {
            // Only repaint when the selection actually moved. Setting the paint every frame would dirty
            // the tree on idle frames, which is the per-frame rebuild trap the editor already pays for.
            return Core::success();
        }

        auto tree = context.primaryWindowUITreeUpdater(root_);
        if (!tree)
        {
            return Core::failure(std::move(tree.error()));
        }
        selectionDirty_ = false;
        return applySelectionPaint(*tree);
    }

  private:
    // Fixed, because the listener tokens and node ids are stored inline: a growing menu would need a
    // heap vector on a path that runs during UI authoring. Raising it is a one-line change.
    static constexpr usize MaximumRows = 16;

    Core::Status buildMenu(PrimaryWindowUITreeUpdater& tree)
    {
        UI::UILayoutStyle rootStyle{};
        // The root must fill the window, or percentage-sized children resolve against an auto-sized
        // parent and collapse to nothing -- visible only as "the menu never appears".
        rootStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        rootStyle.size.height = UI::UILayoutLength::Percent(100.0F);
        rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        rootStyle.flexContainer.gap = UI::UILayoutGap::All(RowGap);
        rootStyle.padding = UI::UIEdgeSpacing::All(16.0F);
        if (Core::Status status = tree.setLayoutStyle(root_.rootNodeId(), rootStyle); !status)
        {
            return status;
        }
        UI::UIBoxPaint background{};
        background.solidFill = UI::UISolidFill{.color = MenuBackground};
        if (Core::Status status = tree.setBoxPaint(root_.rootNodeId(), background); !status)
        {
            return status;
        }

        const std::span<const GalleryEntry> entries = galleryEntries();
        rowCount_ = entries.size() < MaximumRows ? entries.size() : MaximumRows;
        for (usize index = 0; index < rowCount_; ++index)
        {
            if (Core::Status status = buildRow(tree, index, entries[index]); !status)
            {
                return status;
            }
        }
        return applySelectionPaint(tree);
    }

    Core::Status buildRow(PrimaryWindowUITreeUpdater& tree, usize index, const GalleryEntry& entry)
    {
        UI::UILayoutStyle rowStyle{};
        rowStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        rowStyle.size.height = UI::UILayoutLength::Px(RowHeight);
        rowStyle.padding = UI::UIEdgeSpacing::All(12.0F);

        // A Label, not a Button, and the difference is load-bearing. A Button carries the Activatable
        // behaviour, so the UI claims Enter as its default action and suppresses the gameplay action --
        // correct behaviour, but it meant the menu's own Activate action never fired and Enter fell
        // through to the menu's Back handling, which exits the app. Measured on a device: pressing Enter
        // closed the gallery instead of opening a scene.
        //
        // So the row is inert and the menu owns both paths itself: its own action for keys, its own
        // pointer listener for taps. Targetable is required explicitly because a Label defaults to
        // Ignore and is only auto-promoted when it carries a standard behaviour.
        auto row = tree.createElement(root_.rootNodeId(), UI::makeLabelElement(entry.title, rowStyle));
        if (!row)
        {
            return Core::failure(std::move(row.error()));
        }
        rows_[index] = *row;
        if (Core::Status status = tree.setPointerHitPolicy(rows_[index], UI::UIPointerHitPolicy::Targetable);
            !status)
        {
            return status;
        }

        auto listener = tree.addRoutedPointerListener(
            {
                .node = rows_[index],
                .kind = UI::UIRoutedPointerEventKind::ButtonUp,
                .phases = UI::UIEventPhaseMask::Target,
            },
            UI::UIRoutedPointerCallback{[this, index](UI::UIRoutedPointerEvent&) noexcept {
                // ButtonUp, not ButtonDown: a tap should be cancellable by dragging off the row, which is
                // what every platform's button convention does. Recorded rather than acted on, because
                // pushing a state from a pointer callback would mutate the stack mid-route.
                selected_ = index;
                selectionDirty_ = true;
                pendingActivation_ = index;
            }});
        if (!listener)
        {
            return Core::failure(std::move(listener.error()));
        }
        rowListeners_[index] = std::move(*listener);

        // Summary as a child Label so the row shows both lines. Ignore hit policy: it must not steal the
        // tap from its parent row, the same rule the on-screen thumbstick's knob follows.
        UI::UILayoutStyle summaryStyle{};
        summaryStyle.size.width = UI::UILayoutLength::Percent(100.0F);
        auto summary = tree.createElement(rows_[index], UI::makeLabelElement(entry.summary, summaryStyle));
        if (!summary)
        {
            return Core::failure(std::move(summary.error()));
        }
        return tree.setPointerHitPolicy(*summary, UI::UIPointerHitPolicy::Ignore);
    }

    Core::Status applySelectionPaint(PrimaryWindowUITreeUpdater& tree)
    {
        for (usize index = 0; index < rowCount_; ++index)
        {
            if (!rows_[index].hasValue())
            {
                continue;
            }
            UI::UIBoxPaint paint{};
            paint.solidFill =
                UI::UISolidFill{.color = index == selected_ ? RowSelectedFill : RowFill};
            if (Core::Status status = tree.setBoxPaint(rows_[index], paint); !status)
            {
                return status;
            }
        }
        return Core::success();
    }

    UI::UIRootOwner root_{};
    std::array<UI::UINodeId, MaximumRows> rows_{};
    std::array<UI::UIRoutedPointerListenerToken, MaximumRows> rowListeners_{};
    usize rowCount_ = 0;
    usize selected_ = 0;
    // Set by a tap or an Enter press, consumed once in updateFrame. Deferred because requestPush is only
    // available there, and because it de-duplicates a tap and a key press landing in the same frame.
    std::optional<usize> pendingActivation_{};
    // True until the selection highlight has been repainted. Starts true so the first frame paints it.
    bool selectionDirty_ = true;
    bool downWasHeld_ = false;
    bool upWasHeld_ = false;
    bool activateWasHeld_ = false;
    // Starts true so the press that popped a scene cannot immediately exit the app: the menu resumes on
    // the frame that press is still held, and an unlatched read would see it as a fresh press.
    bool backWasHeld_ = true;
};

class GalleryApplication final : public IGameApplication {
  public:
    Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext&) override
    {
        return std::unique_ptr<IGameState>{std::make_unique<GalleryMenuState>()};
    }

    void onShutdown(GameShutdownContext&) noexcept override
    {
        // Nothing to unwind: the menu and every scene release their own roots in onExit, which the state
        // stack has already run by the time shutdown reaches here.
    }
};

} // namespace

std::unique_ptr<IGameApplication> createGalleryApplication() noexcept
{
    return std::make_unique<GalleryApplication>();
}

} // namespace Tina::Gallery
