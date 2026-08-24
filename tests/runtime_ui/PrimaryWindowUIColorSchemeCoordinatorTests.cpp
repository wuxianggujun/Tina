#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include "../../src/runtime/ui/PrimaryWindowUIColorSchemeCoordinator.hpp"

#include <array>
#include <memory>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using Coordinator = Runtime::Detail::PrimaryWindowUIColorSchemeCoordinator;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext()
{
    auto pool = WindowPool::Create(1);
    if (!pool)
    {
        return {};
    }
    auto window = pool->tryEmplace(1);
    if (!window)
    {
        return {};
    }
    auto context = UI::UIContext::Create(*window);
    return context ? std::move(*context) : nullptr;
}

[[nodiscard]] Platform::PlatformEvent colorSchemeEvent(
    u64 sequence, Platform::SystemColorScheme colorScheme) noexcept
{
    return Platform::PlatformEvent{
        .sequence = sequence,
        .payload = Platform::SystemColorSchemeChangedEvent{.colorScheme = colorScheme},
    };
}

TEST(PrimaryWindowUIColorSchemeCoordinatorTests, AppliesLatestPreferenceAndPreservesDensity)
{
    std::unique_ptr<UI::UIContext> context = createContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->style().setProductTheme(UI::makeModernDesktopTheme(
        UI::UIColorScheme::Dark, UI::UIDensity::Comfortable)));
    auto root = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());

    Coordinator coordinator;
    const std::array events{
        colorSchemeEvent(1, Platform::SystemColorScheme::Dark),
        colorSchemeEvent(2, Platform::SystemColorScheme::Light),
    };
    coordinator.observe(events);
    ASSERT_TRUE(coordinator.apply(context.get()));
    EXPECT_EQ(context->style().productTheme().colorScheme, UI::UIColorScheme::Light);
    EXPECT_EQ(context->style().productTheme().density, UI::UIDensity::Comfortable);
}

TEST(PrimaryWindowUIColorSchemeCoordinatorTests, IgnoresIncompleteResetBatch)
{
    std::unique_ptr<UI::UIContext> context = createContext();
    ASSERT_NE(context, nullptr);

    Coordinator coordinator;
    const std::array resetBatch{
        colorSchemeEvent(1, Platform::SystemColorScheme::Light),
        Platform::PlatformEvent{
            .sequence = 2,
            .payload = Platform::PlatformEventStreamReset{
                .reason = Platform::PlatformEventResetReason::CapacityExceeded,
            },
        },
    };
    coordinator.observe(resetBatch);
    ASSERT_TRUE(coordinator.apply(context.get()));
    EXPECT_EQ(context->style().productTheme().colorScheme, UI::UIColorScheme::Dark);

    const std::array retry{colorSchemeEvent(3, Platform::SystemColorScheme::Light)};
    coordinator.observe(retry);
    ASSERT_TRUE(coordinator.apply(context.get()));
    EXPECT_EQ(context->style().productTheme().colorScheme, UI::UIColorScheme::Light);
}

TEST(PrimaryWindowUIColorSchemeCoordinatorTests, DefersUntilAWindowContextExists)
{
    Coordinator coordinator;
    const std::array events{colorSchemeEvent(1, Platform::SystemColorScheme::Light)};
    coordinator.observe(events);
    ASSERT_TRUE(coordinator.apply(nullptr));

    std::unique_ptr<UI::UIContext> context = createContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(coordinator.apply(context.get()));
    EXPECT_EQ(context->style().productTheme().colorScheme, UI::UIColorScheme::Light);
}

} // namespace
} // namespace Tina::Tests
