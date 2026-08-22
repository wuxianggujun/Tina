#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window, UI::UIContextCapacityConfig capacities = {})
{
    capacities.nodeCapacity =
        capacities.nodeCapacity == UI::UIContextCapacityConfig::DefaultNodeCapacity
            ? 48
            : capacities.nodeCapacity;
    capacities.rootCapacity =
        capacities.rootCapacity == UI::UIContextCapacityConfig::DefaultRootCapacity
            ? 2
            : capacities.rootCapacity;
    capacities.layoutSnapshotCapacity =
        capacities.layoutSnapshotCapacity == 0 ? capacities.nodeCapacity
                                               : capacities.layoutSnapshotCapacity;
    capacities.hitSnapshotCapacity =
        capacities.hitSnapshotCapacity == 0 ? capacities.nodeCapacity
                                            : capacities.hitSnapshotCapacity;
    capacities.paintSnapshotCapacity =
        capacities.paintSnapshotCapacity == 0 ? capacities.nodeCapacity * 8U
                                              : capacities.paintSnapshotCapacity;
    capacities.textByteCapacity =
        capacities.textByteCapacity == 0 ? 2048 : capacities.textByteCapacity;
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UIDialogParts buildDialog(
    UI::UITreeUpdater& updater, UI::UINodeId parent,
    std::string_view title = "Dialog")
{
    const std::array actions{
        UI::UIDialogActionConfig{
            .text = "Close",
            .variant = UI::UIButtonVariant::Primary,
        },
    };
    auto result = updater.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = title,
            .actions = actions,
        });
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UIDialogParts{};
}

class UIDialogTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UIDialogTest, PresentationIntentIsIdempotentAndPublishesAtCommit)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    auto background = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Background"));
    ASSERT_TRUE(background.has_value()) << background.error().message;
    const UI::UIDialogParts dialog = buildDialog(updater, root.rootNodeId());

    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    assertOk(context->requestFocus(*background));
    EXPECT_FALSE(updater.isDialogOpen(dialog.modal).value());
    EXPECT_FALSE(context->activeModal().hasValue());

    assertOk(updater.openDialog(dialog.modal));
    assertOk(updater.openDialog(dialog.modal));
    EXPECT_TRUE(updater.isDialogOpen(dialog.modal).value());
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), *background);

    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    EXPECT_EQ(context->activeModal(), dialog.modal);
    EXPECT_EQ(context->activeFocusScope(), dialog.modal);
    EXPECT_EQ(context->defaultActionFocus(), dialog.actions[0]);

    assertOk(updater.dismissDialog(dialog.modal));
    assertOk(updater.dismissDialog(dialog.modal));
    EXPECT_FALSE(updater.isDialogOpen(dialog.modal).value());
    EXPECT_EQ(context->activeModal(), dialog.modal);
    EXPECT_EQ(context->defaultActionFocus(), dialog.actions[0]);

    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), *background);

    UI::UILayoutStyle hidden = fixedSize(100.0F, 80.0F);
    hidden.visibility = UI::UIVisibility::Collapsed;
    Core::Status rejected = updater.setLayoutStyle(dialog.modal, hidden);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
}

TEST_F(UIDialogTest, ValidationAndSingleDialogConflictPreserveIntent)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto firstUpdater = createUpdater(*context, firstRoot);
    auto secondUpdater = createUpdater(*context, secondRoot);
    const UI::UIDialogParts first =
        buildDialog(firstUpdater, firstRoot.rootNodeId(), "First");
    const UI::UIDialogParts second =
        buildDialog(firstUpdater, firstRoot.rootNodeId(), "Second");
    auto panel = firstUpdater.createElement(
        firstRoot.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;

    Core::Status nonDialog = firstUpdater.openDialog(*panel);
    ASSERT_FALSE(nonDialog.has_value());
    EXPECT_EQ(nonDialog.error().code, UI::UIErrorCode::InvalidControlValue);

    auto wrongRoot = secondUpdater.isDialogOpen(first.modal);
    ASSERT_FALSE(wrongRoot.has_value());
    EXPECT_EQ(wrongRoot.error().code, UI::UIErrorCode::InvalidNode);

    assertOk(firstUpdater.openDialog(first.modal));
    Core::Status conflict = firstUpdater.openDialog(second.modal);
    ASSERT_FALSE(conflict.has_value());
    EXPECT_EQ(conflict.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_TRUE(firstUpdater.isDialogOpen(first.modal).value());
    EXPECT_FALSE(firstUpdater.isDialogOpen(second.modal).value());
}

TEST_F(UIDialogTest, DestroyAndGenerationReuseClearRegisteredState)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UIDialogParts original =
        buildDialog(updater, root.rootNodeId(), "Original");
    assertOk(updater.openDialog(original.modal));
    assertOk(updater.destroy(original.modal));

    auto stale = context->isDialogOpen(original.modal);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, UI::UIErrorCode::InvalidNode);

    const UI::UIDialogParts replacement =
        buildDialog(updater, root.rootNodeId(), "Replacement");
    EXPECT_EQ(replacement.modal.index(), original.modal.index());
    EXPECT_NE(replacement.modal.generation(), original.modal.generation());
    EXPECT_FALSE(updater.isDialogOpen(replacement.modal).value());
    assertOk(updater.openDialog(replacement.modal));
    EXPECT_TRUE(updater.isDialogOpen(replacement.modal).value());
}

TEST_F(UIDialogTest, RootReleaseClearsRegisteredState)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UIDialogParts dialog = buildDialog(updater, root.rootNodeId());
    assertOk(updater.openDialog(dialog.modal));

    root.reset();
    EXPECT_FALSE(context->contains(dialog.modal));
    auto released = context->isDialogOpen(dialog.modal);
    ASSERT_FALSE(released.has_value());
    EXPECT_EQ(released.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(UIDialogTest, OpeningDialogDismissesWindowTransientOverlays)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UIDialogParts dialog = buildDialog(updater, root.rootNodeId());

    auto menuAnchor = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Menu"));
    auto menu = updater.createElement(
        root.rootNodeId(),
        UI::makeMenuElement({}, fixedSize(120.0F, 80.0F)));
    auto tooltipAnchor = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Tooltip"));
    auto tooltip = updater.createElement(
        root.rootNodeId(),
        UI::makeTooltipElement(
            "Help",
            UI::UITooltipConfig{.triggers = UI::UITooltipTrigger::Manual},
            fixedSize(80.0F, 24.0F)));
    ASSERT_TRUE(menuAnchor.has_value()) << menuAnchor.error().message;
    ASSERT_TRUE(menu.has_value()) << menu.error().message;
    ASSERT_TRUE(tooltipAnchor.has_value()) << tooltipAnchor.error().message;
    ASSERT_TRUE(tooltip.has_value()) << tooltip.error().message;
    assertOk(updater.setMenuAnchor(*menu, *menuAnchor));
    assertOk(updater.setTooltipAnchor(*tooltip, *tooltipAnchor));
    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));

    assertOk(updater.setMenuOpen(*menu, true));
    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    EXPECT_TRUE(updater.isMenuOpen(*menu).value());

    assertOk(context->openDialog(dialog.modal));
    EXPECT_TRUE(context->isDialogOpen(dialog.modal).value());
    EXPECT_FALSE(updater.isMenuOpen(*menu).value());
    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    EXPECT_EQ(context->activeModal(), dialog.modal);

    assertOk(context->dismissDialog(dialog.modal));
    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    assertOk(updater.showTooltip(*tooltip));
    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    EXPECT_TRUE(updater.isTooltipOpen(*tooltip).value());

    assertOk(context->openDialog(dialog.modal));
    EXPECT_TRUE(updater.isTooltipOpen(*tooltip).value());
    assertOk(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    EXPECT_FALSE(updater.isTooltipOpen(*tooltip).value());
}

TEST_F(UIDialogTest, DirtyCapacityFailurePreservesIntentAndCommittedModal)
{
    auto context = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 24,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
            .layoutSnapshotCapacity = 24,
            .hitSnapshotCapacity = 24,
            .paintSnapshotCapacity = 128,
            .textByteCapacity = 512,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UIDialogParts dialog = buildDialog(updater, root.rootNodeId());
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    const u64 committedHitRevision = context->committedHit().hitRevision();

    assertOk(updater.setLayoutStyle(
        root.rootNodeId(), fixedSize(300.0F, 160.0F)));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 1U);
    Core::Status rejected = updater.openDialog(dialog.modal);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(updater.isDialogOpen(dialog.modal).value());
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->committedHit().hitRevision(), committedHitRevision);

    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_FALSE(updater.isDialogOpen(dialog.modal).value());
}

} // namespace
} // namespace Tina::Tests
