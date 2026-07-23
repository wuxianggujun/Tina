#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return m_allocationCount; }
    [[nodiscard]] usize deallocationCount() const noexcept { return m_deallocationCount; }
    [[nodiscard]] usize currentBytes() const noexcept { return m_currentBytes; }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        m_currentBytes += bytes;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++m_deallocationCount;
        m_currentBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    usize m_deallocationCount = 0;
    usize m_currentBytes = 0;
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 8,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UINodeId createPanel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createPanel(parent);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createLabel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createLabel(parent);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createButton(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createButton(parent);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIPointerInputEvent makePointerInput(
    Platform::WindowId window,
    UI::UIRoutedPointerEventKind kind,
    u64 sequence,
    UI::UILogicalPoint position = {.x = 10.0F, .y = 10.0F}) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .delta = kind == UI::UIRoutedPointerEventKind::Move
            ? UI::UILogicalPoint{.x = 1.0F, .y = 1.0F}
            : UI::UILogicalPoint{},
        .button = Platform::PointerButton::Primary,
    };
}

UI::UIPointerRouteResult route(
    UI::UIContext& context,
    const UI::UIPointerInputEvent& input)
{
    auto result = context.routePointerInput(input);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UIPointerRouteResult{};
}

[[nodiscard]] bool claimsPrimaryButton(const UI::UIPointerRouteResult& result) noexcept
{
    return result.claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Primary));
}

[[nodiscard]] bool buttonPressed(
    UI::UITreeUpdater& updater,
    UI::UINodeId button)
{
    auto result = updater.isButtonPressed(button);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : false;
}

void expectButtonPressed(
    UI::UITreeUpdater& updater,
    UI::UINodeId button,
    bool expected)
{
    EXPECT_EQ(buttonPressed(updater, button), expected);
}

struct ActionInvocation final {
    UI::UINodeId button{};
    UI::UIButtonActivationSource source = UI::UIButtonActivationSource::PrimaryPointer;
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;
    int marker = 0;
};

struct ActionRecorder final {
    void push(const UI::UIButtonActionEvent& event, int marker) noexcept
    {
        if (size < entries.size()) {
            entries[size] = ActionInvocation{
                .button = event.buttonNode,
                .source = event.source,
                .platformFrame = event.platformFrame,
                .sourceSequence = event.sourceSequence,
                .marker = marker,
            };
        }
        ++size;
    }

    std::array<ActionInvocation, 16> entries{};
    usize size = 0;
};

[[nodiscard]] UI::UIButtonActionCallback makeAction(
    ActionRecorder& recorder,
    int marker) noexcept
{
    return UI::UIButtonActionCallback{
        [&recorder, marker](const UI::UIButtonActionEvent& event) noexcept {
            recorder.push(event, marker);
        }};
}

[[nodiscard]] UI::UIRoutedPointerListenerToken addListener(
    UI::UIContext& context,
    UI::UIRoutedPointerListenerDesc descriptor,
    UI::UIRoutedPointerCallback callback)
{
    auto result = context.addRoutedPointerListener(
        descriptor,
        std::move(callback));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRoutedPointerListenerToken{};
}

struct ButtonTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId panel{};
    UI::UINodeId button{};
};

[[nodiscard]] ButtonTree createButtonTree(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 8,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    ButtonTree tree;
    tree.context = createContext(window, capacities, resource);
    if (!tree.context) {
        return tree;
    }
    tree.root = createRoot(*tree.context);
    if (!tree.root) {
        return tree;
    }
    tree.panel = createPanel(*tree.context, tree.root.rootNodeId());
    tree.button = createButton(*tree.context, tree.panel);
    tree.updater = createUpdater(*tree.context, tree.root);

    expectOk(tree.updater.setLayoutStyle(
        tree.root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.panel, fixedSize(80.0F, 80.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.button, fixedSize(40.0F, 40.0F)));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    return tree;
}

class UIButtonActionTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(2);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto firstResult = windows->tryEmplace(1);
        auto secondResult = windows->tryEmplace(2);
        ASSERT_TRUE(firstResult.has_value());
        ASSERT_TRUE(secondResult.has_value());
        firstWindow = *firstResult;
        secondWindow = *secondResult;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId firstWindow{};
    Platform::WindowId secondWindow{};
};

TEST_F(UIButtonActionTest, WidgetKindsPublishExpectedDefaultHitPolicies)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId label = createLabel(*context, panel);
    const UI::UINodeId button = createButton(*context, panel);
    auto textEditResult = context->rootBuilder().createTextEdit(panel);
    ASSERT_TRUE(textEditResult.has_value());
    const UI::UINodeId textEdit = *textEditResult;
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(panel, fixedSize(80.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(label, fixedSize(20.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(30.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(40.0F, 10.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_EQ(hit.size(), 5U);
    EXPECT_EQ(hit.entries()[0].node, root.rootNodeId());
    EXPECT_EQ(hit.entries()[0].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[1].node, panel);
    EXPECT_EQ(hit.entries()[1].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[2].node, label);
    EXPECT_EQ(hit.entries()[2].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[3].node, button);
    EXPECT_EQ(hit.entries()[3].policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_EQ(hit.entries()[4].node, textEdit);
    EXPECT_EQ(hit.entries()[4].policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 2U);
}

TEST_F(UIButtonActionTest, PrimaryPointerDownMoveUpPressedAndActivatesOnce)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_EQ(down.pointQuery.target.node, tree.button);
    EXPECT_TRUE(down.consumed);
    EXPECT_TRUE(claimsPrimaryButton(down));
    expectButtonPressed(tree.updater, tree.button, true);
    EXPECT_EQ(recorder.size, 0U);

    const UI::UIPointerRouteResult move = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::Move, 2));
    EXPECT_EQ(move.pointQuery.target.node, tree.button);
    EXPECT_FALSE(move.consumed);
    EXPECT_TRUE(claimsPrimaryButton(move));
    expectButtonPressed(tree.updater, tree.button, true);

    const UI::UIPointerRouteResult up = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 3));
    EXPECT_EQ(up.pointQuery.target.node, tree.button);
    EXPECT_TRUE(up.consumed);
    EXPECT_FALSE(claimsPrimaryButton(up));
    expectButtonPressed(tree.updater, tree.button, false);
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].button, tree.button);
    EXPECT_EQ(recorder.entries[0].source, UI::UIButtonActivationSource::PrimaryPointer);
    EXPECT_EQ(recorder.entries[0].platformFrame, Platform::PlatformFrameId{3});
    EXPECT_EQ(recorder.entries[0].sourceSequence, 3U);
    EXPECT_EQ(recorder.entries[0].marker, 1);

    const UI::UIPointerRouteResult extraUp = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 4));
    EXPECT_FALSE(extraUp.consumed);
    EXPECT_EQ(recorder.size, 1U);
}

TEST_F(UIButtonActionTest, PrimaryUpOutsideClearsPressedWithoutActivation)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    ASSERT_TRUE(down.consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    const UI::UIPointerRouteResult moveOutside = route(
        *tree.context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::Move,
            2,
            {.x = 90.0F, .y = 90.0F}));
    EXPECT_FALSE(moveOutside.pointQuery.hasTarget());
    EXPECT_FALSE(moveOutside.consumed);
    EXPECT_TRUE(claimsPrimaryButton(moveOutside));
    expectButtonPressed(tree.updater, tree.button, false);

    const UI::UIPointerRouteResult upOutside = route(
        *tree.context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonUp,
            3,
            {.x = 90.0F, .y = 90.0F}));
    EXPECT_FALSE(upOutside.pointQuery.hasTarget());
    EXPECT_TRUE(upOutside.consumed);
    EXPECT_FALSE(claimsPrimaryButton(upOutside));
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 0U);
}

TEST_F(UIButtonActionTest, TargetableChildActivatesNearestButtonAncestor)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    const UI::UINodeId child = createPanel(*context, button);
    auto updater = createUpdater(*context, root);
    ActionRecorder recorder;

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(60.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(child, fixedSize(20.0F, 20.0F)));
    assertOk(updater.setPointerHitPolicy(child, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.setButtonAction(button, makeAction(recorder, 1)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIPointerRouteResult down = route(
        *context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_EQ(down.pointQuery.target.node, child);
    EXPECT_EQ(down.routeDepth, 3U);
    expectButtonPressed(updater, button, true);

    const UI::UIPointerRouteResult up = route(
        *context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_EQ(up.pointQuery.target.node, child);
    expectButtonPressed(updater, button, false);
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].button, button);
}

TEST_F(
    UIButtonActionTest,
    PropagationConsumptionAndClaimsRemainIndependentFromDefaultAction)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    auto token = addListener(
        *tree.context,
        {.node = tree.button,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            event.stopPropagation();
            event.consumeInputTransition();
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Secondary));
        }});
    ASSERT_TRUE(token);

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_TRUE(down.stopped);
    EXPECT_TRUE(down.consumed);
    EXPECT_TRUE(claimsPrimaryButton(down));
    EXPECT_TRUE(down.claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Secondary)));
    expectButtonPressed(tree.updater, tree.button, true);

    const UI::UIPointerRouteResult up = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_TRUE(up.consumed);
    expectButtonPressed(tree.updater, tree.button, false);
    ASSERT_EQ(recorder.size, 1U);
}

TEST_F(UIButtonActionTest, PreventDefaultBlocksArmAndUpActivationOnly)
{
    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
        auto token = addListener(
            *tree.context,
            {.node = tree.button,
             .kind = UI::UIRoutedPointerEventKind::ButtonDown,
             .phases = UI::UIEventPhaseMask::Target},
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
                EXPECT_FALSE(event.isDefaultActionPrevented());
                event.preventDefaultAction();
                EXPECT_TRUE(event.isDefaultActionPrevented());
            }});
        ASSERT_TRUE(token);

        const UI::UIPointerRouteResult down = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        EXPECT_FALSE(down.consumed);
        EXPECT_FALSE(claimsPrimaryButton(down));
        expectButtonPressed(tree.updater, tree.button, false);
        const UI::UIPointerRouteResult up = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_FALSE(up.consumed);
        EXPECT_EQ(recorder.size, 0U);
    }

    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
        auto token = addListener(
            *tree.context,
            {.node = tree.button,
             .kind = UI::UIRoutedPointerEventKind::ButtonUp,
             .phases = UI::UIEventPhaseMask::Target},
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
                event.preventDefaultAction();
            }});
        ASSERT_TRUE(token);

        const UI::UIPointerRouteResult down = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        ASSERT_TRUE(down.consumed);
        expectButtonPressed(tree.updater, tree.button, true);
        const UI::UIPointerRouteResult up = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_TRUE(up.consumed);
        expectButtonPressed(tree.updater, tree.button, false);
        EXPECT_EQ(recorder.size, 0U);
    }
}

TEST_F(UIButtonActionTest, SetReplaceClearActions)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;

    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].marker, 1);

    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 2)));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 3));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 4));
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].marker, 2);

    assertOk(tree.updater.clearButtonAction(tree.button));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 5));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 6));
    EXPECT_EQ(recorder.size, 2U);
    expectButtonPressed(tree.updater, tree.button, false);
}

TEST_F(UIButtonActionTest, RejectsWrongKindRootContextAndStaleButton)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    auto sameWindowContext = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    auto otherWindowContext = createContext(secondWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    ASSERT_NE(sameWindowContext, nullptr);
    ASSERT_NE(otherWindowContext, nullptr);

    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto foreignRoot = createRoot(*sameWindowContext);
    auto otherWindowRoot = createRoot(*otherWindowContext);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    ASSERT_TRUE(foreignRoot);
    ASSERT_TRUE(otherWindowRoot);
    const UI::UINodeId panel = createPanel(*context, firstRoot.rootNodeId());
    const UI::UINodeId label = createLabel(*context, panel);
    const UI::UINodeId button = createButton(*context, firstRoot.rootNodeId());
    const UI::UINodeId otherRootButton = createButton(*context, secondRoot.rootNodeId());
    const UI::UINodeId foreignButton =
        createButton(*sameWindowContext, foreignRoot.rootNodeId());
    const UI::UINodeId otherWindowButton =
        createButton(*otherWindowContext, otherWindowRoot.rootNodeId());
    const UI::UINodeId staleButton = createButton(*context, firstRoot.rootNodeId());
    auto updater = createUpdater(*context, firstRoot);
    ActionRecorder recorder;

    const auto expectInvalidButtonAction = [&](Core::Status status) {
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidButtonAction);
    };
    expectInvalidButtonAction(
        updater.setButtonAction(firstRoot.rootNodeId(), makeAction(recorder, 1)));
    expectInvalidButtonAction(updater.setButtonAction(panel, makeAction(recorder, 1)));
    expectInvalidButtonAction(updater.setButtonAction(label, makeAction(recorder, 1)));
    expectInvalidButtonAction(updater.clearButtonAction(panel));
    auto panelPressed = updater.isButtonPressed(panel);
    ASSERT_FALSE(panelPressed.has_value());
    EXPECT_EQ(panelPressed.error().code, UI::UIErrorCode::InvalidButtonAction);

    const Core::Status wrongRoot =
        updater.setButtonAction(otherRootButton, makeAction(recorder, 1));
    ASSERT_FALSE(wrongRoot.has_value());
    EXPECT_EQ(wrongRoot.error().code, UI::UIErrorCode::InvalidNode);

    const Core::Status wrongContext =
        updater.setButtonAction(foreignButton, makeAction(recorder, 1));
    ASSERT_FALSE(wrongContext.has_value());
    EXPECT_EQ(wrongContext.error().code, UI::UIErrorCode::WrongContext);

    const Core::Status wrongWindow =
        updater.setButtonAction(otherWindowButton, makeAction(recorder, 1));
    ASSERT_FALSE(wrongWindow.has_value());
    EXPECT_EQ(wrongWindow.error().code, UI::UIErrorCode::WrongOwnerWindow);

    assertOk(updater.destroy(staleButton));
    const Core::Status stale =
        updater.setButtonAction(staleButton, makeAction(recorder, 1));
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, UI::UIErrorCode::InvalidNode);

    assertOk(updater.setButtonAction(button, makeAction(recorder, 2)));
    expectButtonPressed(updater, button, false);
}

TEST_F(
    UIButtonActionTest,
    ButtonActionCapacityRejectsValuesAboveNodeCapacityWithoutUsingUiMemory)
{
    ObservingMemoryResource resource;
    const UI::UIContextCapacityConfig capacities{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .buttonActionCapacity = 5,
    };

    const Core::Status validation =
        UI::validateUIContextCapacityConfig(capacities);
    ASSERT_FALSE(validation.has_value());
    EXPECT_EQ(validation.error().code, UI::UIErrorCode::InvalidContextConfig);

    const auto context = UI::UIContext::Create(firstWindow, capacities, resource);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, UI::UIErrorCode::InvalidContextConfig);
    EXPECT_EQ(resource.allocationCount(), 0U);
    EXPECT_EQ(resource.deallocationCount(), 0U);
    EXPECT_EQ(resource.currentBytes(), 0U);
}

TEST_F(
    UIButtonActionTest,
    ButtonActionCapacityAllowsReplacingFullSlotAndReusingClearedSlot)
{
    auto context = createContext(
        firstWindow,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .routePathCapacity = 4,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId first = createButton(*context, root.rootNodeId());
    const UI::UINodeId second = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    ActionRecorder recorder;

    UI::UILayoutStyle row = fixedSize(100.0F, 100.0F);
    row.flex.direction = UI::UIFlexDirection::Column;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), row));
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 40.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    assertOk(updater.setButtonAction(first, makeAction(recorder, 1)));
    const Core::Status capacity =
        updater.setButtonAction(second, makeAction(recorder, 2));
    ASSERT_FALSE(capacity.has_value());
    EXPECT_EQ(capacity.error().code, UI::UIErrorCode::CapacityExceeded);

    assertOk(updater.setButtonAction(first, makeAction(recorder, 3)));
    route(*context, makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    route(*context, makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].marker, 3);

    assertOk(updater.clearButtonAction(first));
    assertOk(updater.setButtonAction(second, makeAction(recorder, 4)));
    route(
        *context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonDown,
            3,
            {.x = 10.0F, .y = 50.0F}));
    route(
        *context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonUp,
            4,
            {.x = 10.0F, .y = 50.0F}));
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].button, second);
    EXPECT_EQ(recorder.entries[1].marker, 4);
}

TEST_F(UIButtonActionTest, RouteMutationReplaceTakesNextRouteAndClearBlocksCurrentAction)
{
    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

        auto token = addListener(
            *tree.context,
            {.node = tree.button,
             .kind = UI::UIRoutedPointerEventKind::ButtonUp,
             .phases = UI::UIEventPhaseMask::Target},
            UI::UIRoutedPointerCallback{
                [&tree, &recorder](UI::UIRoutedPointerEvent&) noexcept {
                    expectOk(tree.updater.setButtonAction(
                        tree.button,
                        makeAction(recorder, 2)));
                }});
        ASSERT_TRUE(token);

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        ASSERT_EQ(recorder.size, 1U);
        EXPECT_EQ(recorder.entries[0].marker, 1);

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 3));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 4));
        ASSERT_EQ(recorder.size, 2U);
        EXPECT_EQ(recorder.entries[1].marker, 2);
    }

    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

        auto token = addListener(
            *tree.context,
            {.node = tree.button,
             .kind = UI::UIRoutedPointerEventKind::ButtonUp,
             .phases = UI::UIEventPhaseMask::Target},
            UI::UIRoutedPointerCallback{[&tree](UI::UIRoutedPointerEvent&) noexcept {
                expectOk(tree.updater.clearButtonAction(tree.button));
            }});
        ASSERT_TRUE(token);

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_EQ(recorder.size, 0U);
        expectButtonPressed(tree.updater, tree.button, false);
    }
}

TEST_F(UIButtonActionTest, ActionCallbackMayDestroyButtonOrRoot)
{
    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        usize callbackCount = 0;
        assertOk(tree.updater.setButtonAction(
            tree.button,
            UI::UIButtonActionCallback{[&tree, &callbackCount](
                                           const UI::UIButtonActionEvent&) noexcept {
                ++callbackCount;
                expectOk(tree.updater.destroy(tree.button));
            }}));

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_EQ(callbackCount, 1U);
        EXPECT_FALSE(tree.context->contains(tree.button));
        EXPECT_EQ(tree.context->liveNodeCount(), 2U);
    }

    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        usize callbackCount = 0;
        assertOk(tree.updater.setButtonAction(
            tree.button,
            UI::UIButtonActionCallback{[&tree, &callbackCount](
                                           const UI::UIButtonActionEvent&) noexcept {
                ++callbackCount;
                tree.root.reset();
            }}));

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_EQ(callbackCount, 1U);
        EXPECT_EQ(tree.context->liveRootCount(), 0U);
        EXPECT_EQ(tree.context->liveNodeCount(), 0U);
    }
}

TEST_F(UIButtonActionTest, ReentrantDefaultActionActivationIsRejectedAndGuardSurvives)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);

    struct State final {
        UI::UIContext* context = nullptr;
        Core::ErrorCode nestedError{};
        Core::ErrorCode commitError{};
        usize callbackCount = 0;
        bool nestedRejected = false;
    } state{
        .context = tree.context.get(),
    };

    assertOk(tree.updater.setButtonAction(
        tree.button,
        UI::UIButtonActionCallback{[&state](const UI::UIButtonActionEvent&) noexcept {
            ++state.callbackCount;
            if (state.callbackCount != 1U) {
                return;
            }
            auto nested = state.context->routeDefaultActionActivate(
                Platform::PlatformFrameId{2},
                20,
                UI::UIButtonActivationSource::Keyboard);
            state.nestedRejected = !nested.has_value();
            if (!nested) {
                state.nestedError = nested.error().code;
            }
            const Core::Status commit = state.context->commitLayout(
                {.width = 100.0F, .height = 100.0F});
            if (!commit) {
                state.commitError = commit.error().code;
            }
        }}));

    auto focus = tree.context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    ASSERT_EQ(tree.context->defaultActionFocus(), tree.button);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    assertOk(tree.updater.setBoxPaint(
        tree.button,
        UI::UIBoxPaint{
            .solidFill = UI::UISolidFill{
                .color = {.red = 68, .green = 85, .blue = 102, .alpha = 255},
            },
        }));
    const u64 paintRevisionBefore = tree.context->statistics().paintRevision;

    auto outer = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(outer.has_value()) << (outer ? "" : outer.error().message);
    EXPECT_TRUE(outer->consumed);
    EXPECT_TRUE(outer->activated);
    EXPECT_TRUE(state.nestedRejected);
    EXPECT_EQ(state.nestedError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.commitError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.callbackCount, 1U);
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.button);
    EXPECT_FALSE(buttonPressed(tree.updater, tree.button));
    EXPECT_EQ(tree.context->statistics().paintRevision, paintRevisionBefore);
}

TEST_F(UIButtonActionTest, CancelOrResetClearsPressedWithoutActivation)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    expectButtonPressed(tree.updater, tree.button, true);
    assertOk(tree.context->cancelPointerInteraction(firstWindow));
    expectButtonPressed(tree.updater, tree.button, false);

    const UI::UIPointerRouteResult up = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_FALSE(up.consumed);
    EXPECT_EQ(recorder.size, 0U);

    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 3));
    expectButtonPressed(tree.updater, tree.button, true);
    assertOk(tree.context->cancelPointerInteraction(firstWindow));
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 0U);
}

TEST_F(UIButtonActionTest, ThreeHundredRepeatedClicksDoNotGrowSuppliedPmr)
{
    ObservingMemoryResource resource;
    {
        ButtonTree tree = createButtonTree(
            firstWindow,
            {
                .nodeCapacity = 4,
                .rootCapacity = 1,
                .routePathCapacity = 4,
                .routedPointerListenerCapacity = 1,
                .buttonActionCapacity = 1,
            },
            resource);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

        const usize allocationCount = resource.allocationCount();
        for (u64 click = 0; click < 300; ++click) {
            const u64 downSequence = click * 2 + 1;
            const u64 upSequence = downSequence + 1;
            const UI::UIPointerRouteResult down = route(
                *tree.context,
                makePointerInput(
                    firstWindow,
                    UI::UIRoutedPointerEventKind::ButtonDown,
                    downSequence));
            ASSERT_TRUE(down.consumed) << "click=" << click;
            const UI::UIPointerRouteResult up = route(
                *tree.context,
                makePointerInput(
                    firstWindow,
                    UI::UIRoutedPointerEventKind::ButtonUp,
                    upSequence));
            ASSERT_TRUE(up.consumed) << "click=" << click;
            ASSERT_EQ(recorder.size, click + 1U);
        }

        EXPECT_EQ(resource.allocationCount(), allocationCount);
        EXPECT_GT(resource.currentBytes(), 0U);
    }
    EXPECT_EQ(resource.currentBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

TEST_F(UIButtonActionTest, KeyboardAndGamepadAcceptActivateDefaultFocusedButton)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 7)));

    // Without pointer arm, Accept does nothing and is not consumed.
    auto idle = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(idle.has_value()) << (idle ? "" : idle.error().message);
    EXPECT_FALSE(idle->consumed);
    EXPECT_FALSE(idle->activated);
    EXPECT_EQ(recorder.size, 0U);

    // Pointer down sets default-action focus (and arms).
    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    ASSERT_TRUE(down.consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    // Keyboard Accept activates once and does not require pointer Up.
    auto keyboard = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{2},
        20,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(keyboard.has_value()) << (keyboard ? "" : keyboard.error().message);
    EXPECT_TRUE(keyboard->consumed);
    EXPECT_TRUE(keyboard->activated);
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].source, UI::UIButtonActivationSource::Keyboard);
    EXPECT_EQ(recorder.entries[0].sourceSequence, 20U);
    EXPECT_EQ(recorder.entries[0].marker, 7);

    auto gamepad = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3},
        30,
        UI::UIButtonActivationSource::Gamepad);
    ASSERT_TRUE(gamepad.has_value()) << (gamepad ? "" : gamepad.error().message);
    EXPECT_TRUE(gamepad->consumed);
    EXPECT_TRUE(gamepad->activated);
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].source, UI::UIButtonActivationSource::Gamepad);
    EXPECT_EQ(recorder.entries[1].sourceSequence, 30U);
}

TEST_F(UIButtonActionTest, DisablingUnrelatedButtonPreservesDefaultActionPress)
{
    auto context = createContext(
        firstWindow,
        UI::UIContextCapacityConfig{.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId firstPanel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId firstButton = createButton(*context, firstPanel);
    const UI::UINodeId unrelatedButton = createButton(*context, root.rootNodeId());

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(firstPanel, fixedSize(50.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(firstButton, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(unrelatedButton, fixedSize(50.0F, 40.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, firstButton);

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto down = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    expectButtonPressed(updater, firstButton, true);

    assertOk(updater.setEnabled(unrelatedButton, false));
    EXPECT_EQ(context->defaultActionFocus(), firstButton);
    expectButtonPressed(updater, firstButton, true);

    auto up = context->routeDefaultActionRelease(
        Platform::PlatformFrameId{2},
        20,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    expectButtonPressed(updater, firstButton, false);
}

TEST_F(UIButtonActionTest, DisablingPressedButtonClearsPressAndQueuesDirtyState)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);

    auto focus = tree.context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto down = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    expectButtonPressed(tree.updater, tree.button, true);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(tree.context->statistics().dirtyQueuePendingCount, 0U);

    assertOk(tree.updater.setEnabled(tree.button, false));
    EXPECT_FALSE(tree.context->defaultActionFocus().hasValue());
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(tree.context->statistics().dirtyQueuePendingCount, 1U);
}

TEST_F(UIButtonActionTest, KeyboardAcceptPressTracksEachKeyUntilItsMatchingUp)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    auto focus = tree.context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    const Platform::DigitalControlIdentity space =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Space};
    auto enterDown = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 30, UI::UIButtonActivationSource::Keyboard, enter);
    ASSERT_TRUE(enterDown.has_value()) << (enterDown ? "" : enterDown.error().message);
    EXPECT_TRUE(enterDown->consumed);
    EXPECT_TRUE(enterDown->activated);
    expectButtonPressed(tree.updater, tree.button, true);

    auto spaceDown = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{4}, 40, UI::UIButtonActivationSource::Keyboard, space);
    ASSERT_TRUE(spaceDown.has_value()) << (spaceDown ? "" : spaceDown.error().message);
    EXPECT_TRUE(spaceDown->consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    auto enterUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{5}, 50, UI::UIButtonActivationSource::Keyboard, enter);
    ASSERT_TRUE(enterUp.has_value()) << (enterUp ? "" : enterUp.error().message);
    EXPECT_TRUE(enterUp->consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    auto spaceUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{6}, 60, UI::UIButtonActivationSource::Keyboard, space);
    ASSERT_TRUE(spaceUp.has_value()) << (spaceUp ? "" : spaceUp.error().message);
    EXPECT_TRUE(spaceUp->consumed);
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 2U);
}

TEST_F(UIButtonActionTest, KeyboardAndGamepadAcceptUpAreReleaseBarriersWhenDirtyQueueIsFull)
{
    ButtonTree tree = createButtonTree(
        firstWindow,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 3,
            .routePathCapacity = 8,
            .routedPointerListenerCapacity = 8,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(tree.context, nullptr);
    const UI::UINodeId blocker =
        createPanel(*tree.context, tree.root.rootNodeId());
    ASSERT_TRUE(blocker.hasValue());
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 9)));

    auto focus = tree.context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto down = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(down->activated);
    expectButtonPressed(tree.updater, tree.button, true);
    EXPECT_EQ(recorder.size, 1U);

    // Publish the pressed state before filling every remaining dirty slot.
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UIBoxPaint blockerPaint{
        .solidFill = UI::UISolidFill{
            .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        },
    };
    assertOk(tree.updater.setBoxPaint(tree.root.rootNodeId(), blockerPaint));
    assertOk(tree.updater.setBoxPaint(tree.panel, blockerPaint));
    assertOk(tree.updater.setBoxPaint(blocker, blockerPaint));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    auto up = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{2},
        20,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(up->activated);
    // Up is a release barrier: failure to repaint cannot resurrect the
    // logical pressed state or trigger another action.
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 1U);

    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.button);
    auto idleUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{3},
        30,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(idleUp.has_value()) << (idleUp ? "" : idleUp.error().message);
    EXPECT_FALSE(idleUp->consumed);
    EXPECT_EQ(recorder.size, 1U);

    auto gamepadsResult = GamepadPool::Create(1);
    ASSERT_TRUE(gamepadsResult.has_value());
    auto gamepads = std::make_unique<GamepadPool>(std::move(*gamepadsResult));
    auto gamepadResult = gamepads->tryEmplace(1);
    ASSERT_TRUE(gamepadResult.has_value());
    const Platform::DigitalControlIdentity south =
        Platform::GamepadButtonControlIdentity{
            .routedWindow = firstWindow,
            .gamepad = *gamepadResult,
            .button = Platform::GamepadButton::South,
        };
    auto gamepadDown = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{4},
        40,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(gamepadDown.has_value())
        << (gamepadDown ? "" : gamepadDown.error().message);
    EXPECT_TRUE(gamepadDown->consumed);
    EXPECT_TRUE(gamepadDown->activated);
    expectButtonPressed(tree.updater, tree.button, true);
    EXPECT_EQ(recorder.size, 2U);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIBoxPaint secondBlockerPaint{
        .solidFill = UI::UISolidFill{
            .color = {.red = 4, .green = 5, .blue = 6, .alpha = 255},
        },
    };
    assertOk(tree.updater.setBoxPaint(
        tree.root.rootNodeId(),
        secondBlockerPaint));
    assertOk(tree.updater.setBoxPaint(tree.panel, secondBlockerPaint));
    assertOk(tree.updater.setBoxPaint(blocker, secondBlockerPaint));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    auto gamepadUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{5},
        50,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(gamepadUp.has_value())
        << (gamepadUp ? "" : gamepadUp.error().message);
    EXPECT_TRUE(gamepadUp->consumed);
    EXPECT_FALSE(gamepadUp->activated);
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 2U);

    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    auto idleGamepadUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{6},
        60,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(idleGamepadUp.has_value())
        << (idleGamepadUp ? "" : idleGamepadUp.error().message);
    EXPECT_FALSE(idleGamepadUp->consumed);
    EXPECT_EQ(recorder.size, 2U);
}

TEST_F(UIButtonActionTest, KeyboardAcceptDownDirtyFailurePrecedesCallbackAndPress)
{
    ButtonTree tree = createButtonTree(
        firstWindow,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 3,
            .routePathCapacity = 8,
            .routedPointerListenerCapacity = 8,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
    const UI::UINodeId blocker =
        createPanel(*tree.context, tree.root.rootNodeId());
    ASSERT_TRUE(blocker.hasValue());
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto focus = tree.context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UIBoxPaint blockerPaint{
        .solidFill = UI::UISolidFill{
            .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        },
    };
    assertOk(tree.updater.setBoxPaint(tree.root.rootNodeId(), blockerPaint));
    assertOk(tree.updater.setBoxPaint(tree.panel, blockerPaint));
    assertOk(tree.updater.setBoxPaint(blocker, blockerPaint));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto rejected = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 30, UI::UIButtonActivationSource::Keyboard, enter);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 0U);
}

TEST_F(UIButtonActionTest, DefaultActionWithoutRegisteredCallbackConsumesButDoesNotActivate)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    ASSERT_TRUE(down.consumed);

    auto result = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{4},
        40,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    EXPECT_TRUE(result->consumed);
    EXPECT_FALSE(result->activated);
}

TEST_F(UIButtonActionTest, TabCyclesDefaultActionFocusAmongButtons)
{
    auto context = createContext(firstWindow);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 40.0F)));

    const UI::UINodeId first = createButton(*context, root.rootNodeId());
    const UI::UINodeId second = createButton(*context, root.rootNodeId());
    const UI::UINodeId third = createButton(*context, root.rootNodeId());
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(third, fixedSize(40.0F, 20.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 40.0F}));

    EXPECT_FALSE(context->defaultActionFocus().hasValue());

    auto step1 = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step1.has_value()) << (step1 ? "" : step1.error().message);
    EXPECT_TRUE(step1->consumed);
    EXPECT_TRUE(step1->moved);
    EXPECT_EQ(step1->focus, first);
    EXPECT_EQ(context->defaultActionFocus(), first);

    auto step2 = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step2.has_value());
    EXPECT_TRUE(step2->consumed);
    EXPECT_EQ(step2->focus, second);

    auto step3 = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step3.has_value());
    EXPECT_EQ(step3->focus, third);

    auto wrap = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(wrap.has_value());
    EXPECT_EQ(wrap->focus, first);

    auto reverse = context->routeDefaultActionFocusStep(true);
    ASSERT_TRUE(reverse.has_value());
    EXPECT_EQ(reverse->focus, third);
}

TEST_F(UIButtonActionTest, FocusStepDirtyQueueFailurePreservesPreviousFocus)
{
    auto context = createContext(
        firstWindow,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId first = createButton(*context, root.rootNodeId());
    const UI::UINodeId second = createButton(*context, root.rootNodeId());
    const UI::UINodeId blocker = createPanel(*context, root.rootNodeId());

    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(200.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 20.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 20.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));

    auto initialFocus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(initialFocus.has_value())
        << (initialFocus ? "" : initialFocus.error().message);
    ASSERT_EQ(initialFocus->focus, first);
    EXPECT_EQ(context->defaultActionFocus(), first);
    EXPECT_FALSE(context->imeFocus().hasValue());
    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));

    assertOk(updater.setPointerHitPolicy(
        blocker,
        UI::UIPointerHitPolicy::Targetable));
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 1U);

    auto rejected = context->routeDefaultActionFocusStep(false);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->defaultActionFocus(), first);
    EXPECT_FALSE(context->imeFocus().hasValue());
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 1U);

    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));
    auto retried = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(retried.has_value()) << (retried ? "" : retried.error().message);
    EXPECT_TRUE(retried->consumed);
    EXPECT_TRUE(retried->moved);
    EXPECT_EQ(retried->focus, second);
    EXPECT_EQ(context->defaultActionFocus(), second);
}

} // namespace
} // namespace Tina::Tests
