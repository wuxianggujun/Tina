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
    UI::UIContextCapacityConfig config)
{
    config.applyDefaultProductChrome = false;
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
        .styleRuleCapacity = 4,
        .styleBucketCapacity = 4,
        .styleRulesPerBucketCapacity = 4,
        .nodeStyleClassLinkCapacity = 8,
    };
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

} // namespace
} // namespace Tina::Tests
