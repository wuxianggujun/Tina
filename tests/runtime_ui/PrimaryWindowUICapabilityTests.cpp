#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UITheme.hpp>

#include "../../src/runtime/ui/PrimaryWindowUICapabilityState.hpp"

#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using CapabilityState = Runtime::Detail::PrimaryWindowUICapabilityState;
using CapabilityPhase = Runtime::Detail::PrimaryWindowUIPhase;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class PrimaryWindowUICapabilityTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto poolResult = WindowPool::Create(1);
        ASSERT_TRUE(poolResult.has_value()) << poolResult.error().message;
        windowPool.emplace(std::move(*poolResult));
        auto windowResult = windowPool->tryEmplace(0);
        ASSERT_TRUE(windowResult.has_value()) << windowResult.error().message;
        window = *windowResult;

        auto contextResult = UI::UIContext::Create(window, {.nodeCapacity = 16, .rootCapacity = 4});
        ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
        context = std::move(*contextResult);
    }

    std::optional<WindowPool> windowPool;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
};

[[nodiscard]] UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
{
    UI::UIBoxPaint paint;
    paint.solidFill = UI::UISolidFill{
        .color = {
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = alpha,
        },
    };
    return paint;
}

[[nodiscard]] UI::UIButtonActionCallback buttonAction(usize& activationCount) noexcept
{
    return UI::UIButtonActionCallback{[&activationCount](const UI::UIButtonActionEvent&) noexcept {
        ++activationCount;
    }};
}

TEST_F(PrimaryWindowUICapabilityTest, EnterCapabilityCreatesOneRootScopedTreeAndExpiresUnconditionally)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    EXPECT_TRUE(state.hasPrimaryWindowUI(*epoch, CapabilityPhase::GameStateEnter));

    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto panel = tree->createPanel(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    auto label = tree->createLabel(*panel);
    ASSERT_TRUE(label.has_value()) << label.error().message;
    auto button = tree->createButton(*panel);
    ASSERT_TRUE(button.has_value()) << button.error().message;
    EXPECT_EQ(context->liveRootCount(), 1U);
    EXPECT_EQ(context->liveNodeCount(), 4U);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    EXPECT_FALSE(state.hasPrimaryWindowUI(*epoch, CapabilityPhase::GameStateEnter));

    auto expiredTree = tree->createPanel(*panel);
    ASSERT_FALSE(expiredTree.has_value());
    EXPECT_EQ(expiredTree.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredBuilder = builder->createRoot();
    ASSERT_FALSE(expiredBuilder.has_value());
    EXPECT_EQ(expiredBuilder.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, TextEditSelectionFacadeRoundTripsAndExpiresWithPhase)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto textEdit = tree->createTextEdit(root->rootNodeId());
    ASSERT_TRUE(textEdit.has_value()) << textEdit.error().message;
    ASSERT_TRUE(tree->setText(*textEdit, "Tina").has_value());

    constexpr UI::UITextSelection Selection{.anchorCodepoint = 1, .caretCodepoint = 3};
    ASSERT_TRUE(tree->setTextSelection(*textEdit, Selection).has_value());
    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto currentSelection = treeView.textSelection(*textEdit);
    ASSERT_TRUE(currentSelection.has_value()) << currentSelection.error().message;
    EXPECT_EQ(*currentSelection, Selection);
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto expiredCreate = tree->createTextEdit(root->rootNodeId());
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->setTextSelection(*textEdit, {});
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = treeView.textSelection(*textEdit);
    ASSERT_FALSE(expiredQuery.has_value());
    EXPECT_EQ(expiredQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, EnabledFacadeRoundTripsAndExpiresWithPhase)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto initiallyEnabled = treeView.isEnabled(*button);
    ASSERT_TRUE(initiallyEnabled.has_value()) << initiallyEnabled.error().message;
    EXPECT_TRUE(*initiallyEnabled);
    ASSERT_TRUE(tree->setEnabled(*button, false).has_value());
    auto disabled = treeView.isEnabled(*button);
    ASSERT_TRUE(disabled.has_value()) << disabled.error().message;
    EXPECT_FALSE(*disabled);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    Core::Status expiredSet = tree->setEnabled(*button, true);
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = treeView.isEnabled(*button);
    ASSERT_FALSE(expiredQuery.has_value());
    EXPECT_EQ(expiredQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, ProductThemeFacadeUpdatesExistingControlsAndExpiresWithPhase)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    auto initialTheme = tree->productTheme();
    ASSERT_TRUE(initialTheme.has_value()) << initialTheme.error().message;
    EXPECT_EQ(*initialTheme, UI::makeDefaultProductTheme());
    ASSERT_TRUE(tree->setProductTheme(UI::makeLightProductTheme()).has_value());
    EXPECT_EQ(tree->productTheme().value(), UI::makeLightProductTheme());
    EXPECT_EQ(
        tree->buttonPaint(*button).value(),
        UI::makeButtonChrome(UI::makeLightProductTheme()).states);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    Core::Status expiredSet = tree->setProductTheme(UI::makeDefaultProductTheme());
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredGet = tree->productTheme();
    ASSERT_FALSE(expiredGet.has_value());
    EXPECT_EQ(expiredGet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, RangeAndSelectionControlFacadesRoundTripAndExpire)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    auto progressBar = tree->createProgressBar(root->rootNodeId());
    auto slider = tree->createSlider(root->rootNodeId());
    auto checkbox = tree->createCheckbox(root->rootNodeId());
    auto firstRadio = tree->createRadioButton(root->rootNodeId());
    auto secondRadio = tree->createRadioButton(root->rootNodeId());
    ASSERT_TRUE(progressBar.has_value()) << progressBar.error().message;
    ASSERT_TRUE(slider.has_value()) << slider.error().message;
    ASSERT_TRUE(checkbox.has_value()) << checkbox.error().message;
    ASSERT_TRUE(firstRadio.has_value()) << firstRadio.error().message;
    ASSERT_TRUE(secondRadio.has_value()) << secondRadio.error().message;

    constexpr UI::UIProgressBarPaint ProgressPaint{
        .fillColor = {.red = 20, .green = 180, .blue = 120, .alpha = 255},
    };
    constexpr UI::UIRadioButtonPaint RadioPaint{
        .indicatorColor = {.red = 30, .green = 40, .blue = 50, .alpha = 255},
        .selectedIndicatorColor = {.red = 240, .green = 180, .blue = 40, .alpha = 255},
        .selectedIndicatorInset = 5.0F,
        .labelGap = 7.0F,
    };
    constexpr UI::UICheckboxPaint CheckboxPaint{
        .checkedIndicatorColor = {.red = 240, .green = 240, .blue = 240, .alpha = 255},
        .checkedIndicatorInset = 4.0F,
    };
    constexpr UI::UISliderPaint SliderPaint{
        .filledTrackColor = {.red = 40, .green = 160, .blue = 220, .alpha = 255},
        .thumbColor = {.red = 235, .green = 240, .blue = 245, .alpha = 255},
        .draggingThumbColor = {.red = 255, .green = 200, .blue = 40, .alpha = 255},
        .contentInset = 4.0F,
        .thumbWidth = 8.0F,
    };
    ASSERT_TRUE(tree->setProgressBarRange(*progressBar, 10.0F, 20.0F).has_value());
    ASSERT_TRUE(tree->setProgressBarValue(*progressBar, 15.0F).has_value());
    ASSERT_TRUE(tree->setProgressBarPaint(*progressBar, ProgressPaint).has_value());
    ASSERT_TRUE(tree->setSliderPaint(*slider, SliderPaint).has_value());
    ASSERT_TRUE(tree->setSliderRange(*slider, 0.0F, 1.0F, 0.05F).has_value());
    ASSERT_TRUE(tree->setSliderValue(*slider, 0.55F).has_value());
    ASSERT_TRUE(tree->setCheckboxPaint(*checkbox, CheckboxPaint).has_value());
    ASSERT_TRUE(tree->setChecked(*checkbox, true).has_value());
    ASSERT_TRUE(tree->setRadioButtonPaint(*firstRadio, RadioPaint).has_value());
    ASSERT_TRUE(tree->setRadioButtonPaint(*secondRadio, RadioPaint).has_value());
    ASSERT_TRUE(tree->setText(*firstRadio, "Windowed").has_value());
    ASSERT_TRUE(tree->setText(*secondRadio, "Fullscreen").has_value());
    usize activations = 0;
    ASSERT_TRUE(tree->setRadioButtonAction(*firstRadio, buttonAction(activations)).has_value());
    ASSERT_TRUE(tree->setRadioButtonSelected(*firstRadio, true).has_value());
    ASSERT_TRUE(tree->setRadioButtonSelected(*secondRadio, true).has_value());

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto value = treeView.progressBarValue(*progressBar);
    auto progressPaint = treeView.progressBarPaint(*progressBar);
    auto sliderValue = treeView.sliderValue(*slider);
    auto sliderPaint = treeView.sliderPaint(*slider);
    auto checkboxPaint = treeView.checkboxPaint(*checkbox);
    auto checkboxChecked = treeView.isChecked(*checkbox);
    auto firstSelected = treeView.isRadioButtonSelected(*firstRadio);
    auto secondSelected = treeView.isRadioButtonSelected(*secondRadio);
    auto radioPaint = treeView.radioButtonPaint(*secondRadio);
    auto pressed = treeView.isRadioButtonPressed(*secondRadio);
    ASSERT_TRUE(value.has_value()) << value.error().message;
    ASSERT_TRUE(progressPaint.has_value()) << progressPaint.error().message;
    ASSERT_TRUE(sliderValue.has_value()) << sliderValue.error().message;
    ASSERT_TRUE(sliderPaint.has_value()) << sliderPaint.error().message;
    ASSERT_TRUE(checkboxPaint.has_value()) << checkboxPaint.error().message;
    ASSERT_TRUE(checkboxChecked.has_value()) << checkboxChecked.error().message;
    ASSERT_TRUE(firstSelected.has_value()) << firstSelected.error().message;
    ASSERT_TRUE(secondSelected.has_value()) << secondSelected.error().message;
    ASSERT_TRUE(radioPaint.has_value()) << radioPaint.error().message;
    ASSERT_TRUE(pressed.has_value()) << pressed.error().message;
    EXPECT_FLOAT_EQ(*value, 15.0F);
    EXPECT_EQ(*progressPaint, ProgressPaint);
    EXPECT_FLOAT_EQ(*sliderValue, 0.55F);
    EXPECT_EQ(*sliderPaint, SliderPaint);
    EXPECT_EQ(*checkboxPaint, CheckboxPaint);
    EXPECT_TRUE(*checkboxChecked);
    EXPECT_FALSE(*firstSelected);
    EXPECT_TRUE(*secondSelected);
    EXPECT_EQ(*radioPaint, RadioPaint);
    EXPECT_FALSE(*pressed);
    EXPECT_EQ(activations, 0U);
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto expiredCreate = tree->createProgressBar(root->rootNodeId());
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredValue = treeView.progressBarValue(*progressBar);
    ASSERT_FALSE(expiredValue.has_value());
    EXPECT_EQ(expiredValue.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredCheckboxPaint = treeView.checkboxPaint(*checkbox);
    ASSERT_FALSE(expiredCheckboxPaint.has_value());
    EXPECT_EQ(expiredCheckboxPaint.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredSliderPaint = treeView.sliderPaint(*slider);
    ASSERT_FALSE(expiredSliderPaint.has_value());
    EXPECT_EQ(expiredSliderPaint.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSetSliderPaint = tree->setSliderPaint(*slider, SliderPaint);
    ASSERT_FALSE(expiredSetSliderPaint.has_value());
    EXPECT_EQ(expiredSetSliderPaint.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSelect = tree->setRadioButtonSelected(*firstRadio, true);
    ASSERT_FALSE(expiredSelect.has_value());
    EXPECT_EQ(expiredSelect.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, ButtonPaintFacadeRoundTripsAndExpiresWithPhase)
{
    constexpr UI::UIButtonPaint ButtonPaint{
        .hoveredBackgroundColor = {.red = 30, .green = 80, .blue = 140, .alpha = 255},
        .pressedBackgroundColor = {.red = 20, .green = 60, .blue = 110, .alpha = 255},
        .focusedBackgroundColor = {.red = 220, .green = 170, .blue = 40, .alpha = 255},
        .disabledBackgroundColor = {.red = 70, .green = 75, .blue = 80, .alpha = 210},
    };

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto initialPaint = treeView.buttonPaint(*button);
    ASSERT_TRUE(initialPaint.has_value()) << initialPaint.error().message;
    EXPECT_EQ(*initialPaint, UI::makeButtonChrome(context->productTheme()).states);

    ASSERT_TRUE(tree->setButtonPaint(*button, ButtonPaint).has_value());
    auto configuredPaint = treeView.buttonPaint(*button);
    ASSERT_TRUE(configuredPaint.has_value()) << configuredPaint.error().message;
    EXPECT_EQ(*configuredPaint, ButtonPaint);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    Core::Status expiredSet = tree->setButtonPaint(*button, {});
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = treeView.buttonPaint(*button);
    ASSERT_FALSE(expiredQuery.has_value());
    EXPECT_EQ(expiredQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, ButtonPaintWrongKindFailureIsStickyAndPreventsLaterMutation)
{
    constexpr UI::UIButtonPaint ButtonPaint{
        .hoveredBackgroundColor = {.red = 40, .green = 100, .blue = 180, .alpha = 255},
        .pressedBackgroundColor = {.red = 20, .green = 60, .blue = 120, .alpha = 255},
        .focusedBackgroundColor = {.red = 245, .green = 190, .blue = 45, .alpha = 255},
        .disabledBackgroundColor = {.red = 80, .green = 85, .blue = 90, .alpha = 220},
    };

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto panel = tree->createPanel(root->rootNodeId());
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    ASSERT_TRUE(button.has_value()) << button.error().message;

    Core::Status wrongKind = tree->setButtonPaint(*panel, ButtonPaint);
    ASSERT_FALSE(wrongKind.has_value());
    EXPECT_EQ(wrongKind.error().code.domain, Core::ErrorDomain::UI);

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto otherwiseValid = treeView.buttonPaint(*button);
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, wrongKind.error().code);
    EXPECT_EQ(otherwiseValid.error().message, wrongKind.error().message);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, wrongKind.error().code);

    auto directUpdater = context->treeUpdater(*root);
    ASSERT_TRUE(directUpdater.has_value()) << directUpdater.error().message;
    auto unmodifiedPaint = directUpdater->buttonPaint(*button);
    ASSERT_TRUE(unmodifiedPaint.has_value()) << unmodifiedPaint.error().message;
    EXPECT_EQ(*unmodifiedPaint, UI::makeButtonChrome(context->productTheme()).states);
}

TEST_F(PrimaryWindowUICapabilityTest, ButtonActionFacadeSetsReplacesClearsAndQueriesInitialPressedState)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    auto initialPressed = tree->isButtonPressed(*button);
    ASSERT_TRUE(initialPressed.has_value()) << initialPressed.error().message;
    EXPECT_FALSE(*initialPressed);

    usize firstActivationCount = 0;
    usize replacementActivationCount = 0;
    ASSERT_TRUE(tree->setButtonAction(*button, buttonAction(firstActivationCount)).has_value());
    ASSERT_TRUE(tree->setButtonAction(*button, buttonAction(replacementActivationCount)).has_value());
    auto stillNotPressed = tree->isButtonPressed(*button);
    ASSERT_TRUE(stillNotPressed.has_value()) << stillNotPressed.error().message;
    EXPECT_FALSE(*stillNotPressed);
    ASSERT_TRUE(tree->clearButtonAction(*button).has_value());
    EXPECT_EQ(firstActivationCount, 0U);
    EXPECT_EQ(replacementActivationCount, 0U);
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, ButtonActionFacadeExpiresWithItsPhase)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value()) << button.error().message;
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    usize activationCount = 0;
    Core::Status expiredSet = tree->setButtonAction(*button, buttonAction(activationCount));
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredClear = tree->clearButtonAction(*button);
    ASSERT_FALSE(expiredClear.has_value());
    EXPECT_EQ(expiredClear.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredPressed = tree->isButtonPressed(*button);
    ASSERT_FALSE(expiredPressed.has_value());
    EXPECT_EQ(expiredPressed.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, ButtonActionWrongRootFailureIsStickyAndPreventsLaterMutation)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto firstRoot = builder->createRoot();
    auto secondRoot = builder->createRoot();
    ASSERT_TRUE(firstRoot.has_value()) << firstRoot.error().message;
    ASSERT_TRUE(secondRoot.has_value()) << secondRoot.error().message;
    auto firstTree = builder->treeUpdater(*firstRoot);
    ASSERT_TRUE(firstTree.has_value()) << firstTree.error().message;
    auto secondTree = builder->treeUpdater(*secondRoot);
    ASSERT_TRUE(secondTree.has_value()) << secondTree.error().message;
    auto firstButton = firstTree->createButton(firstRoot->rootNodeId());
    auto secondButton = secondTree->createButton(secondRoot->rootNodeId());
    ASSERT_TRUE(firstButton.has_value()) << firstButton.error().message;
    ASSERT_TRUE(secondButton.has_value()) << secondButton.error().message;

    usize activationCount = 0;
    Core::Status wrongRoot = firstTree->setButtonAction(*secondButton, buttonAction(activationCount));
    ASSERT_FALSE(wrongRoot.has_value());
    EXPECT_EQ(wrongRoot.error().code, UI::UIErrorCode::InvalidNode);

    Core::Status otherwiseValid = firstTree->clearButtonAction(*firstButton);
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, wrongRoot.error().code);
    EXPECT_EQ(otherwiseValid.error().message, wrongRoot.error().message);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(PrimaryWindowUICapabilityTest, ButtonActionWrongKindFailureIsStickyAndPreventsPressedQuery)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto panel = tree->createPanel(root->rootNodeId());
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    ASSERT_TRUE(button.has_value()) << button.error().message;

    usize activationCount = 0;
    Core::Status wrongKind = tree->setButtonAction(*panel, buttonAction(activationCount));
    ASSERT_FALSE(wrongKind.has_value());
    EXPECT_EQ(wrongKind.error().code.domain, Core::ErrorDomain::UI);

    auto otherwiseValid = tree->isButtonPressed(*button);
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, wrongKind.error().code);
    EXPECT_EQ(otherwiseValid.error().message, wrongKind.error().message);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, wrongKind.error().code);
}

TEST_F(PrimaryWindowUICapabilityTest, RoutedPointerListenerSurvivesItsRegistrationPhase)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto button = tree->createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    UI::UILayoutStyle rootStyle{};
    rootStyle.size.width = UI::UILayoutLength::Px(100.0F);
    rootStyle.size.height = UI::UILayoutLength::Px(100.0F);
    ASSERT_TRUE(tree->setLayoutStyle(root->rootNodeId(), rootStyle).has_value());
    UI::UILayoutStyle buttonStyle{};
    buttonStyle.size.width = UI::UILayoutLength::Px(50.0F);
    buttonStyle.size.height = UI::UILayoutLength::Px(40.0F);
    ASSERT_TRUE(tree->setLayoutStyle(*button, buttonStyle).has_value());
    ASSERT_TRUE(tree->setPointerHitPolicy(*button, UI::UIPointerHitPolicy::Targetable).has_value());

    usize callbackCount = 0;
    auto listener = tree->addRoutedPointerListener(
        {.node = *button, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent& event) noexcept {
            ++callbackCount;
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(listener.has_value()) << listener.error().message;
    EXPECT_EQ(context->statistics().activeRoutedPointerListenerCount, 1U);
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto expiredRegistration = tree->addRoutedPointerListener(
        {.node = *button, .kind = UI::UIRoutedPointerEventKind::ButtonUp, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
    ASSERT_FALSE(expiredRegistration.has_value());
    EXPECT_EQ(expiredRegistration.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);

    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 100.0F}).has_value());
    auto routed = context->routePointerInput(UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{1},
        .transitionOrdinal = 0,
        .sourceSequence = 1,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::ButtonDown,
        .position = {.x = 10.0F, .y = 10.0F},
        .button = Platform::PointerButton::Primary,
    });
    ASSERT_TRUE(routed.has_value()) << routed.error().message;
    EXPECT_EQ(callbackCount, 1U);
    EXPECT_TRUE(routed->consumed);

    listener->reset();
    EXPECT_EQ(context->statistics().activeRoutedPointerListenerCount, 0U);
}

TEST_F(PrimaryWindowUICapabilityTest, CrossRootListenerFailureIsStickyAndConsumesNoSlot)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto firstRoot = builder->createRoot();
    auto secondRoot = builder->createRoot();
    ASSERT_TRUE(firstRoot.has_value()) << firstRoot.error().message;
    ASSERT_TRUE(secondRoot.has_value()) << secondRoot.error().message;
    auto firstTree = builder->treeUpdater(*firstRoot);
    ASSERT_TRUE(firstTree.has_value()) << firstTree.error().message;

    auto crossRoot =
        firstTree->addRoutedPointerListener({.node = secondRoot->rootNodeId(),
                                             .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                                             .phases = UI::UIEventPhaseMask::Target},
                                            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
    ASSERT_FALSE(crossRoot.has_value());
    EXPECT_EQ(crossRoot.error().code, UI::UIErrorCode::InvalidNode);
    EXPECT_EQ(context->statistics().activeRoutedPointerListenerCount, 0U);

    auto otherwiseValid =
        firstTree->addRoutedPointerListener({.node = firstRoot->rootNodeId(),
                                             .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                                             .phases = UI::UIEventPhaseMask::Target},
                                            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, crossRoot.error().code);
    EXPECT_EQ(otherwiseValid.error().message, crossRoot.error().message);
    EXPECT_EQ(context->statistics().activeRoutedPointerListenerCount, 0U);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(PrimaryWindowUICapabilityTest, AbortPhaseInvalidatesFacadesAndAllowsTheNextPhase)
{
    CapabilityState state;
    auto enterEpoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(enterEpoch.has_value()) << enterEpoch.error().message;
    auto builder = state.rootBuilder(*enterEpoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;

    state.abortPhase(*enterEpoch, CapabilityPhase::GameStateEnter);
    EXPECT_FALSE(state.hasPrimaryWindowUI(*enterEpoch, CapabilityPhase::GameStateEnter));
    auto expired = builder->createRoot();
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    EXPECT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, HeadlessRequestSticksTheUnavailableErrorUntilPhaseFinish)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(nullptr);
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    EXPECT_FALSE(state.hasPrimaryWindowUI(*epoch, CapabilityPhase::GameStateEnter));

    auto first = state.rootBuilder(*epoch);
    ASSERT_FALSE(first.has_value());
    EXPECT_EQ(first.error().code, RuntimeErrorCode::PrimaryWindowUIUnavailable);
    auto second = state.rootBuilder(*epoch);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, first.error().code);
    EXPECT_EQ(second.error().message, first.error().message);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, RuntimeErrorCode::PrimaryWindowUIUnavailable);
}

TEST_F(PrimaryWindowUICapabilityTest, FirstTreeFailureIsStickyAndPreventsLaterMutation)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto firstRoot = builder->createRoot();
    auto secondRoot = builder->createRoot();
    ASSERT_TRUE(firstRoot.has_value()) << firstRoot.error().message;
    ASSERT_TRUE(secondRoot.has_value()) << secondRoot.error().message;
    auto firstTree = builder->treeUpdater(*firstRoot);
    ASSERT_TRUE(firstTree.has_value()) << firstTree.error().message;

    auto crossRoot = firstTree->createPanel(secondRoot->rootNodeId());
    ASSERT_FALSE(crossRoot.has_value());
    EXPECT_EQ(crossRoot.error().code, UI::UIErrorCode::InvalidNode);
    const usize nodesAfterFailure = context->liveNodeCount();

    auto otherwiseValid = firstTree->createPanel(firstRoot->rootNodeId());
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, crossRoot.error().code);
    EXPECT_EQ(context->liveNodeCount(), nodesAfterFailure);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(PrimaryWindowUICapabilityTest, UpdateCapabilityMutatesOwnedTreeThenExpires)
{
    CapabilityState state;
    auto enterEpoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(enterEpoch.has_value()) << enterEpoch.error().message;
    auto builder = state.rootBuilder(*enterEpoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto enterTree = builder->treeUpdater(*root);
    ASSERT_TRUE(enterTree.has_value()) << enterTree.error().message;
    auto panel = enterTree->createPanel(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    ASSERT_TRUE(state.finishPhase(*enterEpoch, CapabilityPhase::GameStateEnter).has_value());

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    auto updateTree = state.treeUpdater(*updateEpoch, CapabilityPhase::UIUpdate, *root);
    ASSERT_TRUE(updateTree.has_value()) << updateTree.error().message;
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(320.0F);
    style.size.height = UI::UILayoutLength::Px(180.0F);
    ASSERT_TRUE(updateTree->setLayoutStyle(*panel, style).has_value());
    ASSERT_TRUE(updateTree->setBoxPaint(*panel, solidFill(40, 80, 120, 200)).has_value());
    auto alive = updateTree->isAlive(*panel);
    ASSERT_TRUE(alive.has_value()) << alive.error().message;
    EXPECT_TRUE(*alive);
    ASSERT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
    ASSERT_TRUE(context->commitLayout({.width = 640.0F, .height = 360.0F}).has_value());
    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 1U);
    EXPECT_EQ(paint.entries().front().node, *panel);
    EXPECT_EQ(paint.entries().front().solidFill,
              (UI::UIPremultipliedRgba8Color{.red = 31, .green = 63, .blue = 94, .alpha = 200}));

    auto expired = updateTree->setBoxPaint(*panel, solidFill(1, 2, 3));
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, SetBoxPaintFailureIsStickyAcrossContextAndPreventsLaterMutation)
{
    auto foreignContextResult = UI::UIContext::Create(window, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_TRUE(foreignContextResult.has_value()) << foreignContextResult.error().message;
    std::unique_ptr<UI::UIContext> foreignContext = std::move(*foreignContextResult);
    auto foreignRoot = foreignContext->rootBuilder().createRoot();
    ASSERT_TRUE(foreignRoot.has_value()) << foreignRoot.error().message;
    auto foreignPanel = foreignContext->rootBuilder().createPanel(foreignRoot->rootNodeId());
    ASSERT_TRUE(foreignPanel.has_value()) << foreignPanel.error().message;

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto panel = tree->createPanel(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;

    Core::Status wrongContext = tree->setBoxPaint(*foreignPanel, solidFill(255, 0, 0));
    ASSERT_FALSE(wrongContext.has_value());
    EXPECT_EQ(wrongContext.error().code, UI::UIErrorCode::WrongContext);
    const bool wasPaintDirty = context->statistics().paintDirty;

    Core::Status otherwiseValid = tree->setBoxPaint(*panel, solidFill(0, 255, 0));
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, wrongContext.error().code);
    EXPECT_EQ(otherwiseValid.error().message, wrongContext.error().message);
    EXPECT_EQ(context->statistics().paintDirty, wasPaintDirty);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::WrongContext);
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 50.0F}).has_value());
    EXPECT_TRUE(context->committedPaint().empty());
}

TEST_F(PrimaryWindowUICapabilityTest, SetBoxPaintRejectsStaleGenerationAndSticksTheError)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto panel = tree->createPanel(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    ASSERT_TRUE(tree->destroy(*panel).has_value());

    Core::Status staleGeneration = tree->setBoxPaint(*panel, solidFill(10, 20, 30));
    ASSERT_FALSE(staleGeneration.has_value());
    EXPECT_EQ(staleGeneration.error().code, UI::UIErrorCode::InvalidNode);

    Core::Status otherwiseValid = tree->setBoxPaint(root->rootNodeId(), solidFill(40, 80, 120));
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, staleGeneration.error().code);
    EXPECT_EQ(otherwiseValid.error().message, staleGeneration.error().message);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(PrimaryWindowUICapabilityTest, CrossThreadUseFailsWithoutPoisoningTheOwnerPhase)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    std::optional<Core::Result<bool>> crossThread;
    std::thread worker([&] { crossThread.emplace(tree->isAlive(root->rootNodeId())); });
    worker.join();
    ASSERT_TRUE(crossThread.has_value());
    ASSERT_FALSE(crossThread->has_value());
    EXPECT_EQ(crossThread->error().code, RuntimeErrorCode::WrongOwnerThread);

    auto ownerThread = tree->isAlive(root->rootNodeId());
    ASSERT_TRUE(ownerThread.has_value()) << ownerThread.error().message;
    EXPECT_TRUE(*ownerThread);
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, MovedFromFacadesReportExpired)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builderResult = state.rootBuilder(*epoch);
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    PrimaryWindowUIRootBuilder builder = std::move(*builderResult);
    PrimaryWindowUIRootBuilder movedBuilder = std::move(builder);

    auto movedFrom = builder.createRoot();
    ASSERT_FALSE(movedFrom.has_value());
    EXPECT_EQ(movedFrom.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto root = movedBuilder.createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

} // namespace
} // namespace Tina::Tests
