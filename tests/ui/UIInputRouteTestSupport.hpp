#pragma once

#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests::UIInputRouteTestSupport {

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

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
};

[[nodiscard]] inline UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] inline std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] inline UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.authoring().rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] inline UI::UINodeId createPanel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.authoring().rootBuilder().createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] inline UI::UINodeId createButton(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.authoring().rootBuilder().createElement(parent, UI::makeButtonElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] inline UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.authoring().treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

inline void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] inline UI::UIRoutedPointerListenerToken addListener(
    UI::UIContext& context,
    UI::UIRoutedPointerListenerDesc descriptor,
    UI::UIRoutedPointerCallback callback)
{
    auto result = context.input().addRoutedPointerListener(
        descriptor,
        std::move(callback));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRoutedPointerListenerToken{};
}

struct RouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId panel{};
    UI::UINodeId target{};
};

[[nodiscard]] inline RouteTree createRouteTree(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 16,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    RouteTree tree;
    tree.context = createContext(window, capacities, resource);
    if (!tree.context) {
        return tree;
    }

    tree.root = createRoot(*tree.context);
    if (!tree.root) {
        return tree;
    }
    tree.panel = createPanel(*tree.context, tree.root.rootNodeId());
    tree.target = createButton(*tree.context, tree.panel);
    tree.updater = createUpdater(*tree.context, tree.root);

    expectOk(tree.updater.setLayoutStyle(
        tree.root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.panel, fixedSize(80.0F, 80.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.target, fixedSize(40.0F, 40.0F)));
    expectOk(tree.updater.setPointerHitPolicy(
        tree.target,
        UI::UIPointerHitPolicy::Targetable));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    return tree;
}

[[nodiscard]] inline UI::UIPointerInputEvent makePointerInput(
    Platform::WindowId window,
    u64 sequence = 1) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::ButtonDown,
        .position = {.x = 10.0F, .y = 10.0F},
        .delta = {},
        .button = Platform::PointerButton::Primary,
    };
}

struct TraceEntry final {
    UI::UINodeId node{};
    UI::UIEventPhase phase = UI::UIEventPhase::Target;
    int marker = 0;
};

struct RouteTrace final {
    void push(const UI::UIRoutedPointerEvent& event, int marker) noexcept
    {
        if (size < entries.size()) {
            entries[size++] = TraceEntry{
                .node = event.currentNode(),
                .phase = event.currentPhase(),
                .marker = marker,
            };
        }
    }

    std::array<TraceEntry, 16> entries{};
    usize size = 0;
};

class UIInputRouteTest : public testing::Test {
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

} // namespace Tina::Tests::UIInputRouteTestSupport
