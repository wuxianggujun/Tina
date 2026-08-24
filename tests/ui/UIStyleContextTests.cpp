#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIInputRouter.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>
#include <tina/ui/UIStyleController.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <thread>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Platform::WindowId makeStyleTestWindow()
{
    static auto windows = [] {
        auto pool = WindowPool::Create(32);
        EXPECT_TRUE(pool.has_value());
        return std::make_unique<WindowPool>(std::move(*pool));
    }();
    auto id = windows->tryEmplace(1);
    EXPECT_TRUE(id.has_value());
    return id ? *id : Platform::WindowId{};
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createStyleContext(
    UI::UIContextCapacityConfig config, bool applyDefaultProductChrome = false)
{
    config.applyDefaultProductChrome = applyDefaultProductChrome;
    auto context = UI::UIContext::Create(makeStyleTestWindow(), config);
    EXPECT_TRUE(context.has_value())
        << (context ? "" : context.error().message);
    return context ? std::move(*context) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createStyleRoot(UI::UIContext& context)
{
    auto root = context.authoring().rootBuilder().createRoot();
    EXPECT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    return root ? std::move(*root) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UIContextCapacityConfig styleTestCapacity()
{
    return UI::UIContextCapacityConfig{
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .styleClassCapacity = 4,
        .styleTokenCapacity = 4,
        .styleRuleCapacity = 4,
        .styleBucketCapacity = 4,
        .styleRulesPerBucketCapacity = 4,
        .nodeStyleClassLinkCapacity = 8,
    };
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] const UI::UICommittedPaintEntry*
findPaintEntry(UI::UICommittedPaintView view, UI::UINodeId node) noexcept
{
    for (const UI::UICommittedPaintEntry& entry : view.entries())
    {
        if (entry.node == node && entry.kind == UI::UICommittedPaintKind::SolidQuad)
        {
            return &entry;
        }
    }
    return nullptr;
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

[[nodiscard]] Core::AssetId styleImageAssetId()
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x51};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] UI::UIImageContent styleImageContent(UI::UIStraightSrgba8Color tint)
{
    return UI::UIImageContent{
        .source =
            {
                .texture = styleImageAssetId(),
                .sourcePixels = {.x = 0, .y = 0, .width = 16, .height = 16},
                .texturePixelExtent = {.width = 16, .height = 16},
                .intrinsicLogicalSize = {.width = 16.0F, .height = 16.0F},
            },
        .fit = UI::UIImageFit::Fill,
        .tint = tint,
        .sampling = UI::UIImageSampling::Nearest,
    };
}

[[nodiscard]] const UI::UICommittedPaintEntry*
findImagePaintEntry(UI::UICommittedPaintView view, UI::UINodeId node) noexcept
{
    for (const UI::UICommittedPaintEntry& entry : view.entries())
    {
        if (entry.node == node && entry.kind == UI::UICommittedPaintKind::Image)
        {
            return &entry;
        }
    }
    return nullptr;
}

TEST(UIStyleContextTests, RegistersAndAtomicallyInstallsOnlyBeforeFirstNode)
{
    auto config = styleTestCapacity();
    config.styleClassCapacity = 2;
    auto context = createStyleContext(config);
    ASSERT_NE(context, nullptr);

    const auto accent = context->style().registerStyleClass();
    const auto compact = context->style().registerStyleClass();
    const auto exhausted = context->style().registerStyleClass();
    ASSERT_TRUE(accent.has_value());
    ASSERT_TRUE(compact.has_value());
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);

    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0x202020),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = *accent,
            .requiredStates = UI::UIStyleState::Hovered,
            .color = UI::rgb(0x3366FF),
        },
    };
    ASSERT_TRUE(context->style().installStyleSheet(rules).has_value());

    const auto beforeRoot = context->statistics().style;
    EXPECT_EQ(beforeRoot.registeredClassCount, 2U);
    EXPECT_EQ(beforeRoot.activeRuleCount, 2U);
    EXPECT_EQ(beforeRoot.activeBucketCount, 2U);
    EXPECT_EQ(beforeRoot.revision, 1U);
    EXPECT_EQ(beforeRoot.capacityFailureCount, 1U);

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const auto lateClass = context->style().registerStyleClass();
    ASSERT_FALSE(lateClass.has_value());
    EXPECT_EQ(lateClass.error().code, UI::UIErrorCode::InvalidStyle);
    const Core::Status lateSheet = context->style().installStyleSheet(
        std::span<const UI::UIStyleBoxFillRule>{});
    ASSERT_FALSE(lateSheet.has_value());
    EXPECT_EQ(lateSheet.error().code, UI::UIErrorCode::InvalidStyle);
    EXPECT_EQ(context->statistics().style.revision, 1U);
}

TEST(UIStyleContextTests, RegisteredColorTokenDrivesCommittedBoxFill)
{
    auto config = styleTestCapacity();
    config.styleTokenCapacity = 1;
    auto context = createStyleContext(config);
    ASSERT_NE(context, nullptr);
    const auto token = context->style().registerStyleColorToken(UI::rgb(0x2463A5));
    ASSERT_TRUE(token.has_value()) << token.error().message;
    const auto exhausted = context->style().registerStyleColorToken(UI::rgb(0xFFFFFF));
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .colorToken = *token,
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor =
        UI::makePanelElement(fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    const auto panel = context->authoring().rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *panel,
                             UI::premultiply(UI::rgb(0x2463A5))));

    const auto statistics = context->statistics().style;
    EXPECT_EQ(statistics.tokenCapacity, 1U);
    EXPECT_EQ(statistics.registeredTokenCount, 1U);
    EXPECT_EQ(statistics.tokenHighWater, 1U);
    EXPECT_EQ(statistics.capacityFailureCount, 1U);
    const auto lateToken = context->style().registerStyleColorToken(UI::rgb(0x111111));
    ASSERT_FALSE(lateToken.has_value());
    EXPECT_EQ(lateToken.error().code, UI::UIErrorCode::InvalidStyle);
}

TEST(UIStyleContextTests, FocusVisibleIsDerivedFromFocusAndNonPointerModality)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const UI::UIStraightSrgba8Color focusedColor = UI::rgb(0xA04444);
    const UI::UIStraightSrgba8Color focusVisibleColor = UI::rgb(0x4488DD);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonTonal,
            .requiredStates = UI::UIStyleState::Focused,
            .color = focusedColor,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonTonal,
            .requiredStates = UI::UIStyleState::FocusVisible,
            .color = focusVisibleColor,
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    auto button = context->authoring().rootBuilder().createElement(
        root.rootNodeId(), UI::makeButtonElement("", fixedSize(60.0F, 30.0F)));
    ASSERT_TRUE(button.has_value()) << button.error().message;
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 60.0F}));
    assertOk(context->input().requestFocus(*button));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 60.0F}));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *button,
                             UI::premultiply(focusedColor)));
    EXPECT_FALSE(hasPaintFill(context->publication().committedPaint(), *button,
                              UI::premultiply(focusVisibleColor)));

    auto navigation = context->input().routeFocusNavigation(
        UI::UIFocusNavigationDirection::Right, true, UI::UIInputModality::Keyboard);
    ASSERT_TRUE(navigation.has_value()) << navigation.error().message;
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 60.0F}));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *button,
                             UI::premultiply(focusVisibleColor)));
}

TEST(UIStyleContextTests, RuntimeColorTokenUpdateDirtiesOnlyWinningDependencies)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const UI::UIStyleTokenId primaryToken =
        *context->style().registerStyleColorToken(UI::rgb(0x2463A5));
    const UI::UIStyleTokenId secondaryToken =
        *context->style().registerStyleColorToken(UI::rgb(0xBA4A35));
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .colorToken = primaryToken,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonTonal,
            .colorToken = secondaryToken,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::TextInput,
            .color = UI::rgb(0x667788),
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const auto createPanel = [&](UI::UIStyleRoleId role,
                                 std::optional<UI::UIStraightSrgba8Color> localFill = std::nullopt) {
        UI::UIElementDescriptor descriptor =
            UI::makePanelElement(fixedSize(40.0F, 20.0F));
        descriptor.visual.styleRole = role;
        if (localFill.has_value())
        {
            descriptor.visual.boxPaint = UI::UIBoxPaint{
                .solidFill = UI::UISolidFill{.color = *localFill},
            };
        }
        return context->authoring().rootBuilder().createElement(root.rootNodeId(), descriptor);
    };
    const auto primaryPanel = createPanel(UI::UIStyleRoleId::PanelSurface);
    const auto secondaryPanel = createPanel(UI::UIStyleRoleId::ButtonTonal);
    const auto literalPanel = createPanel(UI::UIStyleRoleId::TextInput);
    const auto localPanel = createPanel(UI::UIStyleRoleId::PanelSurface,
                                        UI::rgb(0x22AA55));
    ASSERT_TRUE(primaryPanel && secondaryPanel && literalPanel && localPanel);
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    const auto initialValue = context->style().styleColorToken(primaryToken);
    ASSERT_TRUE(initialValue.has_value()) << initialValue.error().message;
    EXPECT_EQ(*initialValue, UI::rgb(0x2463A5));
    assertOk(context->style().setStyleColorToken(primaryToken, UI::rgb(0x3978C5)));

    const UI::UIContextStatistics updateStatistics = context->statistics();
    // Reverse-dependency index walks only nodes whose winning BoxFill is this token.
    EXPECT_EQ(updateStatistics.lastStyleTokenUpdateInspectedNodeCount, 1U);
    EXPECT_EQ(updateStatistics.lastStyleTokenUpdateResolvedNodeCount, 1U);
    EXPECT_EQ(updateStatistics.lastStyleTokenUpdateAffectedNodeCount, 1U);
    EXPECT_EQ(updateStatistics.lastStyleTokenUpdateCandidateRuleCount, 0U);
    EXPECT_EQ(updateStatistics.dirtyQueuePendingCount, 1U);
    EXPECT_FALSE(updateStatistics.layoutDirty);
    EXPECT_FALSE(updateStatistics.hitDirty);
    EXPECT_TRUE(updateStatistics.paintDirty);
    EXPECT_FALSE(updateStatistics.semanticsDirty);
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *primaryPanel,
                             UI::premultiply(UI::rgb(0x2463A5))));

    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *primaryPanel,
                             UI::premultiply(UI::rgb(0x3978C5))));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *secondaryPanel,
                             UI::premultiply(UI::rgb(0xBA4A35))));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *literalPanel,
                             UI::premultiply(UI::rgb(0x667788))));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *localPanel,
                             UI::premultiply(UI::rgb(0x22AA55))));

    const UI::UIContextStatistics beforeNoOp = context->statistics();
    assertOk(context->style().setStyleColorToken(primaryToken, UI::rgb(0x3978C5)));
    const UI::UIContextStatistics afterNoOp = context->statistics();
    EXPECT_EQ(afterNoOp.lastStyleTokenUpdateInspectedNodeCount, 0U);
    EXPECT_EQ(afterNoOp.lastStyleTokenUpdateResolvedNodeCount, 0U);
    EXPECT_EQ(afterNoOp.lastStyleTokenUpdateAffectedNodeCount, 0U);
    EXPECT_EQ(afterNoOp.lastStyleTokenUpdateCandidateRuleCount, 0U);
    EXPECT_EQ(afterNoOp.dirtyQueuePendingCount,
              beforeNoOp.dirtyQueuePendingCount);
    EXPECT_EQ(afterNoOp.paintRevision, beforeNoOp.paintRevision);
}

TEST(UIStyleContextTests, RuntimeColorTokenUpdateIsOwnerThreadValidated)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const UI::UIStyleTokenId token =
        *context->style().registerStyleColorToken(UI::rgb(0x2463A5));

    const auto invalidQuery = context->style().styleColorToken(UI::UIStyleTokenId{});
    ASSERT_FALSE(invalidQuery.has_value());
    EXPECT_EQ(invalidQuery.error().code, UI::UIErrorCode::InvalidStyle);
    const Core::Status invalidSet = context->style().setStyleColorToken(
        UI::UIStyleTokenId{.value = token.value + 1U}, UI::rgb(0x3978C5));
    ASSERT_FALSE(invalidSet.has_value());
    EXPECT_EQ(invalidSet.error().code, UI::UIErrorCode::InvalidStyle);

    std::optional<Core::Result<UI::UIStraightSrgba8Color>> threadedQuery;
    Core::Status threadedSet = Core::success();
    std::thread worker([&] {
        threadedQuery.emplace(context->style().styleColorToken(token));
        threadedSet = context->style().setStyleColorToken(token, UI::rgb(0x3978C5));
    });
    worker.join();
    ASSERT_TRUE(threadedQuery.has_value());
    ASSERT_FALSE(threadedQuery->has_value());
    EXPECT_EQ(threadedQuery->error().code, UI::UIErrorCode::WrongOwnerThread);
    ASSERT_FALSE(threadedSet.has_value());
    EXPECT_EQ(threadedSet.error().code, UI::UIErrorCode::WrongOwnerThread);
    EXPECT_EQ(*context->style().styleColorToken(token), UI::rgb(0x2463A5));
}

TEST(UIStyleContextTests, RuntimeColorTokenCapacityFailureIsAtomic)
{
    auto config = styleTestCapacity();
    config.nodeCapacity = 4;
    config.dirtyQueueCapacity = 1;
    config.styleTokenCapacity = 1;
    auto context = createStyleContext(config);
    ASSERT_NE(context, nullptr);
    const UI::UIStyleTokenId token =
        *context->style().registerStyleColorToken(UI::rgb(0x2463A5));
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .colorToken = token,
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor =
        UI::makePanelElement(fixedSize(40.0F, 20.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    const auto first = context->authoring().rootBuilder().createElement(root.rootNodeId(), descriptor);
    const auto second = context->authoring().rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(first && second);
    assertOk(context->publication().commitLayout({.width = 120.0F, .height = 80.0F}));
    const UI::UIContextStatistics before = context->statistics();

    const Core::Status rejected =
        context->style().setStyleColorToken(token, UI::rgb(0x3978C5));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(*context->style().styleColorToken(token), UI::rgb(0x2463A5));

    const UI::UIContextStatistics after = context->statistics();
    EXPECT_EQ(after.lastStyleTokenUpdateInspectedNodeCount, 2U);
    EXPECT_EQ(after.lastStyleTokenUpdateResolvedNodeCount, 2U);
    EXPECT_EQ(after.lastStyleTokenUpdateAffectedNodeCount, 2U);
    EXPECT_EQ(after.lastStyleTokenUpdateCandidateRuleCount, 0U);
    EXPECT_EQ(after.dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_EQ(after.paintRevision, before.paintRevision);
    EXPECT_EQ(after.paintDirty, before.paintDirty);
    EXPECT_EQ(after.semanticsDirty, before.semanticsDirty);
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *first,
                             UI::premultiply(UI::rgb(0x2463A5))));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *second,
                             UI::premultiply(UI::rgb(0x2463A5))));
}

TEST(UIStyleContextTests, NodeClassLinksReleaseOnDestroyAndReuse)
{
    auto config = styleTestCapacity();
    config.nodeStyleClassLinkCapacity = 2;
    auto context = createStyleContext(config);
    ASSERT_NE(context, nullptr);
    const UI::UIStyleClassId accent = *context->style().registerStyleClass();
    const UI::UIStyleClassId compact = *context->style().registerStyleClass();

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const std::array bothClasses{accent, compact};
    UI::UIElementDescriptor styled = UI::makePanelElement();
    styled.visual.styleClasses = bothClasses;
    ASSERT_TRUE(context->authoring().rootBuilder().createElement(root.rootNodeId(), styled).has_value());
    EXPECT_EQ(context->statistics().style.activeNodeClassLinkCount, 2U);

    const std::array oneClass{accent};
    UI::UIElementDescriptor overflow = UI::makePanelElement();
    overflow.visual.styleClasses = oneClass;
    const auto rejected = context->authoring().rootBuilder().createElement(root.rootNodeId(), overflow);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), 2U);
    EXPECT_EQ(context->statistics().style.capacityFailureCount, 1U);

    root.reset();
    EXPECT_EQ(context->statistics().style.activeNodeClassLinkCount, 0U);
    auto reusedRoot = createStyleRoot(*context);
    ASSERT_TRUE(reusedRoot);
    ASSERT_TRUE(context->authoring().rootBuilder()
                    .createElement(reusedRoot.rootNodeId(), overflow)
                    .has_value());
    const auto stats = context->statistics().style;
    EXPECT_EQ(stats.activeNodeClassLinkCount, 1U);
    EXPECT_EQ(stats.nodeClassLinkHighWater, 2U);
}

TEST(UIStyleContextTests, RejectsInvalidClassSetsBeforePublishingANode)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const UI::UIStyleClassId registered = *context->style().registerStyleClass();
    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);

    const auto expectInvalid = [&](std::span<const UI::UIStyleClassId> classes) {
        UI::UIElementDescriptor descriptor = UI::makePanelElement();
        descriptor.visual.styleClasses = classes;
        const auto result = context->authoring().rootBuilder().createElement(
            root.rootNodeId(), descriptor);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, UI::UIErrorCode::InvalidStyle);
        EXPECT_EQ(context->liveNodeCount(), 1U);
    };

    const std::array zeroClass{UI::UIStyleClassId{}};
    expectInvalid(zeroClass);
    const std::array unknownClass{UI::UIStyleClassId{.value = registered.value + 1U}};
    expectInvalid(unknownClass);
    const std::array duplicateClasses{registered, registered};
    expectInvalid(duplicateClasses);
    const std::array tooManyClasses{
        registered, registered, registered, registered, registered,
    };
    expectInvalid(tooManyClasses);
}

TEST(UIStyleContextTests, ResolvesRetainedStateIntoCommittedBoxFillCache)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0x112233),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .requiredStates = UI::UIStyleState::Disabled,
            .color = UI::rgb(0xABCDEF),
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor = UI::makeButtonElement({}, fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    auto panelResult = context->authoring().rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panelResult.has_value()) << (panelResult ? "" : panelResult.error().message);
    const UI::UINodeId panel = *panelResult;

    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    const UI::UICommittedPaintEntry* paint = findPaintEntry(context->publication().committedPaint(), panel);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgb(0x112233)));

    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);
    assertOk(updater.setEnabled(panel, false));
    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    paint = findPaintEntry(context->publication().committedPaint(), panel);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgb(0xABCDEF)));

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.lastStyleInspectedNodeCount, 1U);
    EXPECT_EQ(statistics.lastStyleResolvedNodeCount, 1U);
    EXPECT_EQ(statistics.lastStyleCandidateRuleCount, 2U);
    EXPECT_EQ(statistics.lastPaintCacheRebuildCount, 1U);

    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    const UI::UIContextStatistics cleanStatistics = context->statistics();
    EXPECT_EQ(cleanStatistics.lastStyleInspectedNodeCount, 0U);
    EXPECT_EQ(cleanStatistics.lastStyleResolvedNodeCount, 0U);
    EXPECT_EQ(cleanStatistics.lastStyleCandidateRuleCount, 0U);
}

TEST(UIStyleContextTests, StylesheetOverridesDefaultProductChromeForClasslessAndClassRules)
{
    auto context = createStyleContext(styleTestCapacity(), true);
    ASSERT_NE(context, nullptr);
    const UI::UIStyleClassId accent = *context->style().registerStyleClass();
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0x184E77),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonTonal,
            .styleClass = accent,
            .color = UI::rgb(0xF4A261),
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor classlessDescriptor =
        UI::makeButtonElement({}, fixedSize(40.0F, 30.0F));
    classlessDescriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    const auto classlessSurface = context->authoring().rootBuilder().createElement(
        root.rootNodeId(), classlessDescriptor);
    ASSERT_TRUE(classlessSurface.has_value()) << classlessSurface.error().message;

    const std::array buttonClasses{accent};
    UI::UIElementDescriptor buttonDescriptor =
        UI::makeButtonElement({}, fixedSize(40.0F, 30.0F));
    buttonDescriptor.visual.styleClasses = buttonClasses;
    const auto button = context->authoring().rootBuilder().createElement(
        root.rootNodeId(), buttonDescriptor);
    ASSERT_TRUE(button.has_value()) << button.error().message;

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *classlessSurface,
                             UI::premultiply(UI::rgb(0x184E77))));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *button,
                             UI::premultiply(UI::rgb(0xF4A261))));

    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    auto updater = std::move(*updaterResult);
    assertOk(updater.setEnabled(*classlessSurface, false));
    assertOk(updater.setEnabled(*button, false));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *classlessSurface,
                             UI::premultiply(UI::rgb(0x184E77))));
    EXPECT_TRUE(hasPaintFill(context->publication().committedPaint(), *button,
                             UI::premultiply(UI::rgb(0xF4A261))));
    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.lastStyleInspectedNodeCount, 2U);
    EXPECT_EQ(statistics.lastStyleResolvedNodeCount, 2U);
    EXPECT_EQ(statistics.lastStyleCandidateRuleCount, 2U);
}

TEST(UIStyleContextTests, LocalBoxPaintOverrideWinsOverStylesheet)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0xAA0000),
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    descriptor.visual.boxPaint = UI::UIBoxPaint{
        .solidFill = UI::UISolidFill{.color = UI::rgb(0x00AA44)},
    };
    const auto panel = context->authoring().rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panel.has_value());

    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    const UI::UICommittedPaintEntry* paint = findPaintEntry(context->publication().committedPaint(), *panel);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgb(0x00AA44)));
}

TEST(UIStyleContextTests, CheckedStateUsesExistingToggleStorage)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::Checkbox,
            .requiredStates = UI::UIStyleState::Checked,
            .color = UI::rgb(0x22CC55),
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const auto checkbox = context->authoring().rootBuilder().createElement(
        root.rootNodeId(), UI::makeCheckboxElement(fixedSize(30.0F, 30.0F)));
    ASSERT_TRUE(checkbox.has_value());
    assertOk(context->publication().commitLayout({.width = 60.0F, .height = 60.0F}));
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *checkbox), nullptr);

    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);
    assertOk(updater.setChecked(*checkbox, true));
    assertOk(context->publication().commitLayout({.width = 60.0F, .height = 60.0F}));
    const UI::UICommittedPaintEntry* paint = findPaintEntry(context->publication().committedPaint(), *checkbox);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgb(0x22CC55)));
}

TEST(UIStyleContextTests, RuntimeRoleAndClearOverrideRepublishResolvedFill)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0x118844),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelElevated,
            .color = UI::rgb(0xCC4422),
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    const auto panel = context->authoring().rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panel.has_value());
    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);

    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    ASSERT_NE(findPaintEntry(context->publication().committedPaint(), *panel), nullptr);
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *panel)->solidFill,
              UI::premultiply(UI::rgb(0x118844)));

    assertOk(updater.setStyleRole(*panel, UI::UIStyleRoleId::PanelElevated));
    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    ASSERT_NE(findPaintEntry(context->publication().committedPaint(), *panel), nullptr);
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *panel)->solidFill,
              UI::premultiply(UI::rgb(0xCC4422)));

    assertOk(updater.setBoxPaint(*panel, {}));
    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *panel), nullptr);

    assertOk(updater.clearOverride(*panel, UI::UIStyleOverride::BoxPaint));
    assertOk(context->publication().commitLayout({.width = 80.0F, .height = 60.0F}));
    ASSERT_NE(findPaintEntry(context->publication().committedPaint(), *panel), nullptr);
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *panel)->solidFill,
              UI::premultiply(UI::rgb(0xCC4422)));
}

TEST(UIStyleContextTests, ControlPaintOverridesSuppressStylesheetEvenAtExistingValues)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonTonal,
            .color = UI::rgb(0xAA3311),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::Checkbox,
            .color = UI::rgb(0x22AA55),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::TextInput,
            .color = UI::rgb(0x3355CC),
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const auto button = context->authoring().rootBuilder().createElement(
        root.rootNodeId(), UI::makeButtonElement({}, fixedSize(30.0F, 20.0F)));
    const auto checkbox = context->authoring().rootBuilder().createElement(
        root.rootNodeId(), UI::makeCheckboxElement(fixedSize(30.0F, 20.0F)));
    const auto textEdit = context->authoring().rootBuilder().createElement(
        root.rootNodeId(), UI::makeTextEditElement({}, fixedSize(30.0F, 20.0F)));
    ASSERT_TRUE(button.has_value());
    ASSERT_TRUE(checkbox.has_value());
    ASSERT_TRUE(textEdit.has_value());
    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_NE(findPaintEntry(context->publication().committedPaint(), *button), nullptr);
    EXPECT_NE(findPaintEntry(context->publication().committedPaint(), *checkbox), nullptr);
    EXPECT_NE(findPaintEntry(context->publication().committedPaint(), *textEdit), nullptr);

    assertOk(updater.setButtonPaint(*button, {}));
    assertOk(updater.setCheckboxPaint(*checkbox, {}));
    assertOk(updater.setTextEditPaint(*textEdit, {}));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *button), nullptr);
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *checkbox), nullptr);
    EXPECT_EQ(findPaintEntry(context->publication().committedPaint(), *textEdit), nullptr);
}

TEST(UIStyleContextTests, StylesheetImageTintTokenDrivesPaintAndLocalOverride)
{
    auto capacities = styleTestCapacity();
    capacities.imageContentCapacity = 2;
    auto context = createStyleContext(capacities);
    ASSERT_NE(context, nullptr);

    auto styleClass = context->style().registerStyleClass();
    ASSERT_TRUE(styleClass.has_value()) << styleClass.error().message;
    auto tintToken = context->style().registerStyleColorToken(UI::rgba8(16, 32, 48, 255));
    ASSERT_TRUE(tintToken.has_value()) << tintToken.error().message;
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = *styleClass,
            .imageTintToken = *tintToken,
        },
    };
    assertOk(context->style().installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const UI::UIStyleClassId classId = *styleClass;
    UI::UIElementDescriptor imageDesc =
        UI::makeImageElement(styleImageContent(UI::rgba8(255, 255, 255, 255)), "Icon",
                             fixedSize(32.0F, 32.0F));
    imageDesc.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    imageDesc.visual.styleClasses = std::span(&classId, 1);
    const auto image = context->authoring().rootBuilder().createElement(root.rootNodeId(), imageDesc);
    ASSERT_TRUE(image.has_value()) << image.error().message;

    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);

    assertOk(context->publication().commitLayout({.width = 64.0F, .height = 64.0F}));
    const auto* paint = findImagePaintEntry(context->publication().committedPaint(), *image);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgba8(16, 32, 48, 255)));
    EXPECT_EQ(*updater.imageTint(*image), UI::rgba8(16, 32, 48, 255));

    assertOk(context->style().setStyleColorToken(*tintToken, UI::rgba8(200, 100, 50, 180)));
    const UI::UIContextStatistics afterToken = context->statistics();
    EXPECT_EQ(afterToken.lastStyleTokenUpdateAffectedNodeCount, 1U);
    EXPECT_TRUE(afterToken.paintDirty);
    EXPECT_FALSE(afterToken.layoutDirty);
    assertOk(context->publication().commitLayout({.width = 64.0F, .height = 64.0F}));
    paint = findImagePaintEntry(context->publication().committedPaint(), *image);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgba8(200, 100, 50, 180)));

    assertOk(updater.setImageTint(*image, UI::rgba8(1, 2, 3, 255)));
    assertOk(context->publication().commitLayout({.width = 64.0F, .height = 64.0F}));
    paint = findImagePaintEntry(context->publication().committedPaint(), *image);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgba8(1, 2, 3, 255)));

    // Local override suppresses stylesheet token updates.
    assertOk(context->style().setStyleColorToken(*tintToken, UI::rgba8(9, 9, 9, 255)));
    EXPECT_EQ(context->statistics().lastStyleTokenUpdateAffectedNodeCount, 0U);
    assertOk(context->publication().commitLayout({.width = 64.0F, .height = 64.0F}));
    paint = findImagePaintEntry(context->publication().committedPaint(), *image);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgba8(1, 2, 3, 255)));

    assertOk(updater.clearOverride(*image, UI::UIStyleOverride::ImageTint));
    assertOk(context->publication().commitLayout({.width = 64.0F, .height = 64.0F}));
    paint = findImagePaintEntry(context->publication().committedPaint(), *image);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgba8(9, 9, 9, 255)));
}

} // namespace
} // namespace Tina::Tests
