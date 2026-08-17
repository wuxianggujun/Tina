#include "GlfwTextInputPlacement.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformErrors.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Platform::WindowId createWindow(WindowPool& pool)
{
    auto window = pool.tryEmplace(1);
    EXPECT_TRUE(window.has_value());
    return window ? *window : Platform::WindowId{};
}

[[nodiscard]] Platform::WindowMetricsSnapshot metricsFor(
    Platform::WindowId window) noexcept
{
    return Platform::WindowMetricsSnapshot{
        .window = window,
        .logicalExtent = {800, 600},
        .framebufferExtent = {1600, 900},
        .contentScale = {2.0F, 1.5F},
        .revision = 1,
        .focused = true,
        .visible = true,
    };
}

TEST(GlfwTextInputPlacementTests, ConvertsLogicalCaretToScaledClientPixels)
{
    auto pool = WindowPool::Create(1);
    ASSERT_TRUE(pool.has_value());
    const Platform::WindowId window = createWindow(*pool);

    auto placement = Platform::Detail::resolveGlfwTextInputPlacement(
        Platform::TextInputPlacement{
            .window = window,
            .caret = {.x = 10.0, .y = 20.0, .width = 5.0, .height = 10.0},
        },
        metricsFor(window));

    ASSERT_TRUE(placement.has_value())
        << (placement ? "" : placement.error().message);
    EXPECT_EQ(placement->caretLeft, 20);
    EXPECT_EQ(placement->caretTop, 30);
    EXPECT_EQ(placement->caretRight, 30);
    EXPECT_EQ(placement->caretBottom, 45);
    EXPECT_EQ(placement->candidateX, 20);
    EXPECT_EQ(placement->candidateY, 45);
}

TEST(GlfwTextInputPlacementTests, ClampsCaretToClientBoundsBeforeScaling)
{
    auto pool = WindowPool::Create(1);
    ASSERT_TRUE(pool.has_value());
    const Platform::WindowId window = createWindow(*pool);

    auto placement = Platform::Detail::resolveGlfwTextInputPlacement(
        Platform::TextInputPlacement{
            .window = window,
            .caret = {.x = -20.0, .y = 590.0, .width = 1000.0, .height = 30.0},
        },
        metricsFor(window));

    ASSERT_TRUE(placement.has_value());
    EXPECT_EQ(placement->caretLeft, 0);
    EXPECT_EQ(placement->caretTop, 885);
    EXPECT_EQ(placement->caretRight, 1600);
    EXPECT_EQ(placement->caretBottom, 900);
    EXPECT_EQ(placement->candidateX, 0);
    EXPECT_EQ(placement->candidateY, 900);
}

TEST(GlfwTextInputPlacementTests, RejectsWrongWindowAndInvalidGeometry)
{
    auto pool = WindowPool::Create(2);
    ASSERT_TRUE(pool.has_value());
    const Platform::WindowId window = createWindow(*pool);
    const Platform::WindowId otherWindow = createWindow(*pool);
    const Platform::WindowMetricsSnapshot metrics = metricsFor(window);

    auto wrongWindow = Platform::Detail::resolveGlfwTextInputPlacement(
        Platform::TextInputPlacement{
            .window = otherWindow,
            .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 16.0},
        },
        metrics);
    ASSERT_FALSE(wrongWindow.has_value());
    EXPECT_EQ(wrongWindow.error().code, Core::CoreErrorCode::InvalidArgument);

    auto notFinite = Platform::Detail::resolveGlfwTextInputPlacement(
        Platform::TextInputPlacement{
            .window = window,
            .caret = {.x = (std::numeric_limits<double>::quiet_NaN)(),
                      .y = 2.0, .width = 2.0, .height = 16.0},
        },
        metrics);
    ASSERT_FALSE(notFinite.has_value());
    EXPECT_EQ(notFinite.error().code, Core::CoreErrorCode::InvalidArgument);

    auto zeroHeight = Platform::Detail::resolveGlfwTextInputPlacement(
        Platform::TextInputPlacement{
            .window = window,
            .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 0.0},
        },
        metrics);
    ASSERT_FALSE(zeroHeight.has_value());
    EXPECT_EQ(zeroHeight.error().code, Core::CoreErrorCode::InvalidArgument);
}

TEST(GlfwTextInputPlacementTests, RejectsSuspendedOrInvalidScaleMetrics)
{
    auto pool = WindowPool::Create(1);
    ASSERT_TRUE(pool.has_value());
    const Platform::WindowId window = createWindow(*pool);
    Platform::WindowMetricsSnapshot metrics = metricsFor(window);
    metrics.logicalExtent = {};

    auto suspended = Platform::Detail::resolveGlfwTextInputPlacement(
        Platform::TextInputPlacement{
            .window = window,
            .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 16.0},
        },
        metrics);
    ASSERT_FALSE(suspended.has_value());
    EXPECT_EQ(suspended.error().code, Platform::PlatformErrorCode::BackendOperationFailed);

    metrics = metricsFor(window);
    metrics.contentScale.x = 0.0F;
    auto invalidScale = Platform::Detail::resolveGlfwTextInputPlacement(
        Platform::TextInputPlacement{
            .window = window,
            .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 16.0},
        },
        metrics);
    ASSERT_FALSE(invalidScale.has_value());
    EXPECT_EQ(invalidScale.error().code, Platform::PlatformErrorCode::BackendOperationFailed);
}

} // namespace
} // namespace Tina::Tests
