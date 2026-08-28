#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <limits>
#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return m_allocationCount;
    }

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
};

[[nodiscard]] std::unique_ptr<UI::UIContext>
createContext(Platform::WindowId window,
              UI::UIContextCapacityConfig capacities =
                  {
                      .nodeCapacity = 16,
                      .rootCapacity = 1,
                      .routePathCapacity = 16,
                  },
              std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.authoring().rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.authoring().treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
{
    UI::UIBoxPaint paint{};
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

[[nodiscard]] UI::UIScrollViewPaint visibleScrollPaint() noexcept
{
    return UI::UIScrollViewPaint{
        .trackColor = {.red = 20, .green = 30, .blue = 40, .alpha = 255},
        .thumbColor = {.red = 80, .green = 100, .blue = 120, .alpha = 255},
        .draggingThumbColor = {.red = 240, .green = 180, .blue = 40, .alpha = 255},
        .thickness = 10.0F,
        .minThumbExtent = 24.0F,
    };
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIPointerInputEvent pointerInput(Platform::WindowId window, UI::UIRoutedPointerEventKind kind,
                                                   u64 sequence, UI::UILogicalPoint position,
                                                   UI::UILogicalPoint delta = {}) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = 0,
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .delta = delta,
        .button = Platform::PointerButton::Primary,
    };
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayoutEntry(UI::UICommittedLayoutView view,
                                                                UI::UINodeId node) noexcept
{
    for (const UI::UICommittedLayoutEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(UI::UICommittedSemanticsView view,
                                                             UI::UINodeId node) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

struct VerticalScrollTree final {
    UI::UINodeId scrollView{};
    UI::UINodeId content{};
};

[[nodiscard]] VerticalScrollTree
createVerticalScrollTree(UI::UITreeUpdater& updater, UI::UINodeId root,
                         UI::UIScrollBarVisibility visibility = UI::UIScrollBarVisibility::Auto)
{
    auto scrollResult = updater.createElement(root, UI::makeScrollViewElement());
    EXPECT_TRUE(scrollResult.has_value()) << (scrollResult ? "" : scrollResult.error().message);
    if (!scrollResult)
    {
        return {};
    }
    auto contentResult = updater.createElement(*scrollResult, UI::makePanelElement());
    EXPECT_TRUE(contentResult.has_value()) << (contentResult ? "" : contentResult.error().message);
    if (!contentResult)
    {
        return {};
    }
    assertOk(updater.setLayoutStyle(*scrollResult, fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*contentResult, fixedSize(100.0F, 200.0F)));
    assertOk(updater.setScrollViewStyle(*scrollResult, {
                                                           .axes = UI::UIScrollAxes::Vertical,
                                                           .scrollBarVisibility = visibility,
                                                           .wheelStep = 20.0F,
                                                       }));
    return {.scrollView = *scrollResult, .content = *contentResult};
}

class UIScrollViewTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
        context = createContext(window);
        ASSERT_NE(context, nullptr);
        root = createRoot(*context);
        ASSERT_TRUE(root.hasValue());
        updater = createUpdater(*context, root);
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UIScrollViewTest, AutoScrollbarWidthConvergesWrappedLabelLayout)
{
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(), fixedSize(100.0F, 30.0F)));

    auto scrollResult = updater.createElement(
        root.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(scrollResult.has_value()) << scrollResult.error().message;
    auto contentResult = updater.createElement(
        *scrollResult, UI::makePanelElement());
    ASSERT_TRUE(contentResult.has_value()) << contentResult.error().message;
    auto labelResult = updater.createElement(
        *contentResult, UI::makeLabelElement());
    ASSERT_TRUE(labelResult.has_value()) << labelResult.error().message;

    assertOk(updater.setLayoutStyle(
        *scrollResult, fixedSize(100.0F, 30.0F)));
    UI::UILayoutStyle contentStyle{};
    contentStyle.size.width = UI::UILayoutLength::Percent(100.0F);
    assertOk(updater.setLayoutStyle(*contentResult, contentStyle));
    UI::UILayoutStyle labelStyle{};
    labelStyle.size.width = UI::UILayoutLength::Percent(100.0F);
    assertOk(updater.setLayoutStyle(*labelResult, labelStyle));
    assertOk(updater.setText(*labelResult, "AAAAA AAAA AAAAA"));
    UI::UITextStyle textStyle{};
    textStyle.color.alpha = 0;
    assertOk(updater.setTextStyle(*labelResult, textStyle));
    assertOk(updater.setTextWrapMode(
        *labelResult, UI::UITextWrapMode::Words));
    assertOk(updater.setScrollViewStyle(
        *scrollResult,
        {
            .axes = UI::UIScrollAxes::Vertical,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
            .wheelStep = 20.0F,
        }));
    assertOk(updater.setScrollViewPaint(
        *scrollResult, visibleScrollPaint()));

    assertOk(context->publication().commitLayout(
        {.width = 100.0F, .height = 30.0F}));

    const UI::UIScrollViewMetrics metrics =
        updater.scrollViewMetrics(*scrollResult).value();
    EXPECT_TRUE(metrics.verticalScrollBarVisible);
    EXPECT_FALSE(metrics.horizontalScrollBarVisible);
    EXPECT_FLOAT_EQ(metrics.viewportSize.width, 90.0F);
    EXPECT_FLOAT_EQ(metrics.viewportSize.height, 30.0F);
    EXPECT_NEAR(metrics.contentSize.height, 57.6F, 0.001F);

    const UI::UICommittedLayoutView layout =
        context->publication().committedLayout();
    const UI::UICommittedLayoutEntry* content =
        findLayoutEntry(layout, *contentResult);
    const UI::UICommittedLayoutEntry* label =
        findLayoutEntry(layout, *labelResult);
    ASSERT_NE(content, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(content->worldRect.width, 90.0F);
    EXPECT_NEAR(content->worldRect.height, 57.6F, 0.001F);
    EXPECT_FLOAT_EQ(label->worldRect.width, 90.0F);
    EXPECT_NEAR(label->worldRect.height, 57.6F, 0.001F);
    EXPECT_GE(context->statistics().lastLayoutPassCount, 2U);
    EXPECT_LE(context->statistics().lastLayoutPassCount, 3U);
}

TEST_F(UIScrollViewTest, DefaultsRoundTripAndInvalidValuesDoNotMutate)
{
    auto scrollResult = updater.createElement(root.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(scrollResult.has_value()) << scrollResult.error().message;
    const UI::UINodeId scrollView = *scrollResult;

    ASSERT_EQ(updater.scrollViewStyle(scrollView).value(), UI::UIScrollViewStyle{});
    ASSERT_EQ(updater.scrollViewPaint(scrollView).value(), UI::UIScrollViewPaint{});
    ASSERT_EQ(updater.scrollViewOffset(scrollView).value(), UI::UIScrollOffset{});
    ASSERT_EQ(updater.scrollViewMetrics(scrollView).value(), UI::UIScrollViewMetrics{});

    const UI::UIScrollViewStyle expectedStyle{
        .axes = UI::UIScrollAxes::Both,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        .wheelStep = 16.0F,
    };
    const UI::UIScrollViewPaint expectedPaint = visibleScrollPaint();
    assertOk(updater.setScrollViewStyle(scrollView, expectedStyle));
    assertOk(updater.setScrollViewOffset(scrollView, {.x = 12.0F, .y = 34.0F}));
    assertOk(updater.setScrollViewPaint(scrollView, expectedPaint));

    UI::UIScrollViewStyle invalidStyle = expectedStyle;
    invalidStyle.axes = static_cast<UI::UIScrollAxes>(0xff);
    EXPECT_EQ(updater.setScrollViewStyle(scrollView, invalidStyle).error().code, UI::UIErrorCode::InvalidControlValue);
    invalidStyle = expectedStyle;
    invalidStyle.scrollBarVisibility = static_cast<UI::UIScrollBarVisibility>(0xff);
    EXPECT_EQ(updater.setScrollViewStyle(scrollView, invalidStyle).error().code, UI::UIErrorCode::InvalidControlValue);
    invalidStyle = expectedStyle;
    invalidStyle.wheelStep = 0.0F;
    EXPECT_EQ(updater.setScrollViewStyle(scrollView, invalidStyle).error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(updater.setScrollViewOffset(scrollView, {.x = -1.0F, .y = 0.0F}).error().code,
              UI::UIErrorCode::InvalidControlValue);
    UI::UIScrollViewPaint invalidPaint = expectedPaint;
    invalidPaint.thickness = (std::numeric_limits<float>::infinity)();
    EXPECT_EQ(updater.setScrollViewPaint(scrollView, invalidPaint).error().code, UI::UIErrorCode::InvalidControlValue);

    auto panelResult = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panelResult.has_value()) << panelResult.error().message;
    EXPECT_EQ(updater.setScrollViewOffset(*panelResult, {}).error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_FALSE(updater.scrollViewMetrics(*panelResult).has_value());

    EXPECT_EQ(updater.scrollViewStyle(scrollView).value(), expectedStyle);
    EXPECT_EQ(updater.scrollViewOffset(scrollView).value(), (UI::UIScrollOffset{.x = 12.0F, .y = 34.0F}));
    EXPECT_EQ(updater.scrollViewPaint(scrollView).value(), expectedPaint);
}

TEST_F(UIScrollViewTest, AxesAndVisibilityResolveDeterministicViewportMetrics)
{
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 500.0F)));

    const auto createCase = [&](UI::UIScrollViewStyle scrollStyle, UI::UILayoutStyle containerStyle,
                                UI::UILayoutStyle contentStyle) {
        auto scrollResult = updater.createElement(root.rootNodeId(), UI::makeScrollViewElement());
        EXPECT_TRUE(scrollResult.has_value()) << (scrollResult ? "" : scrollResult.error().message);
        if (!scrollResult)
        {
            return UI::UINodeId{};
        }
        auto childResult = updater.createElement(*scrollResult, UI::makePanelElement());
        EXPECT_TRUE(childResult.has_value()) << (childResult ? "" : childResult.error().message);
        if (!childResult)
        {
            return UI::UINodeId{};
        }
        assertOk(updater.setLayoutStyle(*scrollResult, containerStyle));
        assertOk(updater.setLayoutStyle(*childResult, contentStyle));
        assertOk(updater.setScrollViewStyle(*scrollResult, scrollStyle));
        return *scrollResult;
    };

    UI::UILayoutStyle column = fixedSize(100.0F, 100.0F);
    UI::UILayoutStyle row = column;
    row.flexContainer.direction = UI::UIFlexDirection::Row;
    const UI::UINodeId vertical =
        createCase({.axes = UI::UIScrollAxes::Vertical, .scrollBarVisibility = UI::UIScrollBarVisibility::Auto}, column,
                   fixedSize(100.0F, 200.0F));
    const UI::UINodeId horizontal =
        createCase({.axes = UI::UIScrollAxes::Horizontal, .scrollBarVisibility = UI::UIScrollBarVisibility::Auto}, row,
                   fixedSize(200.0F, 100.0F));
    const UI::UINodeId both =
        createCase({.axes = UI::UIScrollAxes::Both, .scrollBarVisibility = UI::UIScrollBarVisibility::Auto}, row,
                   fixedSize(200.0F, 200.0F));
    const UI::UINodeId hidden =
        createCase({.axes = UI::UIScrollAxes::Both, .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden}, row,
                   fixedSize(200.0F, 200.0F));
    const UI::UINodeId always =
        createCase({.axes = UI::UIScrollAxes::Both, .scrollBarVisibility = UI::UIScrollBarVisibility::Always}, row,
                   fixedSize(20.0F, 20.0F));
    ASSERT_TRUE(vertical.hasValue() && horizontal.hasValue() && both.hasValue() && hidden.hasValue() &&
                always.hasValue());

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 500.0F}));
    const UI::UIScrollViewMetrics verticalMetrics = updater.scrollViewMetrics(vertical).value();
    EXPECT_EQ(verticalMetrics.viewportSize, (UI::UILogicalSize{.width = 90.0F, .height = 100.0F}));
    EXPECT_EQ(verticalMetrics.contentSize, (UI::UILogicalSize{.width = 90.0F, .height = 200.0F}));
    EXPECT_FALSE(verticalMetrics.horizontalScrollBarVisible);
    EXPECT_TRUE(verticalMetrics.verticalScrollBarVisible);

    const UI::UIScrollViewMetrics horizontalMetrics = updater.scrollViewMetrics(horizontal).value();
    EXPECT_EQ(horizontalMetrics.viewportSize, (UI::UILogicalSize{.width = 100.0F, .height = 90.0F}));
    EXPECT_EQ(horizontalMetrics.contentSize, (UI::UILogicalSize{.width = 200.0F, .height = 90.0F}));
    EXPECT_TRUE(horizontalMetrics.horizontalScrollBarVisible);
    EXPECT_FALSE(horizontalMetrics.verticalScrollBarVisible);

    const UI::UIScrollViewMetrics bothMetrics = updater.scrollViewMetrics(both).value();
    EXPECT_EQ(bothMetrics.viewportSize, (UI::UILogicalSize{.width = 90.0F, .height = 90.0F}));
    EXPECT_EQ(bothMetrics.contentSize, (UI::UILogicalSize{.width = 200.0F, .height = 200.0F}));
    EXPECT_TRUE(bothMetrics.horizontalScrollBarVisible);
    EXPECT_TRUE(bothMetrics.verticalScrollBarVisible);

    const UI::UIScrollViewMetrics hiddenMetrics = updater.scrollViewMetrics(hidden).value();
    EXPECT_EQ(hiddenMetrics.viewportSize, (UI::UILogicalSize{.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(hiddenMetrics.contentSize, (UI::UILogicalSize{.width = 200.0F, .height = 200.0F}));
    EXPECT_FALSE(hiddenMetrics.horizontalScrollBarVisible);
    EXPECT_FALSE(hiddenMetrics.verticalScrollBarVisible);

    const UI::UIScrollViewMetrics alwaysMetrics = updater.scrollViewMetrics(always).value();
    EXPECT_EQ(alwaysMetrics.viewportSize, (UI::UILogicalSize{.width = 90.0F, .height = 90.0F}));
    EXPECT_EQ(alwaysMetrics.contentSize, (UI::UILogicalSize{.width = 90.0F, .height = 90.0F}));
    EXPECT_TRUE(alwaysMetrics.horizontalScrollBarVisible);
    EXPECT_TRUE(alwaysMetrics.verticalScrollBarVisible);
}

TEST_F(UIScrollViewTest, OffsetClampsAndDescendantsUseTheCommittedViewportClip)
{
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 200.0F)));
    const VerticalScrollTree tree =
        createVerticalScrollTree(updater, root.rootNodeId(), UI::UIScrollBarVisibility::Hidden);
    ASSERT_TRUE(tree.scrollView.hasValue());
    assertOk(updater.setPointerHitPolicy(tree.content, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.setScrollViewOffset(tree.scrollView, {.x = 40.0F, .y = 60.0F}));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 200.0F}));

    const UI::UIScrollViewMetrics metrics = updater.scrollViewMetrics(tree.scrollView).value();
    EXPECT_EQ(metrics.offset, (UI::UIScrollOffset{.x = 0.0F, .y = 60.0F}));
    const UI::UICommittedLayoutEntry* const content = findLayoutEntry(context->publication().committedLayout(), tree.content);
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(content->worldRect, (UI::UILogicalRect{.x = 0.0F, .y = -60.0F, .width = 100.0F, .height = 200.0F}));
    EXPECT_EQ(content->effectiveClip, (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(context->input().queryPointerHit({.x = 50.0F, .y = 50.0F}).target.node, tree.content);
    EXPECT_FALSE(context->input().queryPointerHit({.x = 50.0F, .y = 110.0F}).hasTarget());

    assertOk(updater.setScrollViewOffset(tree.scrollView, {.x = 0.0F, .y = 1'000.0F}));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 200.0F}));
    EXPECT_EQ(updater.scrollViewMetrics(tree.scrollView).value().offset, (UI::UIScrollOffset{.x = 0.0F, .y = 100.0F}));
    EXPECT_EQ(updater.scrollViewOffset(tree.scrollView).value(), (UI::UIScrollOffset{.x = 0.0F, .y = 100.0F}));

    assertOk(updater.setLayoutStyle(tree.content, fixedSize(100.0F, 50.0F)));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 200.0F}));
    EXPECT_EQ(updater.scrollViewMetrics(tree.scrollView).value().offset, UI::UIScrollOffset{});
    EXPECT_EQ(updater.scrollViewOffset(tree.scrollView).value(), UI::UIScrollOffset{});
}

TEST_F(UIScrollViewTest, WheelConsumesOnlyTheNearestScrollableAncestorThatCanMove)
{
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    auto outerResult = updater.createElement(root.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(outerResult.has_value()) << outerResult.error().message;
    const UI::UINodeId outer = *outerResult;
    auto innerResult = updater.createElement(outer, UI::makeScrollViewElement());
    auto fillerResult = updater.createElement(outer, UI::makePanelElement());
    ASSERT_TRUE(innerResult.has_value() && fillerResult.has_value());
    const UI::UINodeId inner = *innerResult;
    auto innerContentResult = updater.createElement(inner, UI::makePanelElement());
    ASSERT_TRUE(innerContentResult.has_value());

    assertOk(updater.setLayoutStyle(outer, fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(inner, fixedSize(80.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(*innerContentResult, fixedSize(80.0F, 200.0F)));
    assertOk(updater.setLayoutStyle(*fillerResult, fixedSize(100.0F, 200.0F)));
    const UI::UIScrollViewStyle hiddenVertical{
        .axes = UI::UIScrollAxes::Vertical,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        .wheelStep = 20.0F,
    };
    assertOk(updater.setScrollViewStyle(outer, hiddenVertical));
    assertOk(updater.setScrollViewStyle(inner, hiddenVertical));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto innerWheel = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Wheel, 1,
                                                              {.x = 20.0F, .y = 20.0F}, {.x = 0.0F, .y = -1.0F}));
    ASSERT_TRUE(innerWheel.has_value()) << (innerWheel ? "" : innerWheel.error().message);
    EXPECT_TRUE(innerWheel->consumed);
    EXPECT_EQ(updater.scrollViewOffset(inner).value(), (UI::UIScrollOffset{.x = 0.0F, .y = 20.0F}));
    EXPECT_EQ(updater.scrollViewOffset(outer).value(), UI::UIScrollOffset{});

    assertOk(updater.setScrollViewOffset(inner, {.x = 0.0F, .y = 10'000.0F}));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const float innerMaximum = updater.scrollViewMetrics(inner).value().maxOffsetY();
    ASSERT_GT(innerMaximum, 0.0F);
    EXPECT_FLOAT_EQ(updater.scrollViewOffset(inner).value().y, innerMaximum);

    auto outerWheel = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Wheel, 2,
                                                              {.x = 20.0F, .y = 20.0F}, {.x = 0.0F, .y = -1.0F}));
    ASSERT_TRUE(outerWheel.has_value()) << (outerWheel ? "" : outerWheel.error().message);
    EXPECT_TRUE(outerWheel->consumed);
    EXPECT_FLOAT_EQ(updater.scrollViewOffset(inner).value().y, innerMaximum);
    EXPECT_EQ(updater.scrollViewOffset(outer).value(), (UI::UIScrollOffset{.x = 0.0F, .y = 20.0F}));
}

TEST_F(UIScrollViewTest, ThumbDragCapturesPointerPublishesActivePaintAndReleasesOnUp)
{
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    const VerticalScrollTree tree = createVerticalScrollTree(updater, root.rootNodeId());
    ASSERT_TRUE(tree.scrollView.hasValue());
    const UI::UIScrollViewPaint paint = visibleScrollPaint();
    assertOk(updater.setScrollViewPaint(tree.scrollView, paint));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    ASSERT_EQ(context->publication().committedPaint().size(), 2U);
    EXPECT_EQ(context->publication().committedPaint().entries()[0].worldRect,
              (UI::UILogicalRect{.x = 90.0F, .y = 0.0F, .width = 10.0F, .height = 100.0F}));
    EXPECT_EQ(context->publication().committedPaint().entries()[1].worldRect,
              (UI::UILogicalRect{.x = 90.0F, .y = 0.0F, .width = 10.0F, .height = 50.0F}));

    auto down = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 95.0F, .y = 20.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), tree.scrollView);
    EXPECT_TRUE(updater.isScrollViewDragging(tree.scrollView).value());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    ASSERT_EQ(context->publication().committedPaint().size(), 2U);
    EXPECT_EQ(context->publication().committedPaint().entries()[1].solidFill,
              (UI::UIPremultipliedRgba8Color{.red = 240, .green = 180, .blue = 40, .alpha = 255}));
    const UI::UISemanticsEntry* draggingSemantics = findSemanticsEntry(context->publication().committedSemantics(), tree.scrollView);
    ASSERT_NE(draggingSemantics, nullptr);
    EXPECT_TRUE(draggingSemantics->focused);

    auto move = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::Move, 2, {.x = 95.0F, .y = 75.0F}));
    ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
    EXPECT_FLOAT_EQ(updater.scrollViewOffset(tree.scrollView).value().y, 100.0F);

    auto up = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 3, {.x = 95.0F, .y = 75.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_FALSE(updater.isScrollViewDragging(tree.scrollView).value());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(context->publication().committedPaint().entries()[1].solidFill,
              (UI::UIPremultipliedRgba8Color{.red = 80, .green = 100, .blue = 120, .alpha = 255}));
    const UI::UISemanticsEntry* releasedSemantics = findSemanticsEntry(context->publication().committedSemantics(), tree.scrollView);
    ASSERT_NE(releasedSemantics, nullptr);
    EXPECT_EQ(releasedSemantics->role, UI::UISemanticsRole::ScrollView);
    EXPECT_TRUE(releasedSemantics->hasRange);
    EXPECT_FLOAT_EQ(releasedSemantics->minValue, 0.0F);
    EXPECT_FLOAT_EQ(releasedSemantics->maxValue, 100.0F);
    EXPECT_FLOAT_EQ(releasedSemantics->value, 100.0F);
    EXPECT_FALSE(releasedSemantics->focused);
}

TEST_F(UIScrollViewTest, TrackPressPagesByOneViewportAndCapturesUntilUp)
{
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    const VerticalScrollTree tree = createVerticalScrollTree(updater, root.rootNodeId());
    ASSERT_TRUE(tree.scrollView.hasValue());
    assertOk(updater.setScrollViewPaint(tree.scrollView, visibleScrollPaint()));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto down = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 95.0F, .y = 75.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), tree.scrollView);
    EXPECT_FALSE(updater.isScrollViewDragging(tree.scrollView).value());
    EXPECT_FLOAT_EQ(updater.scrollViewOffset(tree.scrollView).value().y, 100.0F);

    auto up = context->input().routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 95.0F, .y = 75.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
}

TEST_F(UIScrollViewTest, CancelDisableModalAndDestroyClearThumbCapture)
{
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    const VerticalScrollTree tree = createVerticalScrollTree(updater, root.rootNodeId());
    ASSERT_TRUE(tree.scrollView.hasValue());
    assertOk(updater.setScrollViewPaint(tree.scrollView, visibleScrollPaint()));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    const auto arm = [&](u64 sequence) {
        auto down = context->input().routePointerInput(
            pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, sequence, {.x = 95.0F, .y = 20.0F}));
        EXPECT_TRUE(down.has_value()) << (down ? "" : down.error().message);
        EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), tree.scrollView);
        EXPECT_TRUE(updater.isScrollViewDragging(tree.scrollView).value());
    };

    arm(1);
    assertOk(context->input().cancelPointerInteraction(window));
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_FALSE(updater.isScrollViewDragging(tree.scrollView).value());

    arm(2);
    assertOk(updater.setEnabled(tree.scrollView, false));
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_FALSE(updater.isScrollViewDragging(tree.scrollView).value());
    assertOk(updater.setEnabled(tree.scrollView, true));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    arm(3);
    auto modalResult = updater.createElement(root.rootNodeId(), UI::makeModalElement());
    ASSERT_TRUE(modalResult.has_value()) << modalResult.error().message;
    assertOk(updater.setLayoutStyle(*modalResult, fixedSize(100.0F, 100.0F)));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(context->input().activeModal(), *modalResult);
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_FALSE(updater.isScrollViewDragging(tree.scrollView).value());

    assertOk(updater.destroy(*modalResult));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    arm(4);
    assertOk(updater.destroy(tree.scrollView));
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_FALSE(updater.isAlive(tree.scrollView));
}

TEST_F(UIScrollViewTest, PaintCapacityFailurePreservesPublishedMetricsAtomically)
{
    auto limitedContext = createContext(window, {
                                                    .nodeCapacity = 3,
                                                    .rootCapacity = 1,
                                                    .paintSnapshotCapacity = 2,
                                                });
    ASSERT_NE(limitedContext, nullptr);
    auto limitedRoot = createRoot(*limitedContext);
    ASSERT_TRUE(limitedRoot.hasValue());
    auto limitedUpdater = createUpdater(*limitedContext, limitedRoot);
    assertOk(limitedUpdater.setLayoutStyle(limitedRoot.rootNodeId(), fixedSize(100.0F, 100.0F)));
    const VerticalScrollTree tree =
        createVerticalScrollTree(limitedUpdater, limitedRoot.rootNodeId(), UI::UIScrollBarVisibility::Hidden);
    ASSERT_TRUE(tree.scrollView.hasValue());
    assertOk(limitedUpdater.setBoxPaint(tree.scrollView, solidFill(1, 2, 3)));
    assertOk(limitedUpdater.setScrollViewPaint(tree.scrollView, visibleScrollPaint()));
    assertOk(limitedContext->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UIScrollViewMetrics oldMetrics = limitedUpdater.scrollViewMetrics(tree.scrollView).value();
    const u64 oldLayoutRevision = limitedContext->publication().committedLayout().layoutRevision();
    ASSERT_EQ(limitedContext->publication().committedPaint().size(), 1U);

    assertOk(limitedUpdater.setScrollViewOffset(tree.scrollView, {.x = 0.0F, .y = 50.0F}));
    assertOk(
        limitedUpdater.setScrollViewStyle(tree.scrollView, {
                                                               .axes = UI::UIScrollAxes::Vertical,
                                                               .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                               .wheelStep = 20.0F,
                                                           }));
    const Core::Status overflow = limitedContext->publication().commitLayout({.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(limitedContext->publication().committedLayout().layoutRevision(), oldLayoutRevision);
    EXPECT_EQ(limitedUpdater.scrollViewMetrics(tree.scrollView).value(), oldMetrics);
    EXPECT_EQ(limitedUpdater.scrollViewOffset(tree.scrollView).value(), (UI::UIScrollOffset{.x = 0.0F, .y = 50.0F}));
    EXPECT_TRUE(limitedContext->statistics().layoutDirty);

    assertOk(
        limitedUpdater.setScrollViewStyle(tree.scrollView, {
                                                               .axes = UI::UIScrollAxes::Vertical,
                                                               .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
                                                               .wheelStep = 20.0F,
                                                           }));
    assertOk(limitedContext->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(limitedUpdater.scrollViewMetrics(tree.scrollView).value().offset,
              (UI::UIScrollOffset{.x = 0.0F, .y = 50.0F}));
}

TEST_F(UIScrollViewTest, SemanticsTextCapacityFailurePreservesPublishedMetricsAtomically)
{
    auto limitedContext = createContext(window, {
                                                    .nodeCapacity = 4,
                                                    .rootCapacity = 1,
                                                    .paintSnapshotCapacity = 1,
                                                    .textByteCapacity = 2,
                                                });
    ASSERT_NE(limitedContext, nullptr);
    auto limitedRoot = createRoot(*limitedContext);
    ASSERT_TRUE(limitedRoot.hasValue());
    auto limitedUpdater = createUpdater(*limitedContext, limitedRoot);
    assertOk(limitedUpdater.setLayoutStyle(limitedRoot.rootNodeId(), fixedSize(100.0F, 100.0F)));

    auto scrollResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(scrollResult.has_value()) << scrollResult.error().message;
    UI::UIElementDescriptor contentDescriptor = UI::makePanelElement();
    contentDescriptor.semantics = {
        .mode = UI::UISemanticsMode::MergeDescendants,
        .role = UI::UISemanticsRole::Group,
        .name = "a",
    };
    auto contentResult = limitedUpdater.createElement(*scrollResult, contentDescriptor);
    ASSERT_TRUE(contentResult.has_value()) << contentResult.error().message;
    const VerticalScrollTree tree{.scrollView = *scrollResult, .content = *contentResult};
    assertOk(limitedUpdater.setLayoutStyle(tree.scrollView, fixedSize(100.0F, 100.0F)));
    assertOk(limitedUpdater.setLayoutStyle(tree.content, fixedSize(100.0F, 200.0F)));
    assertOk(limitedUpdater.setScrollViewStyle(tree.scrollView, {
                                                                    .axes = UI::UIScrollAxes::Vertical,
                                                                    .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
                                                                    .wheelStep = 20.0F,
                                                                }));
    assertOk(limitedContext->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UIScrollViewMetrics oldMetrics = limitedUpdater.scrollViewMetrics(tree.scrollView).value();
    const u64 oldSemanticsRevision = limitedContext->publication().committedSemantics().semanticsRevision();

    assertOk(limitedUpdater.setScrollViewOffset(tree.scrollView, {.x = 0.0F, .y = 50.0F}));
    auto labelResult = limitedUpdater.createElement(tree.content, UI::makeLabelElement("b"));
    ASSERT_TRUE(labelResult.has_value()) << labelResult.error().message;
    assertOk(limitedUpdater.setLayoutStyle(*labelResult, fixedSize(10.0F, 10.0F)));
    const Core::Status overflow = limitedContext->publication().commitLayout({.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(limitedContext->publication().committedSemantics().semanticsRevision(), oldSemanticsRevision);
    EXPECT_EQ(limitedUpdater.scrollViewMetrics(tree.scrollView).value(), oldMetrics);

    assertOk(limitedUpdater.destroy(*labelResult));
    assertOk(limitedContext->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(limitedUpdater.scrollViewMetrics(tree.scrollView).value().offset,
              (UI::UIScrollOffset{.x = 0.0F, .y = 50.0F}));
}

TEST(UIScrollViewStandaloneTest, ThemeInheritanceAndLocalPaintOverrideRemainIndependent)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    auto windowResult = windowsResult->tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    auto contextResult = UI::UIContext::Create(*windowResult, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
    auto context = std::move(*contextResult);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    auto scrollResult = updater.createElement(root.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(scrollResult.has_value()) << scrollResult.error().message;

    EXPECT_EQ(updater.scrollViewPaint(*scrollResult).value(), UI::makeScrollViewPaint(UI::makeModernDesktopTheme()));
    assertOk(context->style().setProductTheme(UI::makeModernDesktopTheme(UI::UIColorScheme::Light)));
    EXPECT_EQ(updater.scrollViewPaint(*scrollResult).value(), UI::makeScrollViewPaint(UI::makeModernDesktopTheme(UI::UIColorScheme::Light)));

    const UI::UIScrollViewPaint localPaint = visibleScrollPaint();
    assertOk(updater.setScrollViewPaint(*scrollResult, localPaint));
    assertOk(context->style().setProductTheme(UI::makeModernDesktopTheme()));
    EXPECT_EQ(updater.scrollViewPaint(*scrollResult).value(), localPaint);
}

TEST(UIScrollViewStandaloneTest, WheelAndCommitRemainAllocationFreeAfterWarmup)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    auto windowResult = windowsResult->tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    ObservingMemoryResource resource;
    auto context = createContext(*windowResult,
                                 {
                                     .nodeCapacity = 3,
                                     .rootCapacity = 1,
                                     .routePathCapacity = 3,
                                 },
                                 resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    const VerticalScrollTree tree =
        createVerticalScrollTree(updater, root.rootNodeId(), UI::UIScrollBarVisibility::Hidden);
    ASSERT_TRUE(tree.scrollView.hasValue());
    assertOk(updater.setScrollViewOffset(tree.scrollView, {.x = 0.0F, .y = 50.0F}));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const usize allocationCount = resource.allocationCount();

    for (u64 routeIndex = 0; routeIndex < 200; ++routeIndex)
    {
        const float wheelDelta = routeIndex % 2 == 0 ? -1.0F : 1.0F;
        auto routed =
            context->input().routePointerInput(pointerInput(*windowResult, UI::UIRoutedPointerEventKind::Wheel, routeIndex + 1,
                                                    {.x = 50.0F, .y = 50.0F}, {.x = 0.0F, .y = wheelDelta}));
        ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
        EXPECT_TRUE(routed->consumed);
        assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

} // namespace
} // namespace Tina::Tests
