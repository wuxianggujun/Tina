#pragma once

#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests::UILayoutTestSupport {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept { return m_allocationCount; }
    [[nodiscard]] usize deallocationCount() const noexcept { return m_deallocationCount; }
    [[nodiscard]] usize currentBytes() const noexcept { return m_currentBytes; }
    [[nodiscard]] usize peakBytes() const noexcept { return m_peakBytes; }

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        m_currentBytes += bytes;
        m_peakBytes = (std::max)(m_peakBytes, m_currentBytes);
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++m_deallocationCount;
        m_currentBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    usize m_deallocationCount = 0;
    usize m_currentBytes = 0;
    usize m_peakBytes = 0;
};

[[nodiscard]] inline std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId ownerWindow,
    UI::UIContextCapacityConfig capacities = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.applyDefaultProductChrome = false;
    auto contextResult = UI::UIContext::Create(ownerWindow, capacities, resource);
    EXPECT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    return contextResult ? std::move(*contextResult) : nullptr;
}

[[nodiscard]] inline UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto rootResult = context.authoring().rootBuilder().createRoot();
    EXPECT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
    return rootResult ? std::move(*rootResult) : UI::UIRootOwner{};
}

[[nodiscard]] inline UI::UINodeId createPanel(UI::UIContext& context, UI::UINodeId parent)
{
    auto panelResult = context.authoring().rootBuilder().createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(panelResult.has_value()) << (panelResult ? "" : panelResult.error().message);
    return panelResult ? *panelResult : UI::UINodeId{};
}

[[nodiscard]] inline UI::UINodeId createLabel(UI::UIContext& context, UI::UINodeId parent)
{
    auto labelResult = context.authoring().rootBuilder().createElement(parent, UI::makeLabelElement());
    EXPECT_TRUE(labelResult.has_value()) << (labelResult ? "" : labelResult.error().message);
    return labelResult ? *labelResult : UI::UINodeId{};
}

[[nodiscard]] inline UI::UINodeId createButton(UI::UIContext& context, UI::UINodeId parent)
{
    auto buttonResult = context.authoring().rootBuilder().createElement(parent, UI::makeButtonElement());
    EXPECT_TRUE(buttonResult.has_value()) << (buttonResult ? "" : buttonResult.error().message);
    return buttonResult ? *buttonResult : UI::UINodeId{};
}

[[nodiscard]] inline UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto updaterResult = context.authoring().treeUpdater(root);
    EXPECT_TRUE(updaterResult.has_value()) << (updaterResult ? "" : updaterResult.error().message);
    return updaterResult ? std::move(*updaterResult) : UI::UITreeUpdater{};
}

[[nodiscard]] inline UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] inline UI::UILayoutStyle percentSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Percent(width);
    style.size.height = UI::UILayoutLength::Percent(height);
    return style;
}

inline void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] inline const UI::UICommittedLayoutEntry* findLayoutEntry(
    UI::UICommittedLayoutView view,
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

inline const UI::UICommittedLayoutEntry& requireLayoutEntry(
    UI::UICommittedLayoutView view,
    UI::UINodeId node)
{
    const UI::UICommittedLayoutEntry* entry = findLayoutEntry(view, node);
    EXPECT_NE(entry, nullptr);
    static const UI::UICommittedLayoutEntry EmptyEntry{};
    return entry != nullptr ? *entry : EmptyEntry;
}

inline void expectRectNear(UI::UILogicalRect actual, UI::UILogicalRect expected)
{
    constexpr float Epsilon = 0.001F;
    EXPECT_NEAR(actual.x, expected.x, Epsilon);
    EXPECT_NEAR(actual.y, expected.y, Epsilon);
    EXPECT_NEAR(actual.width, expected.width, Epsilon);
    EXPECT_NEAR(actual.height, expected.height, Epsilon);
}

class UILayoutTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));

        auto windowResult = windows->tryEmplace(7);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    [[nodiscard]] std::unique_ptr<UI::UIContext> makeContext(
        UI::UIContextCapacityConfig capacities = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource()) const
    {
        return createContext(window, capacities, resource);
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

} // namespace Tina::Tests::UILayoutTestSupport
