#pragma once

#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {

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

[[nodiscard]] inline std::unique_ptr<UI::UIContext> createContext(
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
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] inline UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] inline UI::UINodeId createPanel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] inline UI::UINodeId createLabel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makeLabelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] inline UI::UINodeId createButton(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makeButtonElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] inline UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] inline UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

inline void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

inline void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] inline UI::UIPointerInputEvent makePointerInput(
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

inline UI::UIPointerRouteResult route(
    UI::UIContext& context,
    const UI::UIPointerInputEvent& input)
{
    auto result = context.routePointerInput(input);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UIPointerRouteResult{};
}

[[nodiscard]] inline bool claimsPrimaryButton(const UI::UIPointerRouteResult& result) noexcept
{
    return result.claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Primary));
}

[[nodiscard]] inline bool buttonPressed(
    UI::UITreeUpdater& updater,
    UI::UINodeId button)
{
    auto result = updater.isButtonPressed(button);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : false;
}

inline void expectButtonPressed(
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

[[nodiscard]] inline UI::UIButtonActionCallback makeAction(
    ActionRecorder& recorder,
    int marker) noexcept
{
    return UI::UIButtonActionCallback{
        [&recorder, marker](const UI::UIButtonActionEvent& event) noexcept {
            recorder.push(event, marker);
        }};
}

[[nodiscard]] inline UI::UIRoutedPointerListenerToken addListener(
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

[[nodiscard]] inline ButtonTree createButtonTree(
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

} // namespace Tina::Tests
