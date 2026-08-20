#include <gtest/gtest.h>

#include "GlfwSystemColorSchemeObserver.hpp"

#include <array>
#include <optional>

namespace Tina::Platform::Tests {
namespace {

std::optional<SystemColorScheme> g_observedColorScheme{};

[[nodiscard]] std::optional<SystemColorScheme> queryTestColorScheme() noexcept
{
    return g_observedColorScheme;
}

TEST(GlfwSystemColorSchemeObserverTests, DisabledObserverNeverPublishes)
{
    g_observedColorScheme = SystemColorScheme::Light;
    Detail::GlfwSystemColorSchemeObserver observer(false, queryTestColorScheme);
    EXPECT_FALSE(observer.pendingPreference().has_value());
}

TEST(GlfwSystemColorSchemeObserverTests, PublishesInitialAndChangedPreferencesOnce)
{
    g_observedColorScheme = SystemColorScheme::Light;
    Detail::GlfwSystemColorSchemeObserver observer(true, queryTestColorScheme);

    ASSERT_EQ(observer.pendingPreference(), SystemColorScheme::Light);
    const std::array lightEvents{
        PlatformEvent{
            .sequence = 1,
            .payload = SystemColorSchemeChangedEvent{
                .colorScheme = SystemColorScheme::Light,
            },
        },
    };
    observer.commitPublishedPreference(SystemColorScheme::Light, lightEvents);
    EXPECT_FALSE(observer.pendingPreference().has_value());

    g_observedColorScheme = SystemColorScheme::Dark;
    ASSERT_EQ(observer.pendingPreference(), SystemColorScheme::Dark);
    const std::array darkEvents{
        PlatformEvent{
            .sequence = 2,
            .payload = SystemColorSchemeChangedEvent{
                .colorScheme = SystemColorScheme::Dark,
            },
        },
    };
    observer.commitPublishedPreference(SystemColorScheme::Dark, darkEvents);
    EXPECT_FALSE(observer.pendingPreference().has_value());
}

TEST(GlfwSystemColorSchemeObserverTests, ResetBatchDoesNotCommitPublishedPreference)
{
    g_observedColorScheme = SystemColorScheme::Light;
    Detail::GlfwSystemColorSchemeObserver observer(true, queryTestColorScheme);
    ASSERT_EQ(observer.pendingPreference(), SystemColorScheme::Light);

    const std::array resetBatch{
        PlatformEvent{
            .sequence = 1,
            .payload = SystemColorSchemeChangedEvent{
                .colorScheme = SystemColorScheme::Light,
            },
        },
        PlatformEvent{
            .sequence = 2,
            .payload = PlatformEventStreamReset{
                .reason = PlatformEventResetReason::CapacityExceeded,
            },
        },
    };
    observer.commitPublishedPreference(SystemColorScheme::Light, resetBatch);
    EXPECT_EQ(observer.pendingPreference(), SystemColorScheme::Light);
}

TEST(GlfwSystemColorSchemeObserverTests, MissingPreferenceRemainsDeterministic)
{
    g_observedColorScheme.reset();
    Detail::GlfwSystemColorSchemeObserver observer(true, queryTestColorScheme);
    EXPECT_FALSE(observer.pendingPreference().has_value());
}

} // namespace
} // namespace Tina::Platform::Tests
