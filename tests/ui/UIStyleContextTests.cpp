#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIContext.hpp>

#include <array>
#include <memory>

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
    auto root = context.rootBuilder().createRoot();
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

TEST(UIStyleContextTests, RegistersAndAtomicallyInstallsOnlyBeforeFirstNode)
{
    auto config = styleTestCapacity();
    config.styleClassCapacity = 2;
    auto context = createStyleContext(config);
    ASSERT_NE(context, nullptr);

    const auto accent = context->registerStyleClass();
    const auto compact = context->registerStyleClass();
    const auto exhausted = context->registerStyleClass();
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
    ASSERT_TRUE(context->installStyleSheet(rules).has_value());

    const auto beforeRoot = context->statistics().style;
    EXPECT_EQ(beforeRoot.registeredClassCount, 2U);
    EXPECT_EQ(beforeRoot.activeRuleCount, 2U);
    EXPECT_EQ(beforeRoot.activeBucketCount, 2U);
    EXPECT_EQ(beforeRoot.revision, 1U);
    EXPECT_EQ(beforeRoot.capacityFailureCount, 1U);

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const auto lateClass = context->registerStyleClass();
    ASSERT_FALSE(lateClass.has_value());
    EXPECT_EQ(lateClass.error().code, UI::UIErrorCode::InvalidStyle);
    const Core::Status lateSheet = context->installStyleSheet(
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
    const auto token = context->registerStyleColorToken(UI::rgb(0x2463A5));
    ASSERT_TRUE(token.has_value()) << token.error().message;
    const auto exhausted = context->registerStyleColorToken(UI::rgb(0xFFFFFF));
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .colorToken = *token,
        },
    };
    assertOk(context->installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor =
        UI::makePanelElement(fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    const auto panel = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    EXPECT_TRUE(hasPaintFill(context->committedPaint(), *panel,
                             UI::premultiply(UI::rgb(0x2463A5))));

    const auto statistics = context->statistics().style;
    EXPECT_EQ(statistics.tokenCapacity, 1U);
    EXPECT_EQ(statistics.registeredTokenCount, 1U);
    EXPECT_EQ(statistics.tokenHighWater, 1U);
    EXPECT_EQ(statistics.capacityFailureCount, 1U);
    const auto lateToken = context->registerStyleColorToken(UI::rgb(0x111111));
    ASSERT_FALSE(lateToken.has_value());
    EXPECT_EQ(lateToken.error().code, UI::UIErrorCode::InvalidStyle);
}

TEST(UIStyleContextTests, NodeClassLinksReleaseOnDestroyAndReuse)
{
    auto config = styleTestCapacity();
    config.nodeStyleClassLinkCapacity = 2;
    auto context = createStyleContext(config);
    ASSERT_NE(context, nullptr);
    const UI::UIStyleClassId accent = *context->registerStyleClass();
    const UI::UIStyleClassId compact = *context->registerStyleClass();

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const std::array bothClasses{accent, compact};
    UI::UIElementDescriptor styled = UI::makePanelElement();
    styled.visual.styleClasses = bothClasses;
    ASSERT_TRUE(context->rootBuilder().createElement(root.rootNodeId(), styled).has_value());
    EXPECT_EQ(context->statistics().style.activeNodeClassLinkCount, 2U);

    const std::array oneClass{accent};
    UI::UIElementDescriptor overflow = UI::makePanelElement();
    overflow.visual.styleClasses = oneClass;
    const auto rejected = context->rootBuilder().createElement(root.rootNodeId(), overflow);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->liveNodeCount(), 2U);
    EXPECT_EQ(context->statistics().style.capacityFailureCount, 1U);

    root.reset();
    EXPECT_EQ(context->statistics().style.activeNodeClassLinkCount, 0U);
    auto reusedRoot = createStyleRoot(*context);
    ASSERT_TRUE(reusedRoot);
    ASSERT_TRUE(context->rootBuilder()
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
    const UI::UIStyleClassId registered = *context->registerStyleClass();
    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);

    const auto expectInvalid = [&](std::span<const UI::UIStyleClassId> classes) {
        UI::UIElementDescriptor descriptor = UI::makePanelElement();
        descriptor.visual.styleClasses = classes;
        const auto result = context->rootBuilder().createElement(
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
    assertOk(context->installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor = UI::makeButtonElement({}, fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    auto panelResult = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panelResult.has_value()) << (panelResult ? "" : panelResult.error().message);
    const UI::UINodeId panel = *panelResult;

    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    const UI::UICommittedPaintEntry* paint = findPaintEntry(context->committedPaint(), panel);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgb(0x112233)));

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);
    assertOk(updater.setEnabled(panel, false));
    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    paint = findPaintEntry(context->committedPaint(), panel);
    ASSERT_NE(paint, nullptr);
    EXPECT_EQ(paint->solidFill, UI::premultiply(UI::rgb(0xABCDEF)));

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.lastStyleInspectedNodeCount, 1U);
    EXPECT_EQ(statistics.lastStyleResolvedNodeCount, 1U);
    EXPECT_EQ(statistics.lastStyleCandidateRuleCount, 2U);
    EXPECT_EQ(statistics.lastPaintCacheRebuildCount, 1U);

    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    const UI::UIContextStatistics cleanStatistics = context->statistics();
    EXPECT_EQ(cleanStatistics.lastStyleInspectedNodeCount, 0U);
    EXPECT_EQ(cleanStatistics.lastStyleResolvedNodeCount, 0U);
    EXPECT_EQ(cleanStatistics.lastStyleCandidateRuleCount, 0U);
}

TEST(UIStyleContextTests, StylesheetOverridesDefaultProductChromeForClasslessAndClassRules)
{
    auto context = createStyleContext(styleTestCapacity(), true);
    ASSERT_NE(context, nullptr);
    const UI::UIStyleClassId accent = *context->registerStyleClass();
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .color = UI::rgb(0x184E77),
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
            .styleClass = accent,
            .color = UI::rgb(0xF4A261),
        },
    };
    assertOk(context->installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor classlessDescriptor =
        UI::makeButtonElement({}, fixedSize(40.0F, 30.0F));
    classlessDescriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    const auto classlessSurface = context->rootBuilder().createElement(
        root.rootNodeId(), classlessDescriptor);
    ASSERT_TRUE(classlessSurface.has_value()) << classlessSurface.error().message;

    const std::array buttonClasses{accent};
    UI::UIElementDescriptor buttonDescriptor =
        UI::makeButtonElement({}, fixedSize(40.0F, 30.0F));
    buttonDescriptor.visual.styleClasses = buttonClasses;
    const auto button = context->rootBuilder().createElement(
        root.rootNodeId(), buttonDescriptor);
    ASSERT_TRUE(button.has_value()) << button.error().message;

    assertOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_TRUE(hasPaintFill(context->committedPaint(), *classlessSurface,
                             UI::premultiply(UI::rgb(0x184E77))));
    EXPECT_TRUE(hasPaintFill(context->committedPaint(), *button,
                             UI::premultiply(UI::rgb(0xF4A261))));

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    auto updater = std::move(*updaterResult);
    assertOk(updater.setEnabled(*classlessSurface, false));
    assertOk(updater.setEnabled(*button, false));
    assertOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_TRUE(hasPaintFill(context->committedPaint(), *classlessSurface,
                             UI::premultiply(UI::rgb(0x184E77))));
    EXPECT_TRUE(hasPaintFill(context->committedPaint(), *button,
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
    assertOk(context->installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    descriptor.visual.boxPaint = UI::UIBoxPaint{
        .solidFill = UI::UISolidFill{.color = UI::rgb(0x00AA44)},
    };
    const auto panel = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panel.has_value());

    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    const UI::UICommittedPaintEntry* paint = findPaintEntry(context->committedPaint(), *panel);
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
    assertOk(context->installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const auto checkbox = context->rootBuilder().createElement(
        root.rootNodeId(), UI::makeCheckboxElement(fixedSize(30.0F, 30.0F)));
    ASSERT_TRUE(checkbox.has_value());
    assertOk(context->commitLayout({.width = 60.0F, .height = 60.0F}));
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *checkbox), nullptr);

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);
    assertOk(updater.setChecked(*checkbox, true));
    assertOk(context->commitLayout({.width = 60.0F, .height = 60.0F}));
    const UI::UICommittedPaintEntry* paint = findPaintEntry(context->committedPaint(), *checkbox);
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
    assertOk(context->installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    UI::UIElementDescriptor descriptor = UI::makePanelElement(fixedSize(40.0F, 30.0F));
    descriptor.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    const auto panel = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(panel.has_value());
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);

    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    ASSERT_NE(findPaintEntry(context->committedPaint(), *panel), nullptr);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *panel)->solidFill,
              UI::premultiply(UI::rgb(0x118844)));

    assertOk(updater.setStyleRole(*panel, UI::UIStyleRoleId::PanelElevated));
    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    ASSERT_NE(findPaintEntry(context->committedPaint(), *panel), nullptr);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *panel)->solidFill,
              UI::premultiply(UI::rgb(0xCC4422)));

    assertOk(updater.setBoxPaint(*panel, {}));
    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *panel), nullptr);

    assertOk(updater.clearOverride(*panel, UI::UIStyleOverride::BoxPaint));
    assertOk(context->commitLayout({.width = 80.0F, .height = 60.0F}));
    ASSERT_NE(findPaintEntry(context->committedPaint(), *panel), nullptr);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *panel)->solidFill,
              UI::premultiply(UI::rgb(0xCC4422)));
}

TEST(UIStyleContextTests, ControlPaintOverridesSuppressStylesheetEvenAtExistingValues)
{
    auto context = createStyleContext(styleTestCapacity());
    ASSERT_NE(context, nullptr);
    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::ButtonPrimary,
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
    assertOk(context->installStyleSheet(rules));

    auto root = createStyleRoot(*context);
    ASSERT_TRUE(root);
    const auto button = context->rootBuilder().createElement(
        root.rootNodeId(), UI::makeButtonElement({}, fixedSize(30.0F, 20.0F)));
    const auto checkbox = context->rootBuilder().createElement(
        root.rootNodeId(), UI::makeCheckboxElement(fixedSize(30.0F, 20.0F)));
    const auto textEdit = context->rootBuilder().createElement(
        root.rootNodeId(), UI::makeTextEditElement({}, fixedSize(30.0F, 20.0F)));
    ASSERT_TRUE(button.has_value());
    ASSERT_TRUE(checkbox.has_value());
    ASSERT_TRUE(textEdit.has_value());
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    auto updater = std::move(*updaterResult);

    assertOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_NE(findPaintEntry(context->committedPaint(), *button), nullptr);
    EXPECT_NE(findPaintEntry(context->committedPaint(), *checkbox), nullptr);
    EXPECT_NE(findPaintEntry(context->committedPaint(), *textEdit), nullptr);

    assertOk(updater.setButtonPaint(*button, {}));
    assertOk(updater.setCheckboxPaint(*checkbox, {}));
    assertOk(updater.setTextEditPaint(*textEdit, {}));
    assertOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *button), nullptr);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *checkbox), nullptr);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), *textEdit), nullptr);
}

} // namespace
} // namespace Tina::Tests
