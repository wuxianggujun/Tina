#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] UI::UILayoutStyle overlay(float x, float y, float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = UI::UILayoutLength::Px(x);
    style.overlay.offset.y = UI::UILayoutLength::Px(y);
    return style;
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

class UIFocusNavigationTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;

        auto contextResult = UI::UIContext::Create(window, {
                                                               .nodeCapacity = 32,
                                                               .rootCapacity = 1,
                                                               .dirtyQueueCapacity = 32,
                                                               .paintSnapshotCapacity = 32,
                                                               .routePathCapacity = 16,
                                                               .routedPointerListenerCapacity = 8,
                                                               .buttonActionCapacity = 8,
                                                               .textByteCapacity = 1024,
                                                               .applyDefaultProductChrome = false,
                                                           });
        ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
        context = std::move(*contextResult);

        auto rootResult = context->rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
        root = std::move(*rootResult);
        auto updaterResult = context->treeUpdater(root);
        ASSERT_TRUE(updaterResult.has_value()) << (updaterResult ? "" : updaterResult.error().message);
        updater = std::move(*updaterResult);
        expectOk(updater.setLayoutStyle(root.rootNodeId(), overlay(0.0F, 0.0F, 240.0F, 180.0F)));
    }

    [[nodiscard]] UI::UINodeId addButton(UI::UINodeId parent, float x, float y, float width = 40.0F,
                                         float height = 32.0F)
    {
        auto button = updater.createElement(parent, UI::makeButtonElement());
        EXPECT_TRUE(button.has_value()) << (button ? "" : button.error().message);
        if (!button)
        {
            return {};
        }
        expectOk(updater.setLayoutStyle(*button, overlay(x, y, width, height)));
        return *button;
    }

    [[nodiscard]] UI::UINodeId addSlider(UI::UINodeId parent, float x, float y, float width = 80.0F,
                                         float height = 24.0F)
    {
        auto slider = updater.createElement(parent, UI::makeSliderElement());
        EXPECT_TRUE(slider.has_value()) << (slider ? "" : slider.error().message);
        if (!slider)
        {
            return {};
        }
        expectOk(updater.setLayoutStyle(*slider, overlay(x, y, width, height)));
        return *slider;
    }

    void commit()
    {
        expectOk(context->commitLayout({.width = 240.0F, .height = 180.0F}));
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UIFocusNavigationTest, MovesAcrossGridWithoutWrappingAndConsumesRelease)
{
    const UI::UINodeId topLeft = addButton(root.rootNodeId(), 10.0F, 10.0F);
    const UI::UINodeId topRight = addButton(root.rootNodeId(), 110.0F, 10.0F);
    const UI::UINodeId bottomLeft = addButton(root.rootNodeId(), 10.0F, 90.0F);
    const UI::UINodeId bottomRight = addButton(root.rootNodeId(), 110.0F, 90.0F);
    ASSERT_TRUE(topLeft.hasValue() && topRight.hasValue() && bottomLeft.hasValue() && bottomRight.hasValue());
    commit();

    auto unfocused = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right);
    ASSERT_TRUE(unfocused.has_value()) << (unfocused ? "" : unfocused.error().message);
    EXPECT_FALSE(unfocused->consumed);
    EXPECT_FALSE(unfocused->moved);
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    expectOk(context->requestFocus(topLeft));

    auto right = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right);
    ASSERT_TRUE(right.has_value()) << (right ? "" : right.error().message);
    EXPECT_TRUE(right->moved);
    EXPECT_EQ(right->focus, topRight);
    auto release = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right, false);
    ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
    EXPECT_TRUE(release->consumed);

    auto down = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Down);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->moved);
    EXPECT_EQ(down->focus, bottomRight);
    release = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Down, false);
    ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
    EXPECT_TRUE(release->consumed);

    auto left = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Left);
    ASSERT_TRUE(left.has_value()) << (left ? "" : left.error().message);
    EXPECT_TRUE(left->moved);
    EXPECT_EQ(left->focus, bottomLeft);
    release = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Left, false);
    ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
    EXPECT_TRUE(release->consumed);

    auto edge = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Down);
    ASSERT_TRUE(edge.has_value()) << (edge ? "" : edge.error().message);
    EXPECT_FALSE(edge->consumed);
    EXPECT_FALSE(edge->moved);
    EXPECT_EQ(edge->focus, bottomLeft);

    release = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Down, false);
    ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
    EXPECT_FALSE(release->consumed);
    EXPECT_FALSE(release->moved);
    EXPECT_EQ(release->focus, bottomLeft);
}

TEST_F(UIFocusNavigationTest, PrefersAlignedCandidateAndSkipsDisabledControl)
{
    const UI::UINodeId origin = addButton(root.rootNodeId(), 10.0F, 10.0F);
    const UI::UINodeId disabled = addButton(root.rootNodeId(), 65.0F, 10.0F);
    const UI::UINodeId aligned = addButton(root.rootNodeId(), 150.0F, 10.0F);
    const UI::UINodeId closerDiagonal = addButton(root.rootNodeId(), 60.0F, 100.0F);
    ASSERT_TRUE(origin.hasValue() && disabled.hasValue() && aligned.hasValue() && closerDiagonal.hasValue());
    expectOk(updater.setEnabled(disabled, false));
    commit();
    expectOk(context->requestFocus(origin));

    auto right = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right);
    ASSERT_TRUE(right.has_value()) << (right ? "" : right.error().message);
    EXPECT_TRUE(right->moved);
    EXPECT_EQ(right->focus, aligned);
}

TEST_F(UIFocusNavigationTest, SliderParticipatesInSpatialNavigation)
{
    const UI::UINodeId origin = addButton(root.rootNodeId(), 10.0F, 10.0F);
    const UI::UINodeId slider = addSlider(root.rootNodeId(), 110.0F, 10.0F);
    ASSERT_TRUE(origin.hasValue() && slider.hasValue());
    commit();
    expectOk(context->requestFocus(origin));

    auto right = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right);
    ASSERT_TRUE(right.has_value()) << (right ? "" : right.error().message);
    EXPECT_TRUE(right->consumed);
    EXPECT_TRUE(right->moved);
    EXPECT_EQ(right->focus, slider);
    EXPECT_EQ(context->defaultActionFocus(), slider);

    auto release = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right, false);
    ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
    EXPECT_TRUE(release->consumed);

    auto left = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Left);
    ASSERT_TRUE(left.has_value()) << (left ? "" : left.error().message);
    EXPECT_TRUE(left->moved);
    EXPECT_EQ(left->focus, origin);
}

TEST_F(UIFocusNavigationTest, ContainScopeRejectsGeometricallyCloserOutsideCandidate)
{
    auto scopeResult = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(scopeResult.has_value()) << (scopeResult ? "" : scopeResult.error().message);
    const UI::UINodeId scope = *scopeResult;
    expectOk(updater.setLayoutStyle(scope, overlay(20.0F, 20.0F, 180.0F, 80.0F)));
    expectOk(updater.setFocusScopeMode(scope, UI::UIFocusScopeMode::Contain));

    const UI::UINodeId insideLeft = addSlider(scope, 0.0F, 10.0F);
    const UI::UINodeId insideRight = addButton(scope, 120.0F, 10.0F);
    const UI::UINodeId outside = addButton(root.rootNodeId(), 80.0F, 30.0F);
    ASSERT_TRUE(insideLeft.hasValue() && insideRight.hasValue() && outside.hasValue());
    commit();
    expectOk(context->requestFocus(insideLeft));

    auto right = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right);
    ASSERT_TRUE(right.has_value()) << (right ? "" : right.error().message);
    EXPECT_TRUE(right->moved);
    EXPECT_EQ(right->focus, insideRight);
    auto release = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right, false);
    ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
    EXPECT_TRUE(release->consumed);

    right = context->routeFocusNavigation(UI::UIFocusNavigationDirection::Right);
    ASSERT_TRUE(right.has_value()) << (right ? "" : right.error().message);
    EXPECT_FALSE(right->consumed);
    EXPECT_FALSE(right->moved);
    EXPECT_EQ(right->focus, insideRight);
}

TEST_F(UIFocusNavigationTest, InvalidDirectionFailsWithoutChangingFocus)
{
    const UI::UINodeId button = addButton(root.rootNodeId(), 10.0F, 10.0F);
    ASSERT_TRUE(button.hasValue());
    commit();
    expectOk(context->requestFocus(button));

    auto result = context->routeFocusNavigation(static_cast<UI::UIFocusNavigationDirection>(255));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, UI::UIErrorCode::InvalidFocusTarget);
    EXPECT_EQ(context->defaultActionFocus(), button);
}

} // namespace
} // namespace Tina::Tests
