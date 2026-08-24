#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/ui/UI.hpp>

#include "../../src/runtime/ui/PrimaryWindowTextInputPlacementCoordinator.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using Coordinator = Runtime::Detail::PrimaryWindowTextInputPlacementCoordinator;

class PlacementBackend final : public Platform::IPlatformBackend {
  public:
    [[nodiscard]] Core::Result<std::optional<Platform::WindowMetricsSnapshot>>
    initialPrimaryWindowMetrics() override
    {
        return std::optional<Platform::WindowMetricsSnapshot>{};
    }

    [[nodiscard]] Core::Result<Platform::PlatformPollResult> pollFrame() override
    {
        return Platform::PlatformPollResult::Exit();
    }

    Core::Status updateTextInputPlacement(
        std::optional<Platform::TextInputPlacement> placement) override
    {
        ++publishCount;
        lastPlacement = std::move(placement);
        if (failPublish)
        {
            return Core::failure(Platform::PlatformErrorCode::BackendOperationFailed,
                                 "Injected placement publication failure");
        }
        return Core::success();
    }

    void shutdown() noexcept override {}

    std::optional<Platform::TextInputPlacement> lastPlacement{};
    usize publishCount = 0;
    bool failPublish = false;
};

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

TEST(PrimaryWindowTextInputPlacementCoordinatorTests, MapsCommittedCaretAndPublishesClear)
{
    auto windowPool = WindowPool::Create(1);
    ASSERT_TRUE(windowPool.has_value());
    auto windowResult = windowPool->tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    const Platform::WindowId window = *windowResult;

    auto contextResult = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 8,
            .textByteCapacity = 32,
            .applyDefaultProductChrome = false,
        });
    ASSERT_TRUE(contextResult.has_value())
        << (contextResult ? "" : contextResult.error().message);
    std::unique_ptr<UI::UIContext> context = std::move(*contextResult);
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    ASSERT_TRUE(updater.setLayoutStyle(root.rootNodeId(), fixedSize(120.0F, 40.0F)).has_value());
    auto textEditResult = updater.createElement(root.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value());
    const UI::UINodeId textEdit = *textEditResult;
    ASSERT_TRUE(updater.setLayoutStyle(textEdit, fixedSize(100.0F, 24.0F)).has_value());
    ASSERT_TRUE(updater.setText(textEdit, "AB").has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 120.0F, .height = 40.0F}).has_value());
    ASSERT_TRUE(context->input().requestFocus(textEdit).has_value());
    ASSERT_TRUE(updater.setTextSelection(
        textEdit, {.anchorCodepoint = 2, .caretCodepoint = 2}).has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 120.0F, .height = 40.0F}).has_value());
    const std::optional<UI::UILogicalRect> caret = context->publication().committedTextInputCaretRect();
    ASSERT_TRUE(caret.has_value());

    Coordinator coordinator;
    PlacementBackend backend;
    ASSERT_TRUE(coordinator.publish(context.get(), backend).has_value());
    ASSERT_EQ(backend.publishCount, 1U);
    ASSERT_TRUE(backend.lastPlacement.has_value());
    EXPECT_EQ(backend.lastPlacement->window, window);
    EXPECT_DOUBLE_EQ(backend.lastPlacement->caret.x, static_cast<double>(caret->x));
    EXPECT_DOUBLE_EQ(backend.lastPlacement->caret.y, static_cast<double>(caret->y));
    EXPECT_DOUBLE_EQ(backend.lastPlacement->caret.width, static_cast<double>(caret->width));
    EXPECT_DOUBLE_EQ(backend.lastPlacement->caret.height, static_cast<double>(caret->height));

    ASSERT_TRUE(context->input().clearFocus().has_value());
    ASSERT_TRUE(context->publication().commitLayout({.width = 120.0F, .height = 40.0F}).has_value());
    ASSERT_TRUE(coordinator.publish(context.get(), backend).has_value());
    EXPECT_EQ(backend.publishCount, 2U);
    EXPECT_FALSE(backend.lastPlacement.has_value());

    ASSERT_TRUE(coordinator.publish(nullptr, backend).has_value());
    EXPECT_EQ(backend.publishCount, 3U);
    EXPECT_FALSE(backend.lastPlacement.has_value());
}

TEST(PrimaryWindowTextInputPlacementCoordinatorTests, PreservesBackendErrorAndAddsBoundaryContext)
{
    Coordinator coordinator;
    PlacementBackend backend;
    backend.failPublish = true;

    const Core::Status status = coordinator.publish(nullptr, backend);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Platform::PlatformErrorCode::BackendOperationFailed);
    ASSERT_FALSE(status.error().context.empty());
    EXPECT_EQ(status.error().context.back().operation,
              "PrimaryWindowTextInputPlacementCoordinator::publish");
    EXPECT_EQ(status.error().context.back().detail,
              "IPlatformBackend::updateTextInputPlacement");
}

} // namespace
} // namespace Tina::Tests
