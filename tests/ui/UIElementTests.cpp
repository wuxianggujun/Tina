#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string_view>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId ownerWindow,
    UI::UIContextCapacityConfig capacities,
    bool applyDefaultProductChrome = false)
{
    capacities.applyDefaultProductChrome = applyDefaultProductChrome;
    auto contextResult = UI::UIContext::Create(ownerWindow, capacities);
    EXPECT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    return contextResult ? std::move(*contextResult) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto rootResult = context.rootBuilder().createRoot();
    EXPECT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
    return rootResult ? std::move(*rootResult) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto updaterResult = context.treeUpdater(root);
    EXPECT_TRUE(updaterResult.has_value()) << (updaterResult ? "" : updaterResult.error().message);
    return updaterResult ? std::move(*updaterResult) : UI::UITreeUpdater{};
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayoutEntry(
    UI::UICommittedLayoutView layout,
    UI::UINodeId node)
{
    const auto entry = std::ranges::find_if(
        layout,
        [node](const UI::UICommittedLayoutEntry& candidate) { return candidate.node == node; });
    return entry == layout.end() ? nullptr : &*entry;
}

[[nodiscard]] const UI::UICommittedHitEntry* findHitEntry(
    UI::UICommittedHitView hit,
    UI::UINodeId node)
{
    const auto entry = std::ranges::find_if(
        hit,
        [node](const UI::UICommittedHitEntry& candidate) { return candidate.node == node; });
    return entry == hit.end() ? nullptr : &*entry;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(
    UI::UICommittedSemanticsView semantics,
    UI::UINodeId node)
{
    const auto entry = std::ranges::find_if(
        semantics,
        [node](const UI::UISemanticsEntry& candidate) { return candidate.node == node; });
    return entry == semantics.end() ? nullptr : &*entry;
}

[[nodiscard]] UI::UILayoutStyle fixedOverlay(float x, float y, float width, float height) noexcept
{
    UI::UILayoutStyle layout{};
    layout.size = {
        .width = UI::UILayoutLength::Px(width),
        .height = UI::UILayoutLength::Px(height),
    };
    layout.overlay.offset = {
        .x = UI::UILayoutLength::Px(x),
        .y = UI::UILayoutLength::Px(y),
    };
    layout.placement = UI::UILayoutPlacement::Overlay;
    return layout;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

class UIElementTest : public testing::Test {
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

TEST_F(UIElementTest, DescriptorInitializesLayoutTextStyleAndAlignmentBeforeFirstCommit)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 16,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 4,
            .paintSnapshotCapacity = 16,
            .textByteCapacity = 32,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle layout{};
    layout.size = {
        .width = UI::UILayoutLength::Px(120.0F),
        .height = UI::UILayoutLength::Px(40.0F),
    };
    layout.padding = UI::UIEdgeSpacing::HorizontalVertical(12.0F, 6.0F);

    constexpr UI::UITextStyle TextStyle{
        .logicalSize = 18.0F,
        .advanceScale = 0.5F,
        .lineHeightScale = 1.0F,
        .color = {.red = 32, .green = 64, .blue = 96, .alpha = 255},
    };
    constexpr UI::UIContentAlignment Alignment{
        .horizontal = UI::UIAxisAlignment::End,
        .vertical = UI::UIAxisAlignment::Center,
    };

    UI::UIElementDescriptor descriptor = UI::makeButtonElement("Launch", layout);
    descriptor.textStyle = TextStyle;
    descriptor.contentAlignment = Alignment;
    descriptor.enabled = false;

    auto buttonResult = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(buttonResult.has_value()) << buttonResult.error().message;
    const UI::UINodeId button = *buttonResult;

    const auto text = updater.text(button);
    ASSERT_TRUE(text.has_value()) << text.error().message;
    EXPECT_EQ(*text, std::string_view("Launch"));
    const auto textStyle = updater.textStyle(button);
    ASSERT_TRUE(textStyle.has_value()) << textStyle.error().message;
    EXPECT_EQ(*textStyle, TextStyle);
    const auto alignment = updater.contentAlignment(button);
    ASSERT_TRUE(alignment.has_value()) << alignment.error().message;
    EXPECT_EQ(*alignment, Alignment);
    const auto enabled = updater.isEnabled(button);
    ASSERT_TRUE(enabled.has_value()) << enabled.error().message;
    EXPECT_FALSE(*enabled);

    const Core::Status commit = context->commitLayout({.width = 320.0F, .height = 200.0F});
    ASSERT_TRUE(commit.has_value()) << commit.error().message;
    const UI::UICommittedLayoutEntry* entry = findLayoutEntry(context->committedLayout(), button);
    ASSERT_NE(entry, nullptr);
    EXPECT_FLOAT_EQ(entry->worldRect.width, 120.0F);
    EXPECT_FLOAT_EQ(entry->worldRect.height, 40.0F);
    EXPECT_TRUE(entry->contentPlacement.hasIntrinsicContent);
    EXPECT_GT(entry->contentPlacement.origin.x, entry->contentPlacement.contentBox.x);
    EXPECT_GT(entry->contentPlacement.origin.y, entry->contentPlacement.contentBox.y);
}

TEST_F(UIElementTest, RejectsInvalidBehaviorContentAndPopupPlacementCombinations)
{
    auto context = createContext(window, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    UI::UIElementDescriptor toggleWithText{
        .text = "not supported",
        .behaviors = UI::UIElementBehavior::Focusable | UI::UIElementBehavior::Activate |
                     UI::UIElementBehavior::Toggle,
    };
    const auto invalidToggle = updater.createElement(root.rootNodeId(), toggleWithText);
    ASSERT_FALSE(invalidToggle.has_value());
    EXPECT_EQ(invalidToggle.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    UI::UIElementDescriptor flowPopup{
        .behaviors = UI::UIElementBehavior::Popup,
    };
    const auto invalidPopup = updater.createElement(root.rootNodeId(), flowPopup);
    ASSERT_FALSE(invalidPopup.has_value());
    EXPECT_EQ(invalidPopup.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    UI::UIElementDescriptor nonContainingModal = UI::makeModalElement();
    nonContainingModal.focusScopeMode = UI::UIFocusScopeMode::None;
    const auto invalidModalScope = updater.createElement(root.rootNodeId(), nonContainingModal);
    ASSERT_FALSE(invalidModalScope.has_value());
    EXPECT_EQ(invalidModalScope.error().code, UI::UIErrorCode::InvalidFocusScope);

    UI::UIElementDescriptor nonContainingPopup = UI::makePopupElement();
    nonContainingPopup.focusScopeMode = UI::UIFocusScopeMode::None;
    const auto invalidPopupScope = updater.createElement(root.rootNodeId(), nonContainingPopup);
    ASSERT_FALSE(invalidPopupScope.has_value());
    EXPECT_EQ(invalidPopupScope.error().code, UI::UIErrorCode::InvalidFocusScope);

    UI::UIElementDescriptor panelWithListConfig = UI::makePanelElement();
    ++panelWithListConfig.listView.materializedItemCapacity;
    const auto invalidListConfig = updater.createElement(root.rootNodeId(), panelWithListConfig);
    ASSERT_FALSE(invalidListConfig.has_value());
    EXPECT_EQ(invalidListConfig.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    UI::UIElementDescriptor panelWithTreeConfig = UI::makePanelElement();
    ++panelWithTreeConfig.treeView.materializedItemCapacity;
    const auto invalidTreeConfig = updater.createElement(root.rootNodeId(), panelWithTreeConfig);
    ASSERT_FALSE(invalidTreeConfig.has_value());
    EXPECT_EQ(invalidTreeConfig.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    EXPECT_EQ(context->liveNodeCount(), 1U);
}

TEST_F(UIElementTest, TextCapacityFailureRollsBackNodeAndTextStorage)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .textByteCapacity = 4,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    const usize liveNodesBeforeCreate = context->liveNodeCount();
    const auto label = updater.createElement(root.rootNodeId(), UI::makeLabelElement("12345"));
    ASSERT_FALSE(label.has_value());
    EXPECT_EQ(label.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), liveNodesBeforeCreate);
    EXPECT_EQ(context->statistics().textByteUsed, 0U);

    const auto replacement = updater.createElement(root.rootNodeId(), UI::makeLabelElement("1234"));
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    EXPECT_EQ(context->liveNodeCount(), liveNodesBeforeCreate + 1U);
    EXPECT_EQ(context->statistics().textByteUsed, 4U);
}

TEST_F(UIElementTest, DescriptorPublishesExplicitSemanticsThroughAutomaticAncestors)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 64,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 64,
            .paintSnapshotCapacity = 64,
            .textByteCapacity = 256,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    UI::UIElementDescriptor groupDescriptor = UI::makePanelElement();
    groupDescriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .role = UI::UISemanticsRole::Group,
        .name = "Settings",
        .description = "Application settings",
        .readOnly = true,
    };
    auto group = updater.createElement(root.rootNodeId(), groupDescriptor);
    ASSERT_TRUE(group.has_value()) << group.error().message;

    auto automatic = updater.createElement(*group, UI::makePanelElement());
    ASSERT_TRUE(automatic.has_value()) << automatic.error().message;

    UI::UIElementDescriptor buttonDescriptor = UI::makeButtonElement("Fallback button text");
    buttonDescriptor.semantics.name = "Apply changes";
    buttonDescriptor.semantics.description = "Commits current settings";
    auto button = updater.createElement(*automatic, buttonDescriptor);
    ASSERT_TRUE(button.has_value()) << button.error().message;

    UI::UIElementDescriptor emptyNameDescriptor = UI::makeLabelElement("Visible label text");
    emptyNameDescriptor.semantics.name = std::string_view{};
    auto explicitlyUnnamed = updater.createElement(*automatic, emptyNameDescriptor);
    ASSERT_TRUE(explicitlyUnnamed.has_value()) << explicitlyUnnamed.error().message;

    assertOk(context->commitLayout({.width = 480.0F, .height = 240.0F}));
    const UI::UICommittedSemanticsView semantics = context->committedSemantics();
    ASSERT_EQ(semantics.size(), 3U);

    const UI::UISemanticsEntry* groupEntry = findSemanticsEntry(semantics, *group);
    const UI::UISemanticsEntry* buttonEntry = findSemanticsEntry(semantics, *button);
    const UI::UISemanticsEntry* unnamedEntry = findSemanticsEntry(semantics, *explicitlyUnnamed);
    ASSERT_NE(groupEntry, nullptr);
    ASSERT_NE(buttonEntry, nullptr);
    ASSERT_NE(unnamedEntry, nullptr);
    EXPECT_EQ(findSemanticsEntry(semantics, *automatic), nullptr);

    EXPECT_FALSE(groupEntry->parent.hasValue());
    EXPECT_EQ(groupEntry->role, UI::UISemanticsRole::Group);
    EXPECT_EQ(groupEntry->name, "Settings");
    EXPECT_EQ(groupEntry->description, "Application settings");
    EXPECT_TRUE(groupEntry->readOnly);

    EXPECT_EQ(buttonEntry->parent, *group);
    EXPECT_EQ(buttonEntry->role, UI::UISemanticsRole::Button);
    EXPECT_EQ(buttonEntry->name, "Apply changes");
    EXPECT_EQ(buttonEntry->description, "Commits current settings");
    EXPECT_TRUE(UI::hasSemanticsAction(buttonEntry->actions, UI::UISemanticsAction::Focus));
    EXPECT_TRUE(UI::hasSemanticsAction(buttonEntry->actions, UI::UISemanticsAction::Activate));

    EXPECT_EQ(unnamedEntry->parent, *group);
    EXPECT_EQ(unnamedEntry->role, UI::UISemanticsRole::Label);
    EXPECT_TRUE(unnamedEntry->name.empty());
}

TEST_F(UIElementTest, MergeDescendantsCombinesEligibleNamesAndExcludeRemovesItsSubtree)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 32,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 32,
            .paintSnapshotCapacity = 32,
            .textByteCapacity = 256,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    UI::UIElementDescriptor mergedDescriptor = UI::makePanelElement();
    mergedDescriptor.semantics = {
        .mode = UI::UISemanticsMode::MergeDescendants,
        .role = UI::UISemanticsRole::Group,
        .name = "Account",
    };
    auto merged = updater.createElement(root.rootNodeId(), mergedDescriptor);
    ASSERT_TRUE(merged.has_value()) << merged.error().message;
    auto first = updater.createElement(*merged, UI::makeLabelElement("First"));
    auto automatic = updater.createElement(*merged, UI::makePanelElement());
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(automatic.has_value()) << automatic.error().message;
    auto last = updater.createElement(*automatic, UI::makeLabelElement("Last"));
    ASSERT_TRUE(last.has_value()) << last.error().message;

    UI::UIElementDescriptor excludedDescriptor = UI::makePanelElement();
    excludedDescriptor.semantics.mode = UI::UISemanticsMode::Exclude;
    auto excluded = updater.createElement(*merged, excludedDescriptor);
    ASSERT_TRUE(excluded.has_value()) << excluded.error().message;
    auto hidden = updater.createElement(*excluded, UI::makeLabelElement("Hidden"));
    ASSERT_TRUE(hidden.has_value()) << hidden.error().message;

    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    const UI::UICommittedSemanticsView semantics = context->committedSemantics();
    ASSERT_EQ(semantics.size(), 1U);
    EXPECT_EQ(semantics.entries().front().node, *merged);
    EXPECT_EQ(semantics.entries().front().role, UI::UISemanticsRole::Group);
    EXPECT_EQ(semantics.entries().front().name, "Account First Last");
    EXPECT_EQ(findSemanticsEntry(semantics, *first), nullptr);
    EXPECT_EQ(findSemanticsEntry(semantics, *last), nullptr);
    EXPECT_EQ(findSemanticsEntry(semantics, *excluded), nullptr);
    EXPECT_EQ(findSemanticsEntry(semantics, *hidden), nullptr);
}

TEST_F(UIElementTest, InvalidOrOversizedSemanticsTextRollsBackNodeAndStorage)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .textByteCapacity = 4,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    const usize liveNodesBeforeCreate = context->liveNodeCount();

    constexpr std::array<char, 2> InvalidUtf8{static_cast<char>(0xC3), '('};
    UI::UIElementDescriptor invalidDescriptor = UI::makePanelElement();
    invalidDescriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .name = std::string_view(InvalidUtf8.data(), InvalidUtf8.size()),
    };
    const auto invalid = updater.createElement(root.rootNodeId(), invalidDescriptor);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, UI::UIErrorCode::InvalidText);
    EXPECT_EQ(context->liveNodeCount(), liveNodesBeforeCreate);
    EXPECT_EQ(context->statistics().textByteUsed, 0U);

    UI::UIElementDescriptor oversizedDescriptor = UI::makePanelElement();
    oversizedDescriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .name = "12345",
    };
    const auto oversized = updater.createElement(root.rootNodeId(), oversizedDescriptor);
    ASSERT_FALSE(oversized.has_value());
    EXPECT_EQ(oversized.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), liveNodesBeforeCreate);
    EXPECT_EQ(context->statistics().textByteUsed, 0U);

    UI::UIElementDescriptor replacementDescriptor = UI::makePanelElement();
    replacementDescriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .name = "1234",
    };
    const auto replacement = updater.createElement(root.rootNodeId(), replacementDescriptor);
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    EXPECT_EQ(context->liveNodeCount(), liveNodesBeforeCreate + 1U);
    EXPECT_EQ(context->statistics().textByteUsed, 4U);
}

TEST_F(UIElementTest, StyleRoleAndPropertyOverridesTrackTheActiveThemeIndependently)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
        },
        true);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    auto button = updater.createElement(root.rootNodeId(), UI::makeButtonElement("Delete"));
    ASSERT_TRUE(button.has_value()) << button.error().message;

    ASSERT_TRUE(updater.setStyleRole(*button, UI::UIStyleRoleId::ButtonDanger).has_value());
    ASSERT_EQ(updater.styleRole(*button).value(), UI::UIStyleRoleId::ButtonDanger);
    EXPECT_EQ(
        updater.buttonPaint(*button).value(),
        UI::makeButtonChrome(context->productTheme(), context->productTheme().danger).states);

    const UI::UIButtonPaint localStates{
        .hoveredBackgroundColor = UI::rgb(0xAA2200),
        .pressedBackgroundColor = UI::rgb(0x771100),
        .focusedBackgroundColor = UI::rgb(0xCC4400),
        .disabledBackgroundColor = UI::rgb(0x555555),
        .focusedBorderColor = UI::rgb(0xFFFFFF),
    };
    assertOk(updater.setButtonPaint(*button, localStates));
    assertOk(context->setProductTheme(UI::makeLightProductTheme()));
    EXPECT_EQ(updater.buttonPaint(*button).value(), localStates);

    assertOk(updater.clearOverride(*button, UI::UIStyleOverride::ButtonPaint));
    EXPECT_EQ(updater.styleRole(*button).value(), UI::UIStyleRoleId::ButtonDanger);
    EXPECT_EQ(
        updater.buttonPaint(*button).value(),
        UI::makeButtonChrome(context->productTheme(), context->productTheme().danger).states);
}

TEST_F(UIElementTest, StyleRoleCapacityFailureLeavesRoleAndChromeUntouched)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
        },
        true);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    auto first = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
    auto second = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;
    assertOk(context->commitLayout({.width = 240.0F, .height = 120.0F}));

    const UI::UIButtonPaint originalSecondPaint = updater.buttonPaint(*second).value();
    UI::UIButtonPaint firstOverride = updater.buttonPaint(*first).value();
    firstOverride.pressedBackgroundColor = UI::rgb(0x010203);
    assertOk(updater.setButtonPaint(*first, firstOverride));

    const Core::Status rejected = updater.setStyleRole(*second, UI::UIStyleRoleId::ButtonDanger);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(updater.styleRole(*second).value(), UI::UIStyleRoleId::ButtonPrimary);
    EXPECT_EQ(updater.buttonPaint(*second).value(), originalSecondPaint);
}

TEST_F(UIElementTest, BuildTransactionPublishesOnlyAfterSuccessfulBoundedConstruction)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 16,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 16,
            .paintSnapshotCapacity = 16,
            .textByteCapacity = 128,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    auto transactionResult = updater.beginBuildTransaction(root.rootNodeId(), UI::makePanelElement(), 3);
    ASSERT_TRUE(transactionResult.has_value()) << transactionResult.error().message;
    UI::UIElementBuildTransaction transaction = std::move(*transactionResult);
    ASSERT_TRUE(transaction.isActive());
    EXPECT_EQ(transaction.remainingNodeBudget(), 2U);

    const Core::Status structureWhileActive = context->commitStructure();
    ASSERT_FALSE(structureWhileActive.has_value());
    EXPECT_EQ(structureWhileActive.error().code, UI::UIErrorCode::BuildTransactionInProgress);
    const Core::Status layoutWhileActive = context->commitLayout({.width = 320.0F, .height = 180.0F});
    ASSERT_FALSE(layoutWhileActive.has_value());
    EXPECT_EQ(layoutWhileActive.error().code, UI::UIErrorCode::BuildTransactionInProgress);
    EXPECT_TRUE(context->committedStructure().empty());

    auto label = transaction.createElement(transaction.rootNodeId(), UI::makeLabelElement("Title"));
    ASSERT_TRUE(label.has_value()) << label.error().message;
    auto button = transaction.createElement(transaction.rootNodeId(), UI::makeButtonElement("Apply"));
    ASSERT_TRUE(button.has_value()) << button.error().message;
    EXPECT_EQ(transaction.remainingNodeBudget(), 0U);

    auto committedComponent = transaction.commit();
    ASSERT_TRUE(committedComponent.has_value()) << committedComponent.error().message;
    EXPECT_FALSE(transaction.isActive());
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(context->committedStructure().size(), 4U);
    EXPECT_NE(findLayoutEntry(context->committedLayout(), *committedComponent), nullptr);
    EXPECT_NE(findLayoutEntry(context->committedLayout(), *label), nullptr);
    EXPECT_NE(findHitEntry(context->committedHit(), *button), nullptr);
}

TEST_F(UIElementTest, BuildTransactionFailureAndDestructionRollbackTheWholeSubtree)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 12,
            .rootCapacity = 1,
            .textByteCapacity = 64,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    const usize baselineNodes = context->liveNodeCount();

    auto exhaustedResult = updater.beginBuildTransaction(root.rootNodeId(), UI::makePanelElement(), 2);
    ASSERT_TRUE(exhaustedResult.has_value()) << exhaustedResult.error().message;
    UI::UIElementBuildTransaction exhausted = std::move(*exhaustedResult);
    ASSERT_TRUE(exhausted.createElement(exhausted.rootNodeId(), UI::makePanelElement()).has_value());
    const auto overBudget = exhausted.createElement(exhausted.rootNodeId(), UI::makePanelElement());
    ASSERT_FALSE(overBudget.has_value());
    EXPECT_EQ(overBudget.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(exhausted.isActive());
    EXPECT_EQ(context->liveNodeCount(), baselineNodes);
    const auto poisonedCommit = exhausted.commit();
    ASSERT_FALSE(poisonedCommit.has_value());
    EXPECT_EQ(poisonedCommit.error().code, UI::UIErrorCode::CapacityExceeded);

    auto invalidChildResult = updater.beginBuildTransaction(root.rootNodeId(), UI::makePanelElement(), 2);
    ASSERT_TRUE(invalidChildResult.has_value()) << invalidChildResult.error().message;
    UI::UIElementBuildTransaction invalidChild = std::move(*invalidChildResult);
    UI::UIElementDescriptor invalidToggle{
        .text = "invalid",
        .behaviors = UI::UIElementBehavior::Focusable | UI::UIElementBehavior::Activate |
                     UI::UIElementBehavior::Toggle,
    };
    const auto invalidCreate = invalidChild.createElement(invalidChild.rootNodeId(), invalidToggle);
    ASSERT_FALSE(invalidCreate.has_value());
    EXPECT_EQ(invalidCreate.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    EXPECT_EQ(context->liveNodeCount(), baselineNodes);

    {
        auto abandonedResult = updater.beginBuildTransaction(root.rootNodeId(), UI::makePanelElement(), 2);
        ASSERT_TRUE(abandonedResult.has_value()) << abandonedResult.error().message;
        UI::UIElementBuildTransaction abandoned = std::move(*abandonedResult);
        ASSERT_TRUE(abandoned.createElement(abandoned.rootNodeId(), UI::makePanelElement()).has_value());
        EXPECT_EQ(context->liveNodeCount(), baselineNodes + 2U);
    }
    EXPECT_EQ(context->liveNodeCount(), baselineNodes);
    assertOk(context->commitLayout({.width = 160.0F, .height = 90.0F}));
    EXPECT_EQ(context->committedStructure().size(), baselineNodes);
}

TEST_F(UIElementTest, BuildTransactionBudgetIncludesVirtualCollectionRowPools)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 16,
            .rootCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    const UI::UIElementDescriptor listDescriptor = UI::makeListViewElement({
        .materializedItemCapacity = 3,
    });
    const auto undersized = updater.beginBuildTransaction(root.rootNodeId(), listDescriptor, 3);
    ASSERT_FALSE(undersized.has_value());
    EXPECT_EQ(undersized.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), 1U);

    auto listResult = updater.beginBuildTransaction(root.rootNodeId(), listDescriptor, 4);
    ASSERT_TRUE(listResult.has_value()) << listResult.error().message;
    UI::UIElementBuildTransaction list = std::move(*listResult);
    EXPECT_EQ(list.remainingNodeBudget(), 0U);
    ASSERT_TRUE(list.commit().has_value());
    EXPECT_EQ(context->liveNodeCount(), 5U);

    const UI::UIElementDescriptor treeDescriptor = UI::makeTreeViewElement({
        .materializedItemCapacity = 2,
    });
    auto treeResult = updater.beginBuildTransaction(root.rootNodeId(), treeDescriptor, 3);
    ASSERT_TRUE(treeResult.has_value()) << treeResult.error().message;
    UI::UIElementBuildTransaction tree = std::move(*treeResult);
    EXPECT_EQ(tree.remainingNodeBudget(), 0U);
    ASSERT_TRUE(tree.commit().has_value());
    EXPECT_EQ(context->liveNodeCount(), 8U);
}

TEST_F(UIElementTest, CanvasCommandsAreCopiedAndPaintAfterTheElementBoxInLocalOrder)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 8,
            .paintSnapshotCapacity = 8,
            .canvasCommandCapacity = 4,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    std::array<UI::UICanvasCommand, 2> commands{
        UI::UICanvasCommand{
            .bounds = {.x = 2.0F, .y = 3.0F, .width = 5.0F, .height = 6.0F},
            .color = UI::rgb(0xFF0000),
            .cornerRadius = 2.5F,
        },
        UI::UICanvasCommand{
            .bounds = {.x = 35.0F, .y = 25.0F, .width = 10.0F, .height = 10.0F},
            .color = UI::rgb(0x0000FF),
        },
    };
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedOverlay(10.0F, 15.0F, 40.0F, 30.0F));
    descriptor.visual.boxPaint = UI::makeSolidBox(UI::rgb(0x00FF00));
    descriptor.visual.canvas = commands;
    auto element = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(element.has_value()) << element.error().message;

    commands[0] = UI::UICanvasCommand{
        .bounds = {.x = 100.0F, .y = 100.0F, .width = 1.0F, .height = 1.0F},
        .color = UI::rgb(0x000000),
    };
    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));

    const UI::UICommittedLayoutEntry* layout = findLayoutEntry(context->committedLayout(), *element);
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->worldRect, (UI::UILogicalRect{.x = 10.0F, .y = 15.0F, .width = 40.0F, .height = 30.0F}));

    std::array<const UI::UICommittedPaintEntry*, 3> elementPaints{};
    usize paintCount = 0;
    for (const UI::UICommittedPaintEntry& entry : context->committedPaint())
    {
        if (entry.node == *element && paintCount < elementPaints.size())
        {
            elementPaints[paintCount++] = &entry;
        }
    }
    ASSERT_EQ(paintCount, elementPaints.size());
    ASSERT_NE(elementPaints[0], nullptr);
    ASSERT_NE(elementPaints[1], nullptr);
    ASSERT_NE(elementPaints[2], nullptr);
    EXPECT_EQ(elementPaints[0]->worldRect, layout->worldRect);
    EXPECT_EQ(elementPaints[0]->solidFill, UI::premultiply(UI::rgb(0x00FF00)));
    EXPECT_EQ(
        elementPaints[1]->worldRect,
        (UI::UILogicalRect{.x = 12.0F, .y = 18.0F, .width = 5.0F, .height = 6.0F}));
    EXPECT_EQ(elementPaints[1]->solidFill, UI::premultiply(UI::rgb(0xFF0000)));
    EXPECT_FLOAT_EQ(elementPaints[1]->cornerRadius, 2.5F);
    EXPECT_EQ(
        elementPaints[2]->worldRect,
        (UI::UILogicalRect{.x = 45.0F, .y = 40.0F, .width = 10.0F, .height = 10.0F}));
    EXPECT_EQ(elementPaints[2]->solidFill, UI::premultiply(UI::rgb(0x0000FF)));
    EXPECT_EQ(elementPaints[1]->effectiveClip, layout->worldRect);
    EXPECT_EQ(elementPaints[2]->effectiveClip, layout->worldRect);
    EXPECT_LT(elementPaints[0]->paintOrdinal, elementPaints[1]->paintOrdinal);
    EXPECT_LT(elementPaints[1]->paintOrdinal, elementPaints[2]->paintOrdinal);

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.canvasCommandCapacity, 4U);
    EXPECT_EQ(statistics.activeCanvasCommandCount, 2U);
    EXPECT_EQ(statistics.canvasCommandHighWater, 2U);
}

TEST_F(UIElementTest, CanvasCapacityValidationDestroyAndTransactionRollbackRecycleSlots)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 8,
            .canvasCommandCapacity = 2,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);

    const std::array<UI::UICanvasCommand, 3> tooManyCommands{
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}, .color = UI::rgb(0x111111)},
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}, .color = UI::rgb(0x222222)},
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}, .color = UI::rgb(0x333333)},
    };
    UI::UIElementDescriptor tooMany = UI::makePanelElement();
    tooMany.visual.canvas = tooManyCommands;
    const auto exhausted = updater.createElement(root.rootNodeId(), tooMany);
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);

    const UI::UICanvasCommand invalidRadiusCommand{
        .bounds = {.width = 1.0F, .height = 1.0F},
        .color = UI::rgb(0xFFFFFF),
        .cornerRadius = (std::numeric_limits<float>::quiet_NaN)(),
    };
    UI::UIElementDescriptor invalidRadius = UI::makePanelElement();
    invalidRadius.visual.canvas = std::span<const UI::UICanvasCommand>(&invalidRadiusCommand, 1);
    const auto invalidRadiusResult = updater.createElement(root.rootNodeId(), invalidRadius);
    ASSERT_FALSE(invalidRadiusResult.has_value());
    EXPECT_EQ(invalidRadiusResult.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);

    const UI::UICanvasCommand unsupportedCommand{
        .kind = static_cast<UI::UICanvasCommandKind>(255),
        .bounds = {.width = 1.0F, .height = 1.0F},
        .color = UI::rgb(0xFFFFFF),
    };
    UI::UIElementDescriptor invalid = UI::makePanelElement();
    invalid.visual.canvas = std::span<const UI::UICanvasCommand>(&unsupportedCommand, 1);
    const auto invalidResult = updater.createElement(root.rootNodeId(), invalid);
    ASSERT_FALSE(invalidResult.has_value());
    EXPECT_EQ(invalidResult.error().code, UI::UIErrorCode::InvalidElementDescriptor);
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);

    const std::array<UI::UICanvasCommand, 2> validCommands{
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}, .color = UI::rgb(0xAA0000)},
        UI::UICanvasCommand{.bounds = {.width = 1.0F, .height = 1.0F}, .color = UI::rgb(0x00AA00)},
    };
    UI::UIElementDescriptor valid = UI::makePanelElement();
    valid.visual.canvas = validCommands;
    auto retained = updater.createElement(root.rootNodeId(), valid);
    ASSERT_TRUE(retained.has_value()) << retained.error().message;
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 2U);
    assertOk(updater.destroy(*retained));
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);

    UI::UIElementDescriptor oneCommand = UI::makePanelElement();
    oneCommand.visual.canvas = std::span<const UI::UICanvasCommand>(validCommands.data(), 1);
    auto transactionResult = updater.beginBuildTransaction(root.rootNodeId(), oneCommand, 2);
    ASSERT_TRUE(transactionResult.has_value()) << transactionResult.error().message;
    UI::UIElementBuildTransaction transaction = std::move(*transactionResult);
    ASSERT_TRUE(transaction.createElement(transaction.rootNodeId(), oneCommand).has_value());
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 2U);
    transaction.reset();
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);

    auto replacement = updater.createElement(root.rootNodeId(), valid);
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.activeCanvasCommandCount, 2U);
    EXPECT_EQ(statistics.canvasCommandHighWater, 2U);
}

} // namespace
} // namespace Tina::Tests
