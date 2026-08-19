#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <ranges>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Core::AssetId iconAssetId()
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x71};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] UI::UIIconContent iconContent()
{
    return {
        .source = {
            .texture = iconAssetId(),
            .sourcePixels = {.width = 16, .height = 16},
            .texturePixelExtent = {.width = 16, .height = 16},
            .intrinsicLogicalSize = {.width = 16.0F, .height = 16.0F},
        },
    };
}

[[nodiscard]] const UI::UICommittedNodeEntry* findStructure(
    UI::UICommittedStructureView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UICommittedNodeEntry& entry) {
            return entry.node == node;
        });
    return found == view.end() ? nullptr : &*found;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemantics(
    UI::UICommittedSemanticsView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UISemanticsEntry& entry) {
            return entry.node == node;
        });
    return found == view.end() ? nullptr : &*found;
}

void expectSuccess(const Core::Status& status)
{
    ASSERT_TRUE(status.has_value()) << status.error().message;
}

class UIComponentProfileTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto pool = WindowPool::Create(1);
        ASSERT_TRUE(pool.has_value()) << pool.error().message;
        windows.emplace(std::move(*pool));
        auto createdWindow = windows->tryEmplace(0);
        ASSERT_TRUE(createdWindow.has_value()) << createdWindow.error().message;
        window = *createdWindow;
    }

    [[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
        usize imageCapacity = 8) const
    {
        auto context = UI::UIContext::Create(
            window,
            {
                .nodeCapacity = 64,
                .rootCapacity = 1,
                .layoutSnapshotCapacity = 64,
                .hitSnapshotCapacity = 64,
                .paintSnapshotCapacity = 256,
                .imageContentCapacity = imageCapacity,
                .textByteCapacity = 2048,
            });
        EXPECT_TRUE(context.has_value())
            << (context ? "" : context.error().message);
        return context ? std::move(*context) : nullptr;
    }

    std::optional<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UIComponentProfileTest, RequiredBudgetsAreExactAndRejectInvalidContracts)
{
    const UI::UIIconButtonConfig iconButton{
        .icon = iconContent(),
        .accessibleName = "Refresh",
        .accessibleDescription = "Reload data",
        .tooltipText = "Reload",
    };
    auto iconBudget = UI::requiredIconButtonBuildBudget(iconButton);
    ASSERT_TRUE(iconBudget.has_value()) << iconBudget.error().message;
    EXPECT_EQ(*iconBudget,
              (UI::UIComponentBuildBudget{
                  .nodes = 4,
                  .textBytes = 24,
                  .behaviors = {.activate = 1},
              }));

    const UI::UIFormFieldConfig formField{
        .label = "Name",
        .value = "Ada",
        .helperText = "Shown",
        .errorText = "Required",
        .leadingAction = UI::UIFormFieldActionConfig{
            .icon = iconContent(),
            .accessibleName = "Open",
            .accessibleDescription = "Choose",
            .tooltipText = "Browse",
        },
        .trailingAction = UI::UIFormFieldActionConfig{
            .icon = iconContent(),
            .accessibleName = "Clear",
            .tooltipText = "Reset",
        },
    };
    auto formBudget = UI::requiredFormFieldBuildBudget(formField);
    ASSERT_TRUE(formBudget.has_value()) << formBudget.error().message;
    EXPECT_EQ(*formBudget,
              (UI::UIComponentBuildBudget{
                  .nodes = 12,
                  .textBytes = 58,
                  .behaviors = {.activate = 2, .textInput = 1},
              }));

    const std::array actions{
        UI::UIDialogActionConfig{.text = "Cancel"},
        UI::UIDialogActionConfig{
            .text = "Delete", .variant = UI::UIButtonVariant::Danger},
    };
    const UI::UIDialogConfig dialog{
        .title = "Delete",
        .body = "Cannot undo",
        .actions = actions,
    };
    auto dialogBudget = UI::requiredDialogBuildBudget(dialog);
    ASSERT_TRUE(dialogBudget.has_value()) << dialogBudget.error().message;
    EXPECT_EQ(*dialogBudget,
              (UI::UIComponentBuildBudget{
                  .nodes = 7,
                  .textBytes = 46,
                  .behaviors = {.activate = 2},
              }));

    UI::UIIconButtonConfig invalid = iconButton;
    invalid.accessibleName = {};
    const auto rejected = UI::requiredIconButtonBuildBudget(invalid);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST_F(UIComponentProfileTest, IconButtonPublishesSiblingTooltipAndSingleAccessibleRoot)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value()) << rootResult.error().message;
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UIIconButtonConfig config{
        .icon = iconContent(),
        .accessibleName = "Refresh",
        .tooltipText = "Reload data",
        .variant = UI::UIButtonVariant::Primary,
    };
    auto built = updater.buildIconButton(root.rootNodeId(), config);
    ASSERT_TRUE(built.has_value()) << built.error().message;
    const UI::UIIconButtonParts parts = *built;
    expectSuccess(context->commitLayout({.width = 320.0F, .height = 180.0F}));

    const UI::UICommittedNodeEntry* button =
        findStructure(context->committedStructure(), parts.button);
    const UI::UICommittedNodeEntry* icon =
        findStructure(context->committedStructure(), parts.icon);
    const UI::UICommittedNodeEntry* tooltip =
        findStructure(context->committedStructure(), parts.tooltip);
    ASSERT_NE(button, nullptr);
    ASSERT_NE(icon, nullptr);
    ASSERT_NE(tooltip, nullptr);
    EXPECT_EQ(button->parent, parts.root);
    EXPECT_EQ(icon->parent, parts.button);
    EXPECT_EQ(tooltip->parent, parts.root);
    EXPECT_EQ(updater.tooltipAnchor(parts.tooltip).value(), parts.button);

    const UI::UISemanticsEntry* accessibleButton =
        findSemantics(context->committedSemantics(), parts.button);
    ASSERT_NE(accessibleButton, nullptr);
    EXPECT_EQ(accessibleButton->role, UI::UISemanticsRole::Button);
    EXPECT_EQ(accessibleButton->name, "Refresh");
    EXPECT_EQ(accessibleButton->description, "Reload data");
    EXPECT_EQ(findSemantics(context->committedSemantics(), parts.root), nullptr);
    EXPECT_EQ(findSemantics(context->committedSemantics(), parts.icon), nullptr);
    EXPECT_EQ(findSemantics(context->committedSemantics(), parts.tooltip), nullptr);
    EXPECT_EQ(context->statistics().activeImageContentCount, 1U);

    EXPECT_EQ(updater.styleRole(parts.icon).value(),
              UI::UIStyleRoleId::IconOnPrimary);
    EXPECT_EQ(updater.imageTint(parts.icon).value(),
              context->productTheme().colors.onPrimary);

    const UI::UITheme light =
        UI::makeModernDesktopTheme(UI::UIColorScheme::Light);
    expectSuccess(context->setProductTheme(light));
    EXPECT_EQ(updater.imageTint(parts.icon).value(), light.colors.onPrimary);

    const UI::UIStraightSrgba8Color localTint = UI::rgba8(1, 2, 3, 255);
    expectSuccess(updater.setImageTint(parts.icon, localTint));
    const UI::UITheme dark =
        UI::makeModernDesktopTheme(UI::UIColorScheme::Dark);
    expectSuccess(context->setProductTheme(dark));
    EXPECT_EQ(updater.imageTint(parts.icon).value(), localTint);
    expectSuccess(updater.clearOverride(parts.icon, UI::UIStyleOverride::ImageTint));
    EXPECT_EQ(updater.imageTint(parts.icon).value(), dark.colors.onPrimary);
}

TEST_F(UIComponentProfileTest, FormFieldOwnsOneTextEditAndExplicitAccessibleDescription)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value()) << rootResult.error().message;
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    UI::UITreeUpdater updater = std::move(*updaterResult);

    const UI::UIFormFieldConfig config{
        .label = "Project name",
        .value = "Tina",
        .helperText = "Shown in the title bar",
        .errorText = "A unique name is required",
        .leadingAction = UI::UIFormFieldActionConfig{
            .icon = iconContent(),
            .accessibleName = "Browse projects",
            .tooltipText = "Browse",
        },
        .trailingAction = UI::UIFormFieldActionConfig{
            .icon = iconContent(),
            .accessibleName = "Clear project name",
        },
    };
    const auto expectedBudget = UI::requiredFormFieldBuildBudget(config);
    ASSERT_TRUE(expectedBudget.has_value()) << expectedBudget.error().message;
    auto built = updater.buildFormField(root.rootNodeId(), config);
    ASSERT_TRUE(built.has_value()) << built.error().message;
    const UI::UIFormFieldParts parts = *built;
    expectSuccess(context->commitLayout({.width = 480.0F, .height = 240.0F}));

    const UI::UISemanticsEntry* textEdit =
        findSemantics(context->committedSemantics(), parts.textEdit);
    ASSERT_NE(textEdit, nullptr);
    EXPECT_EQ(textEdit->role, UI::UISemanticsRole::TextEdit);
    EXPECT_EQ(textEdit->name, "Project name");
    EXPECT_EQ(textEdit->description, "A unique name is required");
    EXPECT_EQ(updater.styleRole(parts.textEdit).value(),
              UI::UIStyleRoleId::TextInputInvalid);
    EXPECT_EQ(updater.textEditPaint(parts.textEdit).value(),
              UI::makeInvalidTextEditChrome(context->productTheme()).paint);

    const UI::UISemanticsEntry* leading = findSemantics(
        context->committedSemantics(), parts.leadingAction.button);
    const UI::UISemanticsEntry* trailing = findSemantics(
        context->committedSemantics(), parts.trailingAction.button);
    ASSERT_NE(leading, nullptr);
    ASSERT_NE(trailing, nullptr);
    EXPECT_EQ(leading->name, "Browse projects");
    EXPECT_EQ(trailing->name, "Clear project name");
    EXPECT_EQ(findSemantics(context->committedSemantics(),
                            parts.leadingAction.icon),
              nullptr);
    EXPECT_EQ(findSemantics(context->committedSemantics(),
                            parts.trailingAction.icon),
              nullptr);
    EXPECT_EQ(updater.tooltipAnchor(parts.leadingAction.tooltip).value(),
              parts.leadingAction.button);

    const UI::UIComponentBuildStatistics build =
        context->statistics().componentBuild;
    EXPECT_EQ(build.nodes.requested, expectedBudget->nodes);
    EXPECT_EQ(build.nodes.published, expectedBudget->nodes);
    EXPECT_EQ(build.textBytes.requested, expectedBudget->textBytes);
    EXPECT_EQ(build.textBytes.published, expectedBudget->textBytes);
    EXPECT_EQ(build.behaviors.activate.published, 2U);
    EXPECT_EQ(build.behaviors.textInput.published, 1U);
}

TEST_F(UIComponentProfileTest, DialogReusesModalScopeAndRestoresPriorFocus)
{
    auto context = createContext();
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value()) << rootResult.error().message;
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    UI::UITreeUpdater updater = std::move(*updaterResult);

    auto background = updater.createElement(
        root.rootNodeId(), UI::makeButtonElement("Background"));
    ASSERT_TRUE(background.has_value()) << background.error().message;
    expectSuccess(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    expectSuccess(context->requestFocus(*background));

    const std::array actions{
        UI::UIDialogActionConfig{.text = "Cancel"},
        UI::UIDialogActionConfig{
            .text = "Delete", .variant = UI::UIButtonVariant::Danger},
    };
    auto built = updater.buildDialog(
        root.rootNodeId(),
        UI::UIDialogConfig{
            .title = "Delete project",
            .body = "This operation cannot be undone.",
            .actions = actions,
        });
    ASSERT_TRUE(built.has_value()) << built.error().message;
    const UI::UIDialogParts parts = *built;
    expectSuccess(context->commitLayout({.width = 640.0F, .height = 360.0F}));

    EXPECT_EQ(context->activeModal(), parts.modal);
    EXPECT_EQ(context->activeFocusScope(), parts.modal);
    EXPECT_EQ(context->defaultActionFocus(), parts.actions[0]);
    const UI::UISemanticsEntry* dialog =
        findSemantics(context->committedSemantics(), parts.modal);
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->role, UI::UISemanticsRole::Dialog);
    EXPECT_EQ(dialog->name, "Delete project");
    EXPECT_EQ(dialog->description, "This operation cannot be undone.");
    const UI::UISemanticsEntry* cancel =
        findSemantics(context->committedSemantics(), parts.actions[0]);
    const UI::UISemanticsEntry* destructive =
        findSemantics(context->committedSemantics(), parts.actions[1]);
    ASSERT_NE(cancel, nullptr);
    ASSERT_NE(destructive, nullptr);
    EXPECT_EQ(cancel->name, "Cancel");
    EXPECT_EQ(destructive->name, "Delete");

    expectSuccess(updater.destroy(parts.modal));
    expectSuccess(context->commitLayout({.width = 640.0F, .height = 360.0F}));
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), *background);
}

TEST_F(UIComponentProfileTest, ImageCapacityFailureRollsBackWholeProfileAndCanRetry)
{
    auto context = createContext(1);
    ASSERT_NE(context, nullptr);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value()) << rootResult.error().message;
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    UI::UITreeUpdater updater = std::move(*updaterResult);

    auto occupied = updater.createElement(
        root.rootNodeId(), UI::makeIconElement(iconContent()));
    ASSERT_TRUE(occupied.has_value()) << occupied.error().message;
    const usize baselineNodes = context->liveNodeCount();
    const usize baselineTextBytes = context->statistics().textByteUsed;

    const UI::UIIconButtonConfig config{
        .icon = iconContent(),
        .accessibleName = "Refresh",
        .tooltipText = "Reload",
    };
    const auto rejected = context->buildIconButton(root.rootNodeId(), config);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), baselineNodes);
    EXPECT_EQ(context->statistics().textByteUsed, baselineTextBytes);
    EXPECT_EQ(context->statistics().activeImageContentCount, 1U);
    EXPECT_EQ(context->statistics().activeActivateBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().componentBuild.activeTransactionCount, 0U);

    expectSuccess(updater.destroy(*occupied));
    auto retried = context->buildIconButton(root.rootNodeId(), config);
    ASSERT_TRUE(retried.has_value()) << retried.error().message;
    EXPECT_EQ(context->statistics().activeImageContentCount, 1U);
    EXPECT_EQ(context->statistics().activeActivateBehaviorCount, 1U);
    expectSuccess(context->commitLayout({.width = 240.0F, .height = 120.0F}));
    EXPECT_EQ(updater.tooltipAnchor(retried->tooltip).value(), retried->button);
}

} // namespace
} // namespace Tina::Tests
