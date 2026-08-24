#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIInputRouter.hpp>
#include <tina/ui/UIMotionController.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>
#include <tina/ui/UIStyleController.hpp>
#include <tina/ui/UITheme.hpp>

#include "../../src/runtime/ui/PrimaryWindowUICapabilityState.hpp"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using CapabilityState = Runtime::Detail::PrimaryWindowUICapabilityState;
using CapabilityPhase = Runtime::Detail::PrimaryWindowUIPhase;
using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
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
        .color =
            {
                .red = red,
                .green = green,
                .blue = blue,
                .alpha = alpha,
            },
    };
    return paint;
}

[[nodiscard]] bool hasPaintFill(UI::UICommittedPaintView view, UI::UINodeId node,
                                UI::UIPremultipliedRgba8Color color) noexcept
{
    for (const UI::UICommittedPaintEntry& entry : view.entries())
    {
        if (entry.node == node && entry.kind == UI::UICommittedPaintKind::SolidQuad &&
            entry.solidFill == color)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UIIconContent runtimeIconContent()
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x51};
    return {
        .source = {
            .texture = *Core::AssetId::fromBytes(bytes),
            .sourcePixels = {.width = 16, .height = 16},
            .texturePixelExtent = {.width = 16, .height = 16},
            .intrinsicLogicalSize = {.width = 16.0F, .height = 16.0F},
        },
    };
}

[[nodiscard]] UI::UIButtonActionCallback buttonAction(usize& activationCount) noexcept
{
    return UI::UIButtonActionCallback{
        [&activationCount](const UI::UIButtonActionEvent&) noexcept { ++activationCount; }};
}

Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
unavailableImageResolver(void*, Core::AssetId, Render::FrameResourceSink&) noexcept
{
    return std::optional<Render::Texture2DFrameResourceResolution>{};
}

[[nodiscard]] constexpr Render::Texture2DFrameResourceResolver imageResolver(void* userData = nullptr) noexcept
{
    return {
        .userData = userData,
        .resolve = &unavailableImageResolver,
    };
}

struct ListFacadeDataSource final {
    u64 count = 0;
    UI::UIListViewItemKey keyBase = 1;

    [[nodiscard]] UI::UIListViewDataSource view() const noexcept
    {
        return {
            .state = this,
            .itemCount = &itemCount,
            .resolveItem = &resolveItem,
        };
    }

    static u64 itemCount(const void* state) noexcept
    {
        return static_cast<const ListFacadeDataSource*>(state)->count;
    }

    static bool resolveItem(const void* state, u64 logicalIndex, UI::UIListViewItemDescriptor& output) noexcept
    {
        const auto& source = *static_cast<const ListFacadeDataSource*>(state);
        if (logicalIndex >= source.count)
        {
            return false;
        }
        output = {
            .key = source.keyBase + logicalIndex,
            .label = "Item",
            .enabled = true,
        };
        return true;
    }
};

struct TreeFacadeDataSource final {
    static constexpr u64 CollapsedItemCount = 20;

    bool expanded = false;
    usize expansionCallCount = 0;
    UI::UITreeViewItemKey lastExpansionKey = UI::InvalidUITreeViewItemKey;

    [[nodiscard]] UI::UITreeViewDataSource view() noexcept
    {
        return {
            .state = this,
            .itemCount = &itemCount,
            .resolveItem = &resolveItem,
            .setItemExpanded = &setItemExpanded,
        };
    }

    static u64 itemCount(const void* state) noexcept
    {
        return static_cast<const TreeFacadeDataSource*>(state)->expanded ? CollapsedItemCount + 2
                                                                         : CollapsedItemCount;
    }

    static bool resolveItem(const void* state, u64 logicalIndex, UI::UITreeViewItemDescriptor& output) noexcept
    {
        const auto& source = *static_cast<const TreeFacadeDataSource*>(state);
        if (logicalIndex >= itemCount(state))
        {
            return false;
        }
        if (logicalIndex == 0)
        {
            output = {
                .key = 1,
                .label = "Workspace",
                .level = 0,
                .enabled = true,
                .expandable = true,
                .expanded = source.expanded,
            };
            return true;
        }
        if (source.expanded && logicalIndex <= 2)
        {
            output = {
                .key = 100 + logicalIndex,
                .label = logicalIndex == 1 ? "Scene.cpp" : "Theme.json",
                .level = 1,
                .enabled = true,
            };
            return true;
        }
        const u64 rootKey = source.expanded ? logicalIndex - 1 : logicalIndex + 1;
        output = {
            .key = rootKey,
            .label = "Root item",
            .level = 0,
            .enabled = true,
        };
        return true;
    }

    static bool setItemExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept
    {
        auto& source = *static_cast<TreeFacadeDataSource*>(state);
        if (key != 1)
        {
            return false;
        }
        source.expanded = expanded;
        source.lastExpansionKey = key;
        ++source.expansionCallCount;
        return true;
    }
};

struct DataGridFacadeDataSource final {
    u64 rows = 0;
    u32 columns = 2;
    UI::UIDataGridRowKey rowKeyBase = 1;
    UI::UIDataGridColumnKey columnKeyBase = 1'000;

    [[nodiscard]] UI::UIDataGridDataSource view() const noexcept
    {
        return {
            .state = this,
            .rowCount = &rowCount,
            .columnCount = &columnCount,
            .resolveRow = &resolveRow,
            .resolveColumn = &resolveColumn,
            .resolveCell = &resolveCell,
        };
    }

    static u64 rowCount(const void* state) noexcept
    {
        return static_cast<const DataGridFacadeDataSource*>(state)->rows;
    }

    static u32 columnCount(const void* state) noexcept
    {
        return static_cast<const DataGridFacadeDataSource*>(state)->columns;
    }

    static bool resolveRow(const void* state, u64 logicalRow,
                           UI::UIDataGridRowDescriptor& output) noexcept
    {
        const auto& source =
            *static_cast<const DataGridFacadeDataSource*>(state);
        if (logicalRow >= source.rows)
        {
            return false;
        }
        output = {
            .key = source.rowKeyBase + logicalRow,
            .enabled = true,
        };
        return true;
    }

    static bool resolveColumn(const void* state, u32 logicalColumn,
                              UI::UIDataGridColumnDescriptor& output) noexcept
    {
        const auto& source =
            *static_cast<const DataGridFacadeDataSource*>(state);
        constexpr std::array<std::string_view, 2> Headers{"Name", "State"};
        constexpr std::array<float, 2> Widths{70.0F, 90.0F};
        if (logicalColumn >= source.columns ||
            logicalColumn >= Headers.size())
        {
            return false;
        }
        output = {
            .key = source.columnKeyBase + logicalColumn,
            .header = Headers[logicalColumn],
            .width = Widths[logicalColumn],
        };
        return true;
    }

    static bool resolveCell(const void* state, u64 logicalRow,
                            u32 logicalColumn,
                            UI::UIDataGridCellDescriptor& output) noexcept
    {
        const auto& source =
            *static_cast<const DataGridFacadeDataSource*>(state);
        constexpr std::array<std::string_view, 2> Cells{"Asset", "Ready"};
        if (logicalRow >= source.rows || logicalColumn >= source.columns ||
            logicalColumn >= Cells.size())
        {
            return false;
        }
        output = {.text = Cells[logicalColumn]};
        return true;
    }
};

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
    auto panel = tree->createElement(root->rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    auto label = tree->createElement(*panel, UI::makeLabelElement());
    ASSERT_TRUE(label.has_value()) << label.error().message;
    auto button = tree->createElement(*panel, UI::makeButtonElement());
    ASSERT_TRUE(button.has_value()) << button.error().message;
    EXPECT_EQ(context->liveRootCount(), 1U);
    EXPECT_EQ(context->liveNodeCount(), 4U);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    EXPECT_FALSE(state.hasPrimaryWindowUI(*epoch, CapabilityPhase::GameStateEnter));

    auto expiredTree = tree->createElement(*panel, UI::makePanelElement());
    ASSERT_FALSE(expiredTree.has_value());
    EXPECT_EQ(expiredTree.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredBuilder = builder->createRoot();
    ASSERT_FALSE(expiredBuilder.has_value());
    EXPECT_EQ(expiredBuilder.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, StyleSheetFacadeRegistersClassTokenAndInstallsBeforeRootCreation)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;

    std::optional<Core::Result<UI::UIStyleClassId>> crossThread;
    std::thread worker([&] { crossThread.emplace(builder->registerStyleClass()); });
    worker.join();
    ASSERT_TRUE(crossThread.has_value());
    ASSERT_FALSE(crossThread->has_value());
    EXPECT_EQ(crossThread->error().code, RuntimeErrorCode::WrongOwnerThread);

    auto styleClassResult = builder->registerStyleClass();
    ASSERT_TRUE(styleClassResult.has_value()) << styleClassResult.error().message;
    const UI::UIStyleClassId styleClass = *styleClassResult;
    auto colorTokenResult = builder->registerStyleColorToken(UI::rgb(0x2357A6));
    ASSERT_TRUE(colorTokenResult.has_value()) << colorTokenResult.error().message;
    UI::UIStyleBoxFillRule rule{
        .role = UI::UIStyleRoleId::PanelSurface,
        .styleClass = styleClass,
        .colorToken = *colorTokenResult,
    };
    ASSERT_TRUE(builder->installStyleSheet(std::span(&rule, 1)).has_value());
    rule.colorToken = {};
    rule.color = UI::rgb(0xB42318);
    const UI::UIStyleStatistics published = context->statistics().style;
    EXPECT_EQ(published.registeredClassCount, 1U);
    EXPECT_EQ(published.registeredTokenCount, 1U);
    EXPECT_EQ(published.activeRuleCount, 1U);
    EXPECT_EQ(published.activeBucketCount, 1U);
    EXPECT_EQ(published.revision, 1U);

    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    UI::UIElementDescriptor descriptor =
        UI::makeButtonElement({}, fixedSize(80.0F, 40.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    descriptor.visual.styleClasses = std::span(&styleClass, 1);
    auto panel = tree->createElement(root->rootNodeId(), descriptor);
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    EXPECT_EQ(context->statistics().style.activeNodeClassLinkCount, 1U);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *panel,
                             UI::premultiply(UI::rgb(0x2357A6))));
    EXPECT_FALSE(hasPaintFill(context->publication().committedPaint(), *panel,
                              UI::premultiply(UI::rgb(0xB42318))));

    auto expiredClass = builder->registerStyleClass();
    ASSERT_FALSE(expiredClass.has_value());
    EXPECT_EQ(expiredClass.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredToken = builder->registerStyleColorToken(UI::rgb(0xFFFFFF));
    ASSERT_FALSE(expiredToken.has_value());
    EXPECT_EQ(expiredToken.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSheet =
        builder->installStyleSheet(std::span<const UI::UIStyleBoxFillRule>{});
    ASSERT_FALSE(expiredSheet.has_value());
    EXPECT_EQ(expiredSheet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, StyleRegistrationAfterRootCreationIsSticky)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;

    const usize registeredBefore = context->statistics().style.registeredClassCount;
    const usize registeredTokensBefore = context->statistics().style.registeredTokenCount;
    auto lateClass = builder->registerStyleClass();
    ASSERT_FALSE(lateClass.has_value());
    EXPECT_EQ(lateClass.error().code, UI::UIErrorCode::InvalidStyle);
    EXPECT_EQ(context->statistics().style.registeredClassCount, registeredBefore);

    auto lateToken = builder->registerStyleColorToken(UI::rgb(0xFFFFFF));
    ASSERT_FALSE(lateToken.has_value());
    EXPECT_EQ(lateToken.error().code, lateClass.error().code);
    EXPECT_EQ(lateToken.error().message, lateClass.error().message);
    EXPECT_EQ(context->statistics().style.registeredTokenCount,
              registeredTokensBefore);

    Core::Status otherwiseValid =
        builder->installStyleSheet(std::span<const UI::UIStyleBoxFillRule>{});
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, lateClass.error().code);
    EXPECT_EQ(otherwiseValid.error().message, lateClass.error().message);

    Core::Status finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidStyle);
}

TEST_F(PrimaryWindowUICapabilityTest, StyleColorTokenFacadeRoundTripsAndExpiresWithPhase)
{
    constexpr UI::UIStraightSrgba8Color InitialColor = UI::rgb(0x2357A6);
    constexpr UI::UIStraightSrgba8Color UpdatedColor = UI::rgb(0x3978C5);

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto token = builder->registerStyleColorToken(InitialColor);
    ASSERT_TRUE(token.has_value()) << token.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    auto tree = state.treeUpdater(*updateEpoch, CapabilityPhase::UIUpdate, *root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    std::optional<Core::Result<UI::UIStraightSrgba8Color>> crossThread;
    std::thread worker([&] { crossThread.emplace(tree->styleColorToken(*token)); });
    worker.join();
    ASSERT_TRUE(crossThread.has_value());
    ASSERT_FALSE(crossThread->has_value());
    EXPECT_EQ(crossThread->error().code, RuntimeErrorCode::WrongOwnerThread);

    auto initial = tree->styleColorToken(*token);
    ASSERT_TRUE(initial.has_value()) << initial.error().message;
    EXPECT_EQ(*initial, InitialColor);
    ASSERT_TRUE(tree->setStyleColorToken(*token, UpdatedColor).has_value());
    auto updated = tree->styleColorToken(*token);
    ASSERT_TRUE(updated.has_value()) << updated.error().message;
    EXPECT_EQ(*updated, UpdatedColor);

    ASSERT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
    auto expiredGet = tree->styleColorToken(*token);
    ASSERT_FALSE(expiredGet.has_value());
    EXPECT_EQ(expiredGet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->setStyleColorToken(*token, InitialColor);
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, MotionFacadeBeginsBackgroundTransitionAndExpires)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto enterTree = builder->treeUpdater(*root);
    ASSERT_TRUE(enterTree.has_value()) << enterTree.error().message;
    UI::UIElementDescriptor panel = UI::makePanelElement();
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    panel.layout.size.width = UI::UILayoutLength::Px(40.0F);
    panel.layout.size.height = UI::UILayoutLength::Px(30.0F);
    const auto node = enterTree->createElement(root->rootNodeId(), panel);
    ASSERT_TRUE(node.has_value()) << node.error().message;
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    auto tree = state.treeUpdater(*updateEpoch, CapabilityPhase::UIUpdate, *root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    auto reduced = tree->reducedMotion();
    ASSERT_TRUE(reduced.has_value()) << reduced.error().message;
    EXPECT_FALSE(*reduced);
    ASSERT_TRUE(tree->setReducedMotion(false).has_value());

    UI::UITransitionSpec spec{
        .property = UI::UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.100},
        .easing = UI::UIEasing::EaseOut,
    };
    ASSERT_TRUE(tree->setStyleBackgroundColorTransition(spec).has_value());
    auto configuredStyleTransition = tree->styleBackgroundColorTransition();
    ASSERT_TRUE(configuredStyleTransition.has_value()) << configuredStyleTransition.error().message;
    EXPECT_EQ(configuredStyleTransition->duration, spec.duration);
    EXPECT_EQ(configuredStyleTransition->easing, spec.easing);
    ASSERT_TRUE(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}).has_value());
    ASSERT_TRUE(tree->beginBackgroundColorTransition(*node, UI::rgba8(100, 0, 0, 255), spec).has_value());
    EXPECT_EQ(context->statistics().motion.activeTrackCount, 1U);
    EXPECT_TRUE(context->statistics().paintDirty);
    EXPECT_FALSE(context->statistics().layoutDirty);
    EXPECT_FALSE(context->statistics().hitDirty);

    ASSERT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
    Core::Status expired = tree->beginBackgroundColorTransition(*node, UI::rgba8(0, 100, 0, 255), spec);
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredStyle = tree->setStyleBackgroundColorTransition(spec);
    ASSERT_FALSE(expiredStyle.has_value());
    EXPECT_EQ(expiredStyle.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredStyleGet = tree->styleBackgroundColorTransition();
    ASSERT_FALSE(expiredStyleGet.has_value());
    EXPECT_EQ(expiredStyleGet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, TimelineFacadeOwnsDefinitionPlaybackAndExpiry)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto enterTree = builder->treeUpdater(*root);
    ASSERT_TRUE(enterTree.has_value()) << enterTree.error().message;
    UI::UIElementDescriptor panel = UI::makePanelElement();
    panel.visual.boxPaint = UI::makeSolidBox(UI::rgba8(0, 0, 0, 255));
    const auto node = enterTree->createElement(root->rootNodeId(), panel);
    ASSERT_TRUE(node.has_value()) << node.error().message;
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    auto tree = state.treeUpdater(*updateEpoch, CapabilityPhase::UIUpdate, *root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    const std::array keyframes{
        UI::UIKeyframe{.normalizedTime = 0.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(0, 0, 0, 255))},
        UI::UIKeyframe{.normalizedTime = 1.0F,
                       .value = UI::UIKeyframeValue::Color(UI::rgba8(100, 0, 0, 255))},
    };
    const std::array tracks{
        UI::UITimelineTrackDesc{
            .node = *node,
            .property = UI::UIAnimatableProperty::BackgroundColor,
            .keyframes = keyframes,
        },
    };
    auto timeline = tree->createTimeline(UI::UITimelineDesc{
        .duration = Core::Duration{0.1},
        .tracks = tracks,
    });
    ASSERT_TRUE(timeline.has_value()) << timeline.error().message;
    ASSERT_TRUE(tree->playTimeline(*timeline).has_value());
    auto active = tree->isTimelineActive(*timeline);
    ASSERT_TRUE(active.has_value()) << active.error().message;
    EXPECT_TRUE(*active);
    ASSERT_TRUE(tree->cancelTimeline(*timeline).has_value());
    ASSERT_TRUE(tree->destroyTimeline(*timeline).has_value());

    ASSERT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
    auto expired = tree->isTimelineActive(*timeline);
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, InvalidStyleColorTokenFailureIsSticky)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto token = builder->registerStyleColorToken(UI::rgb(0x2357A6));
    ASSERT_TRUE(token.has_value()) << token.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    auto invalid = tree->styleColorToken(UI::UIStyleTokenId{});
    ASSERT_FALSE(invalid.has_value());

    Core::Status otherwiseValid =
        tree->setStyleColorToken(*token, UI::rgb(0x3978C5));
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, invalid.error().code);
    EXPECT_EQ(otherwiseValid.error().message, invalid.error().message);

    Core::Status finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, invalid.error().code);
    EXPECT_EQ(finish.error().message, invalid.error().message);
}

TEST_F(PrimaryWindowUICapabilityTest, FailedStyleSheetInstallPreservesActiveSheetAndSticksError)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto styleClass = builder->registerStyleClass();
    ASSERT_TRUE(styleClass.has_value()) << styleClass.error().message;

    const UI::UIStyleBoxFillRule validRule{
        .role = UI::UIStyleRoleId::PanelSurface,
        .styleClass = *styleClass,
        .color = UI::rgb(0x136F63),
    };
    ASSERT_TRUE(builder->installStyleSheet(std::span(&validRule, 1)).has_value());
    const UI::UIStyleStatistics published = context->statistics().style;
    ASSERT_EQ(published.activeRuleCount, 1U);
    ASSERT_EQ(published.revision, 1U);

    const UI::UIStyleBoxFillRule invalidRule{
        .role = UI::UIStyleRoleId::PanelSurface,
        .styleClass = UI::UIStyleClassId{
            static_cast<u32>(published.registeredClassCount + 1U)},
        .color = UI::rgb(0xB42318),
    };
    Core::Status failedInstall =
        builder->installStyleSheet(std::span(&invalidRule, 1));
    ASSERT_FALSE(failedInstall.has_value());
    EXPECT_EQ(failedInstall.error().code, UI::UIErrorCode::InvalidStyle);

    const UI::UIStyleStatistics afterFailure = context->statistics().style;
    EXPECT_EQ(afterFailure.registeredClassCount, published.registeredClassCount);
    EXPECT_EQ(afterFailure.activeRuleCount, published.activeRuleCount);
    EXPECT_EQ(afterFailure.activeBucketCount, published.activeBucketCount);
    EXPECT_EQ(afterFailure.revision, published.revision);

    auto otherwiseValid = builder->registerStyleClass();
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, failedInstall.error().code);
    EXPECT_EQ(otherwiseValid.error().message, failedInstall.error().message);
    EXPECT_EQ(context->statistics().style.registeredClassCount,
              published.registeredClassCount);

    Core::Status finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidStyle);
}

TEST_F(PrimaryWindowUICapabilityTest, BuildTransactionCommitsBoundedComponentDuringEnterPhase)
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

    auto transactionResult =
        tree->beginBuildTransaction(
            root->rootNodeId(), UI::makePanelElement(),
            {.nodes = 4, .textBytes = 22, .canvasCommands = 1,
             .behaviors = {.activate = 1}});
    ASSERT_TRUE(transactionResult.has_value()) << transactionResult.error().message;
    PrimaryWindowUIBuildTransaction transaction = std::move(*transactionResult);
    const UI::UINodeId componentRoot = transaction.rootNodeId();
    ASSERT_TRUE(componentRoot.hasValue());
    EXPECT_EQ(transaction.remainingBudget(),
              (UI::UIComponentBuildBudget{
                  .nodes = 3, .textBytes = 22, .canvasCommands = 1,
                  .behaviors = {.activate = 1}}));

    auto label = transaction.createElement(componentRoot, UI::makeLabelElement("Runtime component"));
    auto button = transaction.createElement(componentRoot, UI::makeButtonElement("Apply"));
    ASSERT_TRUE(label.has_value()) << label.error().message;
    ASSERT_TRUE(button.has_value()) << button.error().message;
    const UI::UICanvasCommand canvasCommand{
        .bounds = {.width = 8.0F, .height = 8.0F},
        .color = UI::rgb(0x2C7A7B),
    };
    UI::UIElementDescriptor canvasDescriptor = UI::makePanelElement();
    canvasDescriptor.visual.canvas = std::span(&canvasCommand, 1);
    auto canvas = transaction.createElement(componentRoot, canvasDescriptor);
    ASSERT_TRUE(canvas.has_value()) << canvas.error().message;
    EXPECT_EQ(transaction.remainingBudget(), UI::UIComponentBuildBudget{});

    auto committed = transaction.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(*committed, componentRoot);
    EXPECT_FALSE(transaction.isActive());
    EXPECT_EQ(context->liveNodeCount(), 5U);
    EXPECT_GT(context->statistics().textByteUsed, 0U);
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 1U);
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    EXPECT_EQ(context->liveNodeCount(), 5U);
}

TEST_F(PrimaryWindowUICapabilityTest, ComponentProfilesHonorRuntimePhaseLifetime)
{
    auto contextResult = UI::UIContext::Create(
        window, {.nodeCapacity = 18, .rootCapacity = 4});
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    auto iconButton = tree->buildIconButton(
        root->rootNodeId(),
        UI::UIIconButtonConfig{
            .icon = runtimeIconContent(),
            .accessibleName = "Refresh",
            .tooltipText = "Reload",
        });
    ASSERT_TRUE(iconButton.has_value()) << iconButton.error().message;

    auto formField = tree->buildFormField(
        root->rootNodeId(),
        UI::UIFormFieldConfig{
            .label = "Name",
            .value = "Tina",
            .helperText = "Project name",
        });
    ASSERT_TRUE(formField.has_value()) << formField.error().message;

    const std::array actions{
        UI::UIDialogActionConfig{
            .text = "Close", .variant = UI::UIButtonVariant::Primary},
    };
    auto dialog = tree->buildDialog(
        root->rootNodeId(),
        UI::UIDialogConfig{
            .title = "Settings",
            .body = "Changes are applied immediately.",
            .actions = actions,
        });
    ASSERT_TRUE(dialog.has_value()) << dialog.error().message;
    EXPECT_EQ(context->liveNodeCount(), 18U);
    EXPECT_EQ(context->statistics().activeImageContentCount, 1U);
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    const auto expired = tree->buildFormField(
        root->rootNodeId(),
        UI::UIFormFieldConfig{.label = "Expired", .value = {}});
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    EXPECT_EQ(context->liveNodeCount(), 18U);
}

TEST_F(PrimaryWindowUICapabilityTest, ComponentProfileRejectsConcurrentBuildTransaction)
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

    auto transaction = tree->beginBuildTransaction(
        root->rootNodeId(), UI::makePanelElement(), {.nodes = 1});
    ASSERT_TRUE(transaction.has_value()) << transaction.error().message;
    const auto rejected = tree->buildIconButton(
        root->rootNodeId(),
        UI::UIIconButtonConfig{
            .icon = runtimeIconContent(),
            .accessibleName = "Refresh",
        });
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::BuildTransactionInProgress);
    EXPECT_TRUE(transaction->isActive());
    transaction->reset();
    EXPECT_EQ(context->liveNodeCount(), 1U);

    const Core::Status finish =
        state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::BuildTransactionInProgress);
}

TEST_F(PrimaryWindowUICapabilityTest, BuildTransactionCommitsDuringUIUpdatePhase)
{
    CapabilityState state;
    auto enterEpoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(enterEpoch.has_value()) << enterEpoch.error().message;
    auto builder = state.rootBuilder(*enterEpoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    ASSERT_TRUE(state.finishPhase(*enterEpoch, CapabilityPhase::GameStateEnter).has_value());

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    auto tree = state.treeUpdater(*updateEpoch, CapabilityPhase::UIUpdate, *root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto transaction = tree->beginBuildTransaction(
        root->rootNodeId(), UI::makePanelElement(),
        {.nodes = 2, .textBytes = 5});
    ASSERT_TRUE(transaction.has_value()) << transaction.error().message;
    auto label = transaction->createElement(transaction->rootNodeId(), UI::makeLabelElement("Frame"));
    ASSERT_TRUE(label.has_value()) << label.error().message;
    ASSERT_TRUE(transaction->commit().has_value());
    EXPECT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
    EXPECT_EQ(context->liveNodeCount(), 3U);
}

TEST_F(PrimaryWindowUICapabilityTest, BuildTransactionBudgetFailureRollsBackRetainedStorage)
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

    const UI::UICanvasCommand canvasCommand{
        .bounds = {.width = 4.0F, .height = 4.0F},
        .color = UI::rgb(0x336699),
    };
    UI::UIElementDescriptor componentDescriptor = UI::makePanelElement();
    componentDescriptor.visual.canvas = std::span(&canvasCommand, 1);
    auto transaction = tree->beginBuildTransaction(
        root->rootNodeId(), componentDescriptor,
        {.nodes = 2, .textBytes = 9, .canvasCommands = 1});
    ASSERT_TRUE(transaction.has_value()) << transaction.error().message;
    auto label = transaction->createElement(transaction->rootNodeId(), UI::makeLabelElement("Temporary"));
    ASSERT_TRUE(label.has_value()) << label.error().message;
    EXPECT_GT(context->statistics().textByteUsed, 0U);
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 1U);

    auto exhausted = transaction->createElement(transaction->rootNodeId(), UI::makePanelElement());
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(transaction->isActive());
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_EQ(context->statistics().textByteUsed, 0U);
    EXPECT_EQ(context->statistics().activeCanvasCommandCount, 0U);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::CapacityExceeded);
}

TEST_F(PrimaryWindowUICapabilityTest, PhaseFinishRollsBackEscapedBuildTransaction)
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
    auto transaction = tree->beginBuildTransaction(
        root->rootNodeId(), UI::makePanelElement(),
        {.nodes = 2, .textBytes = 7});
    ASSERT_TRUE(transaction.has_value()) << transaction.error().message;
    ASSERT_TRUE(transaction->createElement(
        transaction->rootNodeId(), UI::makeLabelElement("Escaped")).has_value());
    EXPECT_EQ(context->liveNodeCount(), 3U);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::BuildTransactionInProgress);
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_EQ(context->statistics().textByteUsed, 0U);
    EXPECT_FALSE(transaction->isActive());
    auto expired = transaction->commit();
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    EXPECT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, MovedBuildTransactionResetRollsBackExactlyOnce)
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
    auto transactionResult = tree->beginBuildTransaction(
        root->rootNodeId(), UI::makePanelElement(), {.nodes = 1});
    ASSERT_TRUE(transactionResult.has_value()) << transactionResult.error().message;
    PrimaryWindowUIBuildTransaction transaction = std::move(*transactionResult);
    PrimaryWindowUIBuildTransaction moved = std::move(transaction);
    EXPECT_FALSE(transaction.isActive());
    EXPECT_TRUE(moved.isActive());
    EXPECT_EQ(context->liveNodeCount(), 2U);

    transaction.reset();
    EXPECT_EQ(context->liveNodeCount(), 2U);
    moved.reset();
    EXPECT_EQ(context->liveNodeCount(), 1U);
    moved.reset();
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, AbandonedBuildTransactionDestructorRollsBackWithoutPoisoningPhase)
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

    {
        auto transaction = tree->beginBuildTransaction(
            root->rootNodeId(), UI::makePanelElement(),
            {.nodes = 2, .textBytes = 9});
        ASSERT_TRUE(transaction.has_value()) << transaction.error().message;
        ASSERT_TRUE(transaction->createElement(
            transaction->rootNodeId(), UI::makeLabelElement("Abandoned")).has_value());
        EXPECT_EQ(context->liveNodeCount(), 3U);
    }

    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_EQ(context->statistics().textByteUsed, 0U);
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, BuildTransactionRejectsForeignParentBeforeMutation)
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
    const usize liveNodesBefore = context->liveNodeCount();

    auto foreignParent = firstTree->beginBuildTransaction(
        secondRoot->rootNodeId(), UI::makePanelElement(), {.nodes = 1});
    ASSERT_FALSE(foreignParent.has_value());
    EXPECT_EQ(foreignParent.error().code, UI::UIErrorCode::InvalidNode);
    EXPECT_EQ(context->liveNodeCount(), liveNodesBefore);
    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(PrimaryWindowUICapabilityTest, ImageResolverRegistrationIsRootScopedMoveOnlyAndGenerationSafe)
{
    int firstUserData = 0;
    int replacementUserData = 0;
    CapabilityState state{2};
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto firstRoot = builder->createRoot();
    ASSERT_TRUE(firstRoot.has_value()) << firstRoot.error().message;
    auto otherRoot = builder->createRoot();
    ASSERT_TRUE(otherRoot.has_value()) << otherRoot.error().message;

    auto firstRegistration = builder->bindImageResolver(*firstRoot, imageResolver(&firstUserData));
    ASSERT_TRUE(firstRegistration.has_value()) << firstRegistration.error().message;
    ASSERT_TRUE(firstRegistration->isActive());
    const auto* boundResolver = state.findImageResolver(firstRoot->rootNodeId());
    ASSERT_NE(boundResolver, nullptr);
    EXPECT_EQ(boundResolver->userData, &firstUserData);
    EXPECT_EQ(boundResolver->resolve, &unavailableImageResolver);
    EXPECT_EQ(state.findImageResolver(otherRoot->rootNodeId()), nullptr);

    PrimaryWindowUIImageResolverRegistration movedRegistration = std::move(*firstRegistration);
    EXPECT_FALSE(firstRegistration->isActive());
    EXPECT_TRUE(movedRegistration.isActive());
    movedRegistration.reset();
    EXPECT_FALSE(movedRegistration.isActive());
    EXPECT_EQ(state.findImageResolver(firstRoot->rootNodeId()), nullptr);

    auto replacement = builder->bindImageResolver(*firstRoot, imageResolver(&replacementUserData));
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    firstRegistration->reset();
    boundResolver = state.findImageResolver(firstRoot->rootNodeId());
    ASSERT_NE(boundResolver, nullptr);
    EXPECT_EQ(boundResolver->userData, &replacementUserData);

    replacement->reset();
    EXPECT_EQ(state.findImageResolver(firstRoot->rootNodeId()), nullptr);
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, ImageResolverRegistrationRejectsDuplicateRootsAndCapacityOverflow)
{
    CapabilityState duplicateState{2};
    auto duplicateEpoch = duplicateState.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(duplicateEpoch.has_value()) << duplicateEpoch.error().message;
    auto duplicateBuilder = duplicateState.rootBuilder(*duplicateEpoch);
    ASSERT_TRUE(duplicateBuilder.has_value()) << duplicateBuilder.error().message;
    auto duplicateRoot = duplicateBuilder->createRoot();
    ASSERT_TRUE(duplicateRoot.has_value()) << duplicateRoot.error().message;
    auto registration = duplicateBuilder->bindImageResolver(*duplicateRoot, imageResolver());
    ASSERT_TRUE(registration.has_value()) << registration.error().message;
    auto duplicate = duplicateBuilder->bindImageResolver(*duplicateRoot, imageResolver());
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, Core::CoreErrorCode::InvalidArgument);
    registration->reset();
    duplicateState.abortPhase(*duplicateEpoch, CapabilityPhase::GameStateEnter);

    CapabilityState limitedState{1};
    auto limitedEpoch = limitedState.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(limitedEpoch.has_value()) << limitedEpoch.error().message;
    auto limitedBuilder = limitedState.rootBuilder(*limitedEpoch);
    ASSERT_TRUE(limitedBuilder.has_value()) << limitedBuilder.error().message;
    auto firstRoot = limitedBuilder->createRoot();
    ASSERT_TRUE(firstRoot.has_value()) << firstRoot.error().message;
    auto secondRoot = limitedBuilder->createRoot();
    ASSERT_TRUE(secondRoot.has_value()) << secondRoot.error().message;
    auto firstRegistration = limitedBuilder->bindImageResolver(*firstRoot, imageResolver());
    ASSERT_TRUE(firstRegistration.has_value()) << firstRegistration.error().message;
    auto overflow = limitedBuilder->bindImageResolver(*secondRoot, imageResolver());
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Core::CoreErrorCode::CapacityExceeded);
    firstRegistration->reset();
    limitedState.abortPhase(*limitedEpoch, CapabilityPhase::GameStateEnter);
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
    auto textEdit = tree->createElement(root->rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(textEdit.has_value()) << textEdit.error().message;
    ASSERT_TRUE(tree->setText(*textEdit, "Tina").has_value());

    constexpr UI::UIContentAlignment Alignment{
        .horizontal = UI::UIAxisAlignment::End,
        .vertical = UI::UIAxisAlignment::Center,
    };
    ASSERT_TRUE(tree->setContentAlignment(*textEdit, Alignment).has_value());
    constexpr UI::UITextSelection Selection{.anchorCodepoint = 1, .caretCodepoint = 3};
    ASSERT_TRUE(tree->setTextSelection(*textEdit, Selection).has_value());
    constexpr UI::UITextEditPaint Paint{
        .hoveredBackgroundColor = UI::rgb(0x315170),
        .pressedBackgroundColor = UI::rgb(0x102030),
        .focusedBackgroundColor = UI::rgb(0x406080),
        .disabledBackgroundColor = UI::rgb(0x202020),
        .selectionBackgroundColor = UI::rgb(0x1266AA, 210),
        .caretColor = UI::rgb(0xF2C94C),
    };
    ASSERT_TRUE(tree->setTextEditPaint(*textEdit, Paint).has_value());
    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto currentAlignment = treeView.contentAlignment(*textEdit);
    ASSERT_TRUE(currentAlignment.has_value()) << currentAlignment.error().message;
    EXPECT_EQ(*currentAlignment, Alignment);
    auto currentSelection = treeView.textSelection(*textEdit);
    ASSERT_TRUE(currentSelection.has_value()) << currentSelection.error().message;
    EXPECT_EQ(*currentSelection, Selection);
    auto currentPaint = treeView.textEditPaint(*textEdit);
    ASSERT_TRUE(currentPaint.has_value()) << currentPaint.error().message;
    EXPECT_EQ(*currentPaint, Paint);
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto expiredCreate = tree->createElement(root->rootNodeId(), UI::makeTextEditElement());
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->setTextSelection(*textEdit, {});
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredPaintSet = tree->setTextEditPaint(*textEdit, {});
    ASSERT_FALSE(expiredPaintSet.has_value());
    EXPECT_EQ(expiredPaintSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredAlignmentSet = tree->setContentAlignment(*textEdit, {});
    ASSERT_FALSE(expiredAlignmentSet.has_value());
    EXPECT_EQ(expiredAlignmentSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredAlignmentQuery = treeView.contentAlignment(*textEdit);
    ASSERT_FALSE(expiredAlignmentQuery.has_value());
    EXPECT_EQ(expiredAlignmentQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = treeView.textSelection(*textEdit);
    ASSERT_FALSE(expiredQuery.has_value());
    EXPECT_EQ(expiredQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredPaintQuery = treeView.textEditPaint(*textEdit);
    ASSERT_FALSE(expiredPaintQuery.has_value());
    EXPECT_EQ(expiredPaintQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, TextPresentationFacadeRoundTripsAndExpiresWithPhase)
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
    auto label = tree->createElement(root->rootNodeId(), UI::makeLabelElement("A long label"));
    ASSERT_TRUE(label.has_value()) << label.error().message;

    auto wrapMode = tree->textWrapMode(*label);
    ASSERT_TRUE(wrapMode.has_value()) << wrapMode.error().message;
    EXPECT_EQ(*wrapMode, UI::UITextWrapMode::Words);
    ASSERT_TRUE(tree->setTextWrapMode(*label, UI::UITextWrapMode::NoWrap).has_value());
    ASSERT_TRUE(tree->setTextOverflow(*label, UI::UITextOverflow::Ellipsis).has_value());
    auto overflow = tree->textOverflow(*label);
    ASSERT_TRUE(overflow.has_value()) << overflow.error().message;
    EXPECT_EQ(*overflow, UI::UITextOverflow::Ellipsis);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    Core::Status expiredSet = tree->setTextOverflow(*label, UI::UITextOverflow::Clip);
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredGet = tree->textOverflow(*label);
    ASSERT_FALSE(expiredGet.has_value());
    EXPECT_EQ(expiredGet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredWrapSet =
        tree->setTextWrapMode(*label, UI::UITextWrapMode::Words);
    ASSERT_FALSE(expiredWrapSet.has_value());
    EXPECT_EQ(expiredWrapSet.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredWrapGet = tree->textWrapMode(*label);
    ASSERT_FALSE(expiredWrapGet.has_value());
    EXPECT_EQ(expiredWrapGet.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
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
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
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

TEST_F(PrimaryWindowUICapabilityTest, ModalAndFocusFacadesRoundTripAndExpireWithPhase)
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

    auto panel = tree->createElement(root->rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    ASSERT_TRUE(tree->setFocusScopeMode(*panel, UI::UIFocusScopeMode::Contain).has_value());
    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto scopeMode = treeView.focusScopeMode(*panel);
    ASSERT_TRUE(scopeMode.has_value()) << scopeMode.error().message;
    EXPECT_EQ(*scopeMode, UI::UIFocusScopeMode::Contain);

    auto modal = tree->createElement(root->rootNodeId(), UI::makeModalElement());
    ASSERT_TRUE(modal.has_value()) << modal.error().message;
    auto modalButton = tree->createElement(*modal, UI::makeButtonElement());
    ASSERT_TRUE(modalButton.has_value()) << modalButton.error().message;
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 90.0F}).has_value());
    EXPECT_EQ(context->input().activeModal(), *modal);
    EXPECT_EQ(context->input().defaultActionFocus(), *modalButton);
    ASSERT_TRUE(tree->requestFocus(*modalButton).has_value());
    ASSERT_TRUE(tree->clearFocus().has_value());
    EXPECT_FALSE(context->input().defaultActionFocus().hasValue());

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    auto expiredModal = tree->createElement(root->rootNodeId(), UI::makeModalElement());
    ASSERT_FALSE(expiredModal.has_value());
    EXPECT_EQ(expiredModal.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredRequest = tree->requestFocus(*modalButton);
    ASSERT_FALSE(expiredRequest.has_value());
    EXPECT_EQ(expiredRequest.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredClear = tree->clearFocus();
    ASSERT_FALSE(expiredClear.has_value());
    EXPECT_EQ(expiredClear.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredScope = treeView.focusScopeMode(*panel);
    ASSERT_FALSE(expiredScope.has_value());
    EXPECT_EQ(expiredScope.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, DialogPresentationFacadeRoundTripsAndExpiresWithPhase)
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

    const std::array actions{
        UI::UIDialogActionConfig{
            .text = "Close",
            .variant = UI::UIButtonVariant::Primary,
        },
    };
    auto dialog = tree->buildDialog(
        root->rootNodeId(),
        UI::UIDialogConfig{
            .title = "Runtime dialog",
            .actions = actions,
        });
    ASSERT_TRUE(dialog.has_value()) << dialog.error().message;

    auto initiallyOpen = tree->isDialogOpen(dialog->modal);
    ASSERT_TRUE(initiallyOpen.has_value()) << initiallyOpen.error().message;
    EXPECT_FALSE(*initiallyOpen);
    ASSERT_TRUE(tree->openDialog(dialog->modal).has_value());
    EXPECT_TRUE(tree->isDialogOpen(dialog->modal).value());
    ASSERT_TRUE(tree->dismissDialog(dialog->modal).has_value());
    EXPECT_FALSE(tree->isDialogOpen(dialog->modal).value());

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    Core::Status expiredOpen = tree->openDialog(dialog->modal);
    ASSERT_FALSE(expiredOpen.has_value());
    EXPECT_EQ(expiredOpen.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredDismiss = tree->dismissDialog(dialog->modal);
    ASSERT_FALSE(expiredDismiss.has_value());
    EXPECT_EQ(expiredDismiss.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = tree->isDialogOpen(dialog->modal);
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
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    auto initialTheme = tree->productTheme();
    ASSERT_TRUE(initialTheme.has_value()) << initialTheme.error().message;
    EXPECT_EQ(*initialTheme, UI::makeModernDesktopTheme());

    ASSERT_TRUE(tree->setStyleRole(*button, UI::UIStyleRoleId::ButtonDanger).has_value());
    EXPECT_EQ(tree->styleRole(*button).value(), UI::UIStyleRoleId::ButtonDanger);
    EXPECT_EQ(
        tree->buttonPaint(*button).value(),
        UI::makeButtonChrome(UI::makeModernDesktopTheme(), UI::makeModernDesktopTheme().colors.error).states);

    const UI::UIButtonPaint localPaint{};
    ASSERT_TRUE(tree->setButtonPaint(*button, localPaint).has_value());
    ASSERT_TRUE(tree->setProductTheme(UI::makeModernDesktopTheme(UI::UIColorScheme::Light)).has_value());
    EXPECT_EQ(tree->productTheme().value(), UI::makeModernDesktopTheme(UI::UIColorScheme::Light));
    EXPECT_EQ(tree->buttonPaint(*button).value(), localPaint);
    ASSERT_TRUE(tree->clearOverride(*button, UI::UIStyleOverride::ButtonPaint).has_value());
    EXPECT_EQ(
        tree->buttonPaint(*button).value(),
        UI::makeButtonChrome(UI::makeModernDesktopTheme(UI::UIColorScheme::Light),
                             UI::makeModernDesktopTheme(UI::UIColorScheme::Light).colors.error).states);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    Core::Status expiredSet = tree->setProductTheme(UI::makeModernDesktopTheme());
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredGet = tree->productTheme();
    ASSERT_FALSE(expiredGet.has_value());
    EXPECT_EQ(expiredGet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredRoleSet = tree->setStyleRole(*button, UI::UIStyleRoleId::ButtonPrimary);
    ASSERT_FALSE(expiredRoleSet.has_value());
    EXPECT_EQ(expiredRoleSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredRole = tree->styleRole(*button);
    ASSERT_FALSE(expiredRole.has_value());
    EXPECT_EQ(expiredRole.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredClearOverride = tree->clearOverride(*button);
    ASSERT_FALSE(expiredClearOverride.has_value());
    EXPECT_EQ(expiredClearOverride.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, ScrollViewFacadeRoundTripsMetricsAndExpiresWithPhase)
{
    constexpr UI::UIScrollViewStyle ScrollStyle{
        .axes = UI::UIScrollAxes::Vertical,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        .wheelStep = 32.0F,
    };
    constexpr UI::UIScrollViewPaint ScrollPaint{
        .trackColor = {.red = 20, .green = 30, .blue = 40, .alpha = 255},
        .thumbColor = {.red = 80, .green = 100, .blue = 120, .alpha = 255},
        .draggingThumbColor = {.red = 240, .green = 180, .blue = 40, .alpha = 255},
        .thickness = 10.0F,
        .minThumbExtent = 24.0F,
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
    auto scrollView = tree->createElement(root->rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(scrollView.has_value()) << scrollView.error().message;
    auto content = tree->createElement(*scrollView, UI::makePanelElement());
    ASSERT_TRUE(content.has_value()) << content.error().message;

    ASSERT_TRUE(tree->setLayoutStyle(root->rootNodeId(), fixedSize(100.0F, 100.0F)).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*scrollView, fixedSize(100.0F, 100.0F)).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*content, fixedSize(100.0F, 200.0F)).has_value());
    ASSERT_TRUE(tree->setScrollViewStyle(*scrollView, ScrollStyle).has_value());
    ASSERT_TRUE(tree->setScrollViewPaint(*scrollView, ScrollPaint).has_value());
    ASSERT_TRUE(tree->setScrollViewOffset(*scrollView, {.x = 0.0F, .y = 40.0F}).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}).has_value());

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto style = treeView.scrollViewStyle(*scrollView);
    auto paint = treeView.scrollViewPaint(*scrollView);
    auto offset = treeView.scrollViewOffset(*scrollView);
    auto metrics = treeView.scrollViewMetrics(*scrollView);
    auto dragging = treeView.isScrollViewDragging(*scrollView);
    ASSERT_TRUE(style.has_value()) << style.error().message;
    ASSERT_TRUE(paint.has_value()) << paint.error().message;
    ASSERT_TRUE(offset.has_value()) << offset.error().message;
    ASSERT_TRUE(metrics.has_value()) << metrics.error().message;
    ASSERT_TRUE(dragging.has_value()) << dragging.error().message;
    EXPECT_EQ(*style, ScrollStyle);
    EXPECT_EQ(*paint, ScrollPaint);
    EXPECT_EQ(*offset, (UI::UIScrollOffset{.x = 0.0F, .y = 40.0F}));
    EXPECT_EQ(metrics->offset, *offset);
    EXPECT_FLOAT_EQ(metrics->maxOffsetY(), 100.0F);
    EXPECT_FALSE(*dragging);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    auto expiredCreate = tree->createElement(root->rootNodeId(), UI::makeScrollViewElement());
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredMetrics = treeView.scrollViewMetrics(*scrollView);
    ASSERT_FALSE(expiredMetrics.has_value());
    EXPECT_EQ(expiredMetrics.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->setScrollViewOffset(*scrollView, {});
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, ListViewFacadeRoundTripsAndExpiresWithPhase)
{
    constexpr u32 MaterializedItemCapacity = 12;
    constexpr UI::UIListViewStyle ListStyle{
        .rowHeight = 24.0F,
        .overscanRows = 1,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Always,
        .wheelStep = 20.0F,
    };
    constexpr UI::UIListViewPaint ListPaint{
        .scrollBar =
            {
                .trackColor = {.red = 24, .green = 30, .blue = 40, .alpha = 255},
                .thumbColor = {.red = 80, .green = 100, .blue = 140, .alpha = 255},
                .draggingThumbColor = {.red = 120, .green = 150, .blue = 220, .alpha = 255},
                .thickness = 9.0F,
                .minThumbExtent = 18.0F,
            },
        .selectedItemBackgroundColor = {.red = 36, .green = 92, .blue = 168, .alpha = 220},
    };

    auto contextResult = UI::UIContext::Create(window, {
                                                           .nodeCapacity = 128,
                                                           .rootCapacity = 4,
                                                           .paintSnapshotCapacity = 128,
                                                       });
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    ListFacadeDataSource source{.count = 100, .keyBase = 1'000};
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto listView = tree->createElement(
        root->rootNodeId(),
        UI::makeListViewElement({.materializedItemCapacity = MaterializedItemCapacity}));
    ASSERT_TRUE(listView.has_value()) << listView.error().message;

    ASSERT_TRUE(tree->setLayoutStyle(root->rootNodeId(), fixedSize(120.0F, 80.0F)).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*listView, fixedSize(120.0F, 80.0F)).has_value());
    ASSERT_TRUE(tree->setListViewStyle(*listView, ListStyle).has_value());
    ASSERT_TRUE(tree->setListViewPaint(*listView, ListPaint).has_value());
    ASSERT_TRUE(tree->setListViewDataSource(*listView, source.view()).has_value());
    ASSERT_TRUE(tree->invalidateListViewItems(*listView).has_value());
    ASSERT_TRUE(tree->setListViewSelectedIndex(*listView, 3).has_value());
    ASSERT_TRUE(tree->scrollListViewToIndex(*listView, 40, UI::UIListViewScrollAlignment::Start).has_value());
    Core::Status initialCommit = context->publication().commitLayout({.width = 120.0F, .height = 80.0F});
    ASSERT_TRUE(initialCommit.has_value()) << initialCommit.error().message;

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    EXPECT_EQ(treeView.listViewStyle(*listView).value(), ListStyle);
    EXPECT_EQ(treeView.listViewPaint(*listView).value(), ListPaint);
    const UI::UIListViewMetrics metrics = treeView.listViewMetrics(*listView).value();
    EXPECT_EQ(metrics.logicalItemCount, 100U);
    EXPECT_EQ(metrics.materializedItemCapacity, MaterializedItemCapacity);
    EXPECT_EQ(metrics.firstVisibleIndex, 40U);
    EXPECT_EQ(treeView.listViewSelection(*listView).value(),
              (UI::UIListViewSelection{.key = 1'003, .logicalIndex = 3}));

    ASSERT_TRUE(tree->clearListViewSelection(*listView).has_value());
    EXPECT_FALSE(treeView.listViewSelection(*listView).value().hasValue());
    ASSERT_TRUE(tree->clearListViewDataSource(*listView).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 120.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(treeView.listViewMetrics(*listView).value().logicalItemCount, 0U);
    ASSERT_TRUE(tree->setListViewDataSource(*listView, source.view()).has_value());
    ASSERT_TRUE(tree->invalidateListViewItems(*listView).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 120.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(treeView.listViewMetrics(*listView).value().logicalItemCount, 100U);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    auto expiredCreate = tree->createElement(root->rootNodeId(), UI::makeListViewElement({.materializedItemCapacity = 4}));
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->clearListViewDataSource(*listView);
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = treeView.listViewMetrics(*listView);
    ASSERT_FALSE(expiredQuery.has_value());
    EXPECT_EQ(expiredQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest,
       DataGridFacadeRoundTripsAndExpiresWithPhase)
{
    constexpr u32 ColumnCapacity = 2;
    constexpr u32 MaterializedRowCapacity = 12;
    constexpr UI::UIDataGridStyle GridStyle{
        .columnHeaderHeight = 20.0F,
        .rowHeight = 20.0F,
        .overscanRows = 1,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Always,
        .wheelStep = 20.0F,
    };
    constexpr UI::UIDataGridPaint GridPaint{
        .scrollBar =
            {
                .trackColor = {.red = 24, .green = 30, .blue = 40, .alpha = 255},
                .thumbColor = {.red = 80, .green = 100, .blue = 140, .alpha = 255},
                .draggingThumbColor = {.red = 120, .green = 150, .blue = 220, .alpha = 255},
                .thickness = 9.0F,
                .minThumbExtent = 18.0F,
            },
        .columnHeaderBackgroundColor = {.red = 28, .green = 36, .blue = 48, .alpha = 255},
        .selectedRowBackgroundColor = {.red = 36, .green = 92, .blue = 168, .alpha = 220},
        .hoveredSelectedRowBackgroundColor = {.red = 44, .green = 104, .blue = 184, .alpha = 220},
        .focusedSelectedRowBackgroundColor = {.red = 52, .green = 116, .blue = 200, .alpha = 220},
        .gridLineColor = {.red = 72, .green = 82, .blue = 96, .alpha = 255},
    };

    auto contextResult = UI::UIContext::Create(
        window,
        {
            .nodeCapacity = 192,
            .rootCapacity = 4,
            .paintSnapshotCapacity = 256,
        });
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    DataGridFacadeDataSource source{
        .rows = 100,
        .columns = 2,
        .rowKeyBase = 2'000,
        .columnKeyBase = 3'000,
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
    auto dataGrid = tree->createElement(
        root->rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = ColumnCapacity,
            .materializedRowCapacity = MaterializedRowCapacity,
        }));
    ASSERT_TRUE(dataGrid.has_value()) << dataGrid.error().message;

    ASSERT_TRUE(tree->setLayoutStyle(
                        root->rootNodeId(), fixedSize(120.0F, 80.0F))
                    .has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*dataGrid, fixedSize(120.0F, 80.0F))
                    .has_value());
    ASSERT_TRUE(tree->setDataGridStyle(*dataGrid, GridStyle).has_value());
    ASSERT_TRUE(tree->setDataGridPaint(*dataGrid, GridPaint).has_value());
    ASSERT_TRUE(tree->setDataGridDataSource(*dataGrid, source.view()).has_value());
    ASSERT_TRUE(tree->invalidateDataGridItems(*dataGrid).has_value());
    ASSERT_TRUE(tree->setDataGridSelectedCell(*dataGrid, 3, 1).has_value());
    ASSERT_TRUE(tree->scrollDataGridToCell(
                        *dataGrid, 40, 1,
                        UI::UIDataGridScrollAlignment::Start)
                    .has_value());
    Core::Status initialCommit =
        context->publication().commitLayout({.width = 120.0F, .height = 80.0F});
    ASSERT_TRUE(initialCommit.has_value()) << initialCommit.error().message;

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    EXPECT_EQ(treeView.dataGridStyle(*dataGrid).value(), GridStyle);
    EXPECT_EQ(treeView.dataGridPaint(*dataGrid).value(), GridPaint);
    const UI::UIDataGridMetrics metrics =
        treeView.dataGridMetrics(*dataGrid).value();
    EXPECT_EQ(metrics.logicalRowCount, 100U);
    EXPECT_EQ(metrics.logicalColumnCount, ColumnCapacity);
    EXPECT_EQ(metrics.materializedRowCapacity, MaterializedRowCapacity);
    EXPECT_EQ(metrics.firstVisibleRow, 40U);
    EXPECT_EQ(treeView.dataGridSelection(*dataGrid).value(),
              (UI::UIDataGridSelection{
                  .rowKey = 2'003,
                  .columnKey = 3'001,
                  .logicalRow = 3,
                  .logicalColumn = 1,
              }));

    ASSERT_TRUE(tree->clearDataGridSelection(*dataGrid).has_value());
    EXPECT_FALSE(treeView.dataGridSelection(*dataGrid).value().hasValue());
    ASSERT_TRUE(tree->clearDataGridDataSource(*dataGrid).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 120.0F, .height = 80.0F})
                    .has_value());
    EXPECT_EQ(treeView.dataGridMetrics(*dataGrid).value().logicalRowCount, 0U);
    ASSERT_TRUE(tree->setDataGridDataSource(*dataGrid, source.view()).has_value());
    ASSERT_TRUE(tree->invalidateDataGridItems(*dataGrid).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 120.0F, .height = 80.0F})
                    .has_value());
    EXPECT_EQ(treeView.dataGridMetrics(*dataGrid).value().logicalRowCount, 100U);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter)
                    .has_value());
    auto expiredCreate = tree->createElement(
        root->rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = 1,
            .materializedRowCapacity = 2,
        }));
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->clearDataGridDataSource(*dataGrid);
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = treeView.dataGridMetrics(*dataGrid);
    ASSERT_FALSE(expiredQuery.has_value());
    EXPECT_EQ(expiredQuery.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, TreeViewFacadeRoundTripsExpansionAndExpiresWithPhase)
{
    constexpr u32 MaterializedItemCapacity = 12;
    constexpr UI::UITreeViewStyle TreeStyle{
        .rowHeight = 24.0F,
        .overscanRows = 1,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Always,
        .wheelStep = 20.0F,
        .indentation = 16.0F,
        .disclosureExtent = 10.0F,
        .disclosureGap = 5.0F,
    };
    constexpr UI::UITreeViewPaint TreePaint{
        .scrollBar =
            {
                .trackColor = {.red = 22, .green = 28, .blue = 38, .alpha = 255},
                .thumbColor = {.red = 88, .green = 110, .blue = 150, .alpha = 255},
                .draggingThumbColor = {.red = 130, .green = 160, .blue = 225, .alpha = 255},
                .thickness = 9.0F,
                .minThumbExtent = 18.0F,
            },
        .selectedItemBackgroundColor = {.red = 42, .green = 96, .blue = 176, .alpha = 220},
        .disclosureColor = {.red = 224, .green = 232, .blue = 244, .alpha = 255},
    };

    auto contextResult = UI::UIContext::Create(window, {
                                                           .nodeCapacity = 128,
                                                           .rootCapacity = 4,
                                                           .paintSnapshotCapacity = 128,
                                                       });
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    TreeFacadeDataSource source;
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto treeViewNode = tree->createElement(
        root->rootNodeId(),
        UI::makeTreeViewElement({.materializedItemCapacity = MaterializedItemCapacity}));
    ASSERT_TRUE(treeViewNode.has_value()) << treeViewNode.error().message;

    ASSERT_TRUE(tree->setLayoutStyle(root->rootNodeId(), fixedSize(160.0F, 80.0F)).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*treeViewNode, fixedSize(160.0F, 80.0F)).has_value());
    ASSERT_TRUE(tree->setTreeViewStyle(*treeViewNode, TreeStyle).has_value());
    ASSERT_TRUE(tree->setTreeViewPaint(*treeViewNode, TreePaint).has_value());
    ASSERT_TRUE(tree->setTreeViewDataSource(*treeViewNode, source.view()).has_value());
    ASSERT_TRUE(tree->invalidateTreeViewItems(*treeViewNode).has_value());
    ASSERT_TRUE(tree->setTreeViewSelectedIndex(*treeViewNode, 0).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    EXPECT_EQ(treeView.treeViewStyle(*treeViewNode).value(), TreeStyle);
    EXPECT_EQ(treeView.treeViewPaint(*treeViewNode).value(), TreePaint);
    EXPECT_EQ(treeView.treeViewMetrics(*treeViewNode).value().logicalItemCount,
              TreeFacadeDataSource::CollapsedItemCount);
    EXPECT_EQ(treeView.treeViewMetrics(*treeViewNode).value().materializedItemCapacity, MaterializedItemCapacity);
    EXPECT_EQ(treeView.treeViewSelection(*treeViewNode).value(),
              (UI::UITreeViewSelection{.key = 1, .logicalIndex = 0, .level = 0}));

    ASSERT_TRUE(tree->setTreeViewItemExpanded(*treeViewNode, 0, true).has_value());
    EXPECT_TRUE(source.expanded);
    EXPECT_EQ(source.expansionCallCount, 1U);
    EXPECT_EQ(source.lastExpansionKey, 1U);
    ASSERT_TRUE(tree->setTreeViewSelectedIndex(*treeViewNode, 3).has_value());
    ASSERT_TRUE(tree->scrollTreeViewToIndex(*treeViewNode, 15, UI::UITreeViewScrollAlignment::Start).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    const UI::UITreeViewMetrics expandedMetrics = treeView.treeViewMetrics(*treeViewNode).value();
    EXPECT_EQ(expandedMetrics.logicalItemCount, TreeFacadeDataSource::CollapsedItemCount + 2);
    EXPECT_EQ(expandedMetrics.firstVisibleIndex, 15U);
    EXPECT_EQ(treeView.treeViewSelection(*treeViewNode).value(),
              (UI::UITreeViewSelection{.key = 2, .logicalIndex = 3, .level = 0}));

    ASSERT_TRUE(tree->clearTreeViewSelection(*treeViewNode).has_value());
    EXPECT_FALSE(treeView.treeViewSelection(*treeViewNode).value().hasValue());
    ASSERT_TRUE(tree->clearTreeViewDataSource(*treeViewNode).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(treeView.treeViewMetrics(*treeViewNode).value().logicalItemCount, 0U);
    ASSERT_TRUE(tree->setTreeViewDataSource(*treeViewNode, source.view()).has_value());
    ASSERT_TRUE(tree->invalidateTreeViewItems(*treeViewNode).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 80.0F}).has_value());
    EXPECT_EQ(treeView.treeViewMetrics(*treeViewNode).value().logicalItemCount,
              TreeFacadeDataSource::CollapsedItemCount + 2);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    auto expiredCreate = tree->createElement(root->rootNodeId(), UI::makeTreeViewElement({.materializedItemCapacity = 4}));
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->setTreeViewItemExpanded(*treeViewNode, 0, false);
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredQuery = treeView.treeViewMetrics(*treeViewNode);
    ASSERT_FALSE(expiredQuery.has_value());
    EXPECT_EQ(expiredQuery.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, DropdownPopupFacadeRoundTripsAndExpiresWithPhase)
{
    constexpr UI::UIPopupStyle PopupStyle{
        .placement = UI::UIPopupPlacement::Above,
        .anchorGap = 6.0F,
        .matchAnchorWidth = false,
    };
    constexpr UI::UIDropdownPaint DropdownPaint{
        .indicatorColor = {.red = 220, .green = 230, .blue = 240, .alpha = 255},
        .selectedItemBackgroundColor = {.red = 40, .green = 80, .blue = 120, .alpha = 255},
        .indicatorWidth = 12.0F,
        .indicatorHeight = 7.0F,
        .indicatorInset = 11.0F,
    };

    auto contextResult = UI::UIContext::Create(window, {.nodeCapacity = 64, .rootCapacity = 4});
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    auto dropdown = tree->createElement(root->rootNodeId(), UI::makeDropdownElement());
    ASSERT_TRUE(dropdown.has_value()) << dropdown.error().message;
    auto popup = tree->createElement(*dropdown, UI::makePopupElement());
    ASSERT_TRUE(popup.has_value()) << popup.error().message;
    auto firstItem = tree->createElement(*popup, UI::makeDropdownItemElement());
    auto secondItem = tree->createElement(*popup, UI::makeDropdownItemElement());
    ASSERT_TRUE(firstItem.has_value()) << firstItem.error().message;
    ASSERT_TRUE(secondItem.has_value()) << secondItem.error().message;

    UI::UILayoutStyle popupLayout = fixedSize(140.0F, 48.0F);
    popupLayout.placement = UI::UILayoutPlacement::Overlay;
    ASSERT_TRUE(tree->setLayoutStyle(root->rootNodeId(), fixedSize(200.0F, 120.0F)).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*dropdown, fixedSize(120.0F, 32.0F)).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*popup, popupLayout).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*firstItem, fixedSize(140.0F, 24.0F)).has_value());
    ASSERT_TRUE(tree->setLayoutStyle(*secondItem, fixedSize(140.0F, 24.0F)).has_value());
    ASSERT_TRUE(tree->setPopupStyle(*popup, PopupStyle).has_value());
    ASSERT_TRUE(tree->setDropdownPaint(*dropdown, DropdownPaint).has_value());
    ASSERT_TRUE(tree->setDropdownSelectedItem(*dropdown, *secondItem).has_value());
    ASSERT_TRUE(tree->setDropdownOpen(*dropdown, true).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}).has_value());

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    EXPECT_EQ(treeView.popupStyle(*popup).value(), PopupStyle);
    EXPECT_EQ(treeView.dropdownPaint(*dropdown).value(), DropdownPaint);
    EXPECT_TRUE(treeView.isPopupOpen(*popup).value());
    EXPECT_TRUE(treeView.isDropdownOpen(*dropdown).value());
    EXPECT_EQ(treeView.dropdownSelectedItem(*dropdown).value(), *secondItem);
    EXPECT_FALSE(treeView.isDropdownItemSelected(*firstItem).value());
    EXPECT_TRUE(treeView.isDropdownItemSelected(*secondItem).value());
    const UI::UIPopupMetrics metrics = treeView.popupMetrics(*popup).value();
    EXPECT_TRUE(metrics.open);
    EXPECT_EQ(metrics.resolvedPlacement, UI::UIPopupPlacement::Below);
    EXPECT_FLOAT_EQ(metrics.popupRect.width, 140.0F);

    ASSERT_TRUE(tree->setPopupOpen(*popup, false).has_value());
    EXPECT_FALSE(treeView.isDropdownOpen(*dropdown).value());
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto expiredCreate = tree->createElement(root->rootNodeId(), UI::makeDropdownElement());
    ASSERT_FALSE(expiredCreate.has_value());
    EXPECT_EQ(expiredCreate.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredMetrics = treeView.popupMetrics(*popup);
    ASSERT_FALSE(expiredMetrics.has_value());
    EXPECT_EQ(expiredMetrics.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, MenuFacadeRoundTripsCommandsMetricsAndExpiresWithPhase)
{
    constexpr UI::UIMenuConfig Config{
        .placement = UI::UIMenuPlacement::Right,
        .anchorGap = 6.0F,
        .viewportMargin = 10.0F,
        .matchAnchorWidth = false,
        .wrapKeyboardNavigation = false,
        .closeOnActivate = false,
    };
    auto contextResult = UI::UIContext::Create(window, {.nodeCapacity = 32, .rootCapacity = 2});
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    UI::UILayoutStyle anchorLayout = fixedSize(60.0F, 24.0F);
    anchorLayout.placement = UI::UILayoutPlacement::Overlay;
    anchorLayout.overlay.offset.x = UI::UILayoutLength::Px(20.0F);
    anchorLayout.overlay.offset.y = UI::UILayoutLength::Px(20.0F);
    auto anchor = tree->createElement(
        root->rootNodeId(), UI::makeButtonElement("Menu", anchorLayout));
    auto menu = tree->createElement(
        root->rootNodeId(), UI::makeMenuElement(Config, fixedSize(96.0F, 48.0F)));
    ASSERT_TRUE(anchor.has_value()) << anchor.error().message;
    ASSERT_TRUE(menu.has_value()) << menu.error().message;
    auto checkItem = tree->createElement(
        *menu, UI::makeMenuItemElement(
                   "Check", {.kind = UI::UIMenuItemKind::Check}, fixedSize(88.0F, 20.0F)));
    auto commandItem = tree->createElement(
        *menu, UI::makeMenuItemElement("Command", {}, fixedSize(88.0F, 20.0F)));
    ASSERT_TRUE(checkItem.has_value()) << checkItem.error().message;
    ASSERT_TRUE(commandItem.has_value()) << commandItem.error().message;

    ASSERT_TRUE(tree->setLayoutStyle(
        root->rootNodeId(), fixedSize(240.0F, 140.0F)).has_value());
    ASSERT_TRUE(tree->setMenuAnchor(*menu, *anchor).has_value());
    EXPECT_EQ(tree->menuAnchor(*menu).value(), *anchor);
    ASSERT_TRUE(tree->setMenuItemChecked(*checkItem, true).has_value());
    EXPECT_TRUE(tree->isMenuItemChecked(*checkItem).value());
    auto anchorCommitted = context->publication().commitLayout({.width = 240.0F, .height = 140.0F});
    ASSERT_TRUE(anchorCommitted.has_value()) << anchorCommitted.error().message;
    ASSERT_TRUE(tree->setMenuOpen(*menu, true).has_value());
    auto committed = context->publication().commitLayout({.width = 240.0F, .height = 140.0F});
    ASSERT_TRUE(committed.has_value()) << committed.error().message;

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    EXPECT_TRUE(treeView.isMenuOpen(*menu).value());
    const UI::UIMenuMetrics metrics = treeView.menuMetrics(*menu).value();
    EXPECT_TRUE(metrics.open);
    EXPECT_EQ(metrics.resolvedPlacement, UI::UIMenuPlacement::Right);
    EXPECT_EQ(metrics.anchorRect, (UI::UILogicalRect{20.0F, 20.0F, 60.0F, 24.0F}));
    EXPECT_FLOAT_EQ(metrics.menuRect.x, 86.0F);
    EXPECT_FLOAT_EQ(metrics.menuRect.width, 96.0F);

    auto first = tree->routeMenuCommand(*menu, UI::UIMenuCommand::First);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(first->targeted);
    EXPECT_TRUE(first->consumed);
    EXPECT_EQ(first->focus, *checkItem);
    auto next = tree->routeMenuCommand(*menu, UI::UIMenuCommand::Next);
    ASSERT_TRUE(next.has_value()) << next.error().message;
    EXPECT_EQ(next->focus, *commandItem);
    auto edge = tree->routeMenuCommand(*menu, UI::UIMenuCommand::Next);
    ASSERT_TRUE(edge.has_value()) << edge.error().message;
    EXPECT_TRUE(edge->consumed);
    EXPECT_FALSE(edge->focusChanged);
    EXPECT_EQ(edge->focus, *commandItem);

    ASSERT_TRUE(tree->setMenuOpen(*menu, false).has_value());
    EXPECT_FALSE(treeView.isMenuOpen(*menu).value());
    ASSERT_TRUE(tree->clearMenuAnchor(*menu).has_value());
    EXPECT_FALSE(treeView.menuAnchor(*menu).value().hasValue());
    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    Core::Status expiredOpen = tree->setMenuOpen(*menu, true);
    ASSERT_FALSE(expiredOpen.has_value());
    EXPECT_EQ(expiredOpen.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredMetrics = treeView.menuMetrics(*menu);
    ASSERT_FALSE(expiredMetrics.has_value());
    EXPECT_EQ(expiredMetrics.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredCommand = tree->routeMenuCommand(*menu, UI::UIMenuCommand::First);
    ASSERT_FALSE(expiredCommand.has_value());
    EXPECT_EQ(expiredCommand.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredChecked = treeView.isMenuItemChecked(*checkItem);
    ASSERT_FALSE(expiredChecked.has_value());
    EXPECT_EQ(expiredChecked.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, TooltipFacadeRoundTripsAndExpiresWithPhase)
{
    auto contextResult = UI::UIContext::Create(window, {.nodeCapacity = 32, .rootCapacity = 2});
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    UI::UILayoutStyle anchorLayout = fixedSize(60.0F, 24.0F);
    anchorLayout.placement = UI::UILayoutPlacement::Overlay;
    anchorLayout.overlay.offset.x = UI::UILayoutLength::Px(20.0F);
    anchorLayout.overlay.offset.y = UI::UILayoutLength::Px(20.0F);
    UI::UITooltipConfig config{
        .initialDelay = Core::Duration{0.0},
        .reshowDelay = Core::Duration{0.0},
        .dismissDelay = Core::Duration{0.0},
        .triggers = UI::UITooltipTrigger::Manual,
    };
    auto anchor = tree->createElement(
        root->rootNodeId(), UI::makeButtonElement("Anchor", anchorLayout));
    auto tooltip = tree->createElement(
        root->rootNodeId(), UI::makeTooltipElement("Runtime help", config, fixedSize(90.0F, 24.0F)));
    ASSERT_TRUE(anchor.has_value()) << anchor.error().message;
    ASSERT_TRUE(tooltip.has_value()) << tooltip.error().message;
    ASSERT_TRUE(tree->setTooltipAnchor(*tooltip, *anchor).has_value());
    ASSERT_EQ(tree->tooltipAnchor(*tooltip).value(), *anchor);

    ASSERT_TRUE(tree->setLayoutStyle(root->rootNodeId(), fixedSize(240.0F, 140.0F)).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 240.0F, .height = 140.0F}).has_value());
    ASSERT_FALSE(tree->isTooltipOpen(*tooltip).value());

    ASSERT_TRUE(tree->showTooltip(*tooltip).has_value());
    ASSERT_FALSE(tree->isTooltipOpen(*tooltip).value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 240.0F, .height = 140.0F}).has_value());
    EXPECT_TRUE(tree->isTooltipOpen(*tooltip).value());
    const UI::UITooltipMetrics metrics = tree->tooltipMetrics(*tooltip).value();
    EXPECT_TRUE(metrics.open);
    EXPECT_EQ(metrics.anchorRect.width, 60.0F);
    EXPECT_EQ(metrics.tooltipRect.width, 90.0F);

    ASSERT_TRUE(tree->dismissTooltip(*tooltip).has_value());
    EXPECT_TRUE(tree->isTooltipOpen(*tooltip).value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 240.0F, .height = 140.0F}).has_value());
    EXPECT_FALSE(tree->isTooltipOpen(*tooltip).value());
    ASSERT_TRUE(tree->clearTooltipAnchor(*tooltip).has_value());
    EXPECT_FALSE(tree->tooltipAnchor(*tooltip).value().hasValue());

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    auto expiredAnchor = tree->tooltipAnchor(*tooltip);
    ASSERT_FALSE(expiredAnchor.has_value());
    EXPECT_EQ(expiredAnchor.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredOpen = tree->isTooltipOpen(*tooltip);
    ASSERT_FALSE(expiredOpen.has_value());
    EXPECT_EQ(expiredOpen.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredMetrics = tree->tooltipMetrics(*tooltip);
    ASSERT_FALSE(expiredMetrics.has_value());
    EXPECT_EQ(expiredMetrics.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredShow = tree->showTooltip(*tooltip);
    ASSERT_FALSE(expiredShow.has_value());
    EXPECT_EQ(expiredShow.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, SplitViewFacadeRoundTripsAndExpiresWithPhase)
{
    auto contextResult = UI::UIContext::Create(window, {.nodeCapacity = 16, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    ASSERT_TRUE(tree->setLayoutStyle(
        root->rootNodeId(), fixedSize(240.0F, 120.0F)).has_value());
    auto splitView = tree->createElement(
        root->rootNodeId(),
        UI::makeSplitViewElement(
            UI::UISplitViewConfig{.initialFraction = 0.4F, .splitterExtent = 8.0F},
            fixedSize(240.0F, 120.0F)));
    ASSERT_TRUE(splitView.has_value()) << splitView.error().message;
    auto primary = tree->createElement(*splitView, UI::makePanelElement());
    auto splitter = tree->createElement(
        *splitView, UI::makeSplitterElement({.keyboardStep = 0.1F}));
    auto secondary = tree->createElement(*splitView, UI::makePanelElement());
    ASSERT_TRUE(primary && splitter && secondary);
    ASSERT_TRUE(tree->setSplitViewParts(
        *splitView, *primary, *splitter, *secondary).has_value());
    EXPECT_EQ(tree->splitViewParts(*splitView).value().splitter, *splitter);
    EXPECT_FALSE(tree->isSplitterDragging(*splitter).value());
    const UI::UISplitterPaint splitterPaint{
        .lineColor = UI::rgb(0x203040),
        .hoveredLineColor = UI::rgb(0x405060),
        .draggingLineColor = UI::rgb(0x607080),
        .focusRingColor = UI::rgb(0x8090A0),
        .lineThickness = 2.0F,
        .focusRingThickness = 4.0F,
    };
    ASSERT_TRUE(tree->setSplitterPaint(*splitter, splitterPaint).has_value());
    EXPECT_EQ(tree->splitterPaint(*splitter).value(), splitterPaint);
    ASSERT_TRUE(tree->setSplitViewFraction(*splitView, 0.6F).has_value());
    EXPECT_FLOAT_EQ(tree->splitViewFraction(*splitView).value(), 0.6F);

    ASSERT_TRUE(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}).has_value());
    const UI::UISplitViewMetrics metrics = tree->splitViewMetrics(*splitView).value();
    EXPECT_EQ(metrics.orientation, UI::UISplitViewOrientation::Horizontal);
    EXPECT_FLOAT_EQ(metrics.splitterRect.width, 8.0F);
    EXPECT_NEAR(metrics.fraction, 0.6F, 0.0001F);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    auto expiredParts = tree->splitViewParts(*splitView);
    ASSERT_FALSE(expiredParts.has_value());
    EXPECT_EQ(expiredParts.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredFraction = tree->splitViewFraction(*splitView);
    ASSERT_FALSE(expiredFraction.has_value());
    EXPECT_EQ(expiredFraction.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredMetrics = tree->splitViewMetrics(*splitView);
    ASSERT_FALSE(expiredMetrics.has_value());
    EXPECT_EQ(expiredMetrics.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredPaint = tree->splitterPaint(*splitter);
    ASSERT_FALSE(expiredPaint.has_value());
    EXPECT_EQ(expiredPaint.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredClear = tree->clearSplitViewParts(*splitView);
    ASSERT_FALSE(expiredClear.has_value());
    EXPECT_EQ(expiredClear.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, TabViewFacadeRoundTripsCommandsMetricsAndExpiresWithPhase)
{
    auto contextResult = UI::UIContext::Create(window, {.nodeCapacity = 16, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    context = std::move(*contextResult);

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    ASSERT_TRUE(tree->setLayoutStyle(
        root->rootNodeId(), fixedSize(240.0F, 120.0F)).has_value());
    auto tabView = tree->createElement(
        root->rootNodeId(), UI::makeTabViewElement({}, fixedSize(240.0F, 120.0F)));
    ASSERT_TRUE(tabView.has_value()) << tabView.error().message;
    auto firstTab = tree->createElement(
        *tabView, UI::makeTabElement("First", {}, fixedSize(60.0F, 24.0F)));
    auto secondTab = tree->createElement(
        *tabView, UI::makeTabElement("Second", {}, fixedSize(70.0F, 24.0F)));
    auto firstPanel = tree->createElement(*tabView, UI::makePanelElement());
    auto secondPanel = tree->createElement(*tabView, UI::makePanelElement());
    ASSERT_TRUE(firstTab && secondTab && firstPanel && secondPanel);
    const std::array items{
        UI::UITabViewItem{.tab = *firstTab, .panel = *firstPanel},
        UI::UITabViewItem{.tab = *secondTab, .panel = *secondPanel},
    };
    ASSERT_TRUE(tree->setTabViewItems(*tabView, items).has_value());
    constexpr UI::UITabPaint tabPaint{
        .selectedBackgroundColor = {.red = 20, .green = 30, .blue = 40, .alpha = 255},
        .focusedBorderColor = {.red = 240, .green = 240, .blue = 240, .alpha = 255},
    };
    ASSERT_TRUE(tree->setTabPaint(*firstTab, tabPaint).has_value());
    EXPECT_EQ(tree->tabPaint(*firstTab).value(), tabPaint);
    EXPECT_EQ(tree->tabViewItemCount(*tabView).value(), 2U);
    EXPECT_EQ(tree->tabViewItemAt(*tabView, 1).value(), items[1]);
    EXPECT_EQ(tree->tabViewActiveTab(*tabView).value(), *firstTab);
    EXPECT_EQ(tree->tabViewActivePanel(*tabView).value(), *firstPanel);

    ASSERT_TRUE(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}).has_value());
    auto command = tree->routeTabViewCommand(*tabView, UI::UITabViewCommand::Next);
    ASSERT_TRUE(command.has_value()) << command.error().message;
    EXPECT_TRUE(command->targeted);
    EXPECT_TRUE(command->selectionChanged);
    EXPECT_EQ(tree->tabViewActiveTab(*tabView).value(), *secondTab);
    ASSERT_TRUE(context->publication().commitLayout({.width = 240.0F, .height = 120.0F}).has_value());
    const UI::UITabViewMetrics metrics = tree->tabViewMetrics(*tabView).value();
    EXPECT_EQ(metrics.activeTab, *secondTab);
    EXPECT_EQ(metrics.activePanel, *secondPanel);
    EXPECT_EQ(metrics.itemCount, 2U);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    auto expiredCount = tree->tabViewItemCount(*tabView);
    ASSERT_FALSE(expiredCount.has_value());
    EXPECT_EQ(expiredCount.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredMetrics = tree->tabViewMetrics(*tabView);
    ASSERT_FALSE(expiredMetrics.has_value());
    EXPECT_EQ(expiredMetrics.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredCommand = tree->routeTabViewCommand(*tabView, UI::UITabViewCommand::Previous);
    ASSERT_FALSE(expiredCommand.has_value());
    EXPECT_EQ(expiredCommand.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredSet = tree->setTabViewActiveTab(*tabView, *firstTab);
    ASSERT_FALSE(expiredSet.has_value());
    EXPECT_EQ(expiredSet.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredClear = tree->clearTabViewItems(*tabView);
    ASSERT_FALSE(expiredClear.has_value());
    EXPECT_EQ(expiredClear.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredTabPaint = tree->setTabPaint(*firstTab, tabPaint);
    ASSERT_FALSE(expiredTabPaint.has_value());
    EXPECT_EQ(expiredTabPaint.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
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

    auto progressBar = tree->createElement(root->rootNodeId(), UI::makeProgressBarElement());
    auto slider = tree->createElement(root->rootNodeId(), UI::makeSliderElement());
    auto checkbox = tree->createElement(root->rootNodeId(), UI::makeCheckboxElement());
    auto firstRadio = tree->createElement(root->rootNodeId(), UI::makeRadioButtonElement());
    auto secondRadio = tree->createElement(root->rootNodeId(), UI::makeRadioButtonElement());
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
        .thumbExtent = 8.0F,
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

    auto expiredCreate = tree->createElement(root->rootNodeId(), UI::makeProgressBarElement());
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
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    const PrimaryWindowUITreeUpdater& treeView = *tree;
    auto initialPaint = treeView.buttonPaint(*button);
    ASSERT_TRUE(initialPaint.has_value()) << initialPaint.error().message;
    EXPECT_EQ(*initialPaint, UI::makeTonalButtonChrome(context->style().productTheme()).states);

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
    auto panel = tree->createElement(root->rootNodeId(), UI::makePanelElement());
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
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

    auto directUpdater = context->authoring().treeUpdater(*root);
    ASSERT_TRUE(directUpdater.has_value()) << directUpdater.error().message;
    auto unmodifiedPaint = directUpdater->buttonPaint(*button);
    ASSERT_TRUE(unmodifiedPaint.has_value()) << unmodifiedPaint.error().message;
    EXPECT_EQ(*unmodifiedPaint, UI::makeTonalButtonChrome(context->style().productTheme()).states);
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
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
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
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
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
    auto firstButton = firstTree->createElement(firstRoot->rootNodeId(), UI::makeButtonElement());
    auto secondButton = secondTree->createElement(secondRoot->rootNodeId(), UI::makeButtonElement());
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
    auto panel = tree->createElement(root->rootNodeId(), UI::makePanelElement());
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
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
    auto button = tree->createElement(root->rootNodeId(), UI::makeButtonElement());
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

    ASSERT_TRUE(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}).has_value());
    auto routed = context->input().routePointerInput(UI::UIPointerInputEvent{
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
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto transaction = tree->beginBuildTransaction(
        root->rootNodeId(), UI::makePanelElement(),
        {.nodes = 2, .textBytes = 5});
    ASSERT_TRUE(transaction.has_value()) << transaction.error().message;
    ASSERT_TRUE(transaction->createElement(
        transaction->rootNodeId(), UI::makeLabelElement("Abort")).has_value());
    EXPECT_EQ(context->liveNodeCount(), 3U);

    state.abortPhase(*enterEpoch, CapabilityPhase::GameStateEnter);
    EXPECT_FALSE(state.hasPrimaryWindowUI(*enterEpoch, CapabilityPhase::GameStateEnter));
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_EQ(context->statistics().textByteUsed, 0U);
    EXPECT_FALSE(transaction->isActive());
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

    auto crossRoot = firstTree->createElement(secondRoot->rootNodeId(), UI::makePanelElement());
    ASSERT_FALSE(crossRoot.has_value());
    EXPECT_EQ(crossRoot.error().code, UI::UIErrorCode::InvalidNode);
    const usize nodesAfterFailure = context->liveNodeCount();

    auto otherwiseValid = firstTree->createElement(firstRoot->rootNodeId(), UI::makePanelElement());
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
    auto panel = enterTree->createElement(root->rootNodeId(), UI::makePanelElement());
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
    ASSERT_TRUE(context->publication().commitLayout({.width = 640.0F, .height = 360.0F}).has_value());
    const UI::UICommittedPaintView paint = context->publication().committedPaint();
    ASSERT_EQ(paint.size(), 1U);
    EXPECT_EQ(paint.entries().front().node, *panel);
    EXPECT_EQ(paint.entries().front().solidFill,
              (UI::UIPremultipliedRgba8Color{.red = 31, .green = 63, .blue = 94, .alpha = 200}));

    auto expired = updateTree->setBoxPaint(*panel, solidFill(1, 2, 3));
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, CommittedLayoutRectCopiesPreviousPublishedWorldRectAndExpires)
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

    ASSERT_TRUE(enterTree->setLayoutStyle(root->rootNodeId(), fixedSize(160.0F, 90.0F)).has_value());
    auto panel = enterTree->createElement(root->rootNodeId(), UI::makePanelElement(fixedSize(80.0F, 40.0F)));
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    ASSERT_TRUE(state.finishPhase(*enterEpoch, CapabilityPhase::GameStateEnter).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 160.0F, .height = 90.0F}).has_value());

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    auto updateTree = state.treeUpdater(*updateEpoch, CapabilityPhase::UIUpdate, *root);
    ASSERT_TRUE(updateTree.has_value()) << updateTree.error().message;

    auto rect = updateTree->committedLayoutRect(*panel);
    ASSERT_TRUE(rect.has_value()) << rect.error().message;
    EXPECT_FLOAT_EQ(rect->x, 0.0F);
    EXPECT_FLOAT_EQ(rect->y, 0.0F);
    EXPECT_FLOAT_EQ(rect->width, 80.0F);
    EXPECT_FLOAT_EQ(rect->height, 40.0F);

    ASSERT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
    auto expired = updateTree->committedLayoutRect(*panel);
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, SetBoxPaintFailureIsStickyAcrossContextAndPreventsLaterMutation)
{
    auto foreignContextResult = UI::UIContext::Create(window, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_TRUE(foreignContextResult.has_value()) << foreignContextResult.error().message;
    std::unique_ptr<UI::UIContext> foreignContext = std::move(*foreignContextResult);
    auto foreignRoot = foreignContext->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(foreignRoot.has_value()) << foreignRoot.error().message;
    auto foreignPanel = foreignContext->authoring().rootBuilder().createElement(foreignRoot->rootNodeId(), UI::makePanelElement());
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
    auto panel = tree->createElement(root->rootNodeId(), UI::makePanelElement());
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
    ASSERT_TRUE(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}).has_value());
    EXPECT_TRUE(context->publication().committedPaint().empty());
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
    auto panel = tree->createElement(root->rootNodeId(), UI::makePanelElement());
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

TEST_F(PrimaryWindowUICapabilityTest,
       FlowLocalUserFacadeAssignsQueriesAndExpiresWithPhase)
{
    auto gamepadPoolResult = GamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value())
        << gamepadPoolResult.error().message;
    GamepadPool gamepadPool = std::move(*gamepadPoolResult);
    auto gamepad = gamepadPool.tryEmplace(1);
    ASSERT_TRUE(gamepad.has_value()) << gamepad.error().message;

    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    constexpr UI::UIFlowLocalUserId User2{2};
    auto initialOwner = tree->flowLocalUserForGamepad(*gamepad);
    auto initialState = tree->flowInputDeviceState(User2);
    ASSERT_TRUE(initialOwner.has_value()) << initialOwner.error().message;
    ASSERT_TRUE(initialState.has_value()) << initialState.error().message;
    EXPECT_EQ(*initialOwner, UI::UIFlowPrimaryLocalUser);
    EXPECT_EQ(initialState->localUser, User2);
    EXPECT_EQ(initialState->device, UI::UIFlowInputDevice::KeyboardMouse);

    ASSERT_TRUE(tree->assignFlowGamepad(*gamepad, User2).has_value());
    auto assignedOwner = tree->flowLocalUserForGamepad(*gamepad);
    ASSERT_TRUE(assignedOwner.has_value()) << assignedOwner.error().message;
    EXPECT_EQ(*assignedOwner, User2);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());

    auto expiredOwner = tree->flowLocalUserForGamepad(*gamepad);
    ASSERT_FALSE(expiredOwner.has_value());
    EXPECT_EQ(expiredOwner.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredState = tree->flowInputDeviceState(User2);
    ASSERT_FALSE(expiredState.has_value());
    EXPECT_EQ(expiredState.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
    Core::Status expiredClear = tree->clearFlowGamepadAssignment(*gamepad);
    ASSERT_FALSE(expiredClear.has_value());
    EXPECT_EQ(expiredClear.error().code,
              RuntimeErrorCode::UIPhaseCapabilityExpired);
}

} // namespace
} // namespace Tina::Tests
