#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UI.hpp>

#include <chrono>
#include <memory>
#include <ranges>

namespace Tina::Tests {
namespace {

class FakeSnackbarClock final : public Core::IMonotonicClock {
  public:
    [[nodiscard]] Core::MonotonicTimePoint now() const noexcept override
    {
        return now_;
    }

    void advance(Core::Duration delta) noexcept
    {
        now_ += std::chrono::duration_cast<Core::MonotonicDuration>(delta);
    }

  private:
    Core::MonotonicTimePoint now_{};
};

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] const UI::UISemanticsEntry* findSemantics(
    UI::UICommittedSemanticsView view, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view, [node](const UI::UISemanticsEntry& entry) {
            return entry.node == node;
        });
    return found == view.end() ? nullptr : &*found;
}

TEST(UISnackbarTests, ValidatesConfigTextAndActionContracts)
{
    auto invalid = UI::UISnackbarHost::Create(UI::UISnackbarHostConfig{
        .queueCapacity = 0,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, UI::UIErrorCode::InvalidElementDescriptor);

    auto host = UI::UISnackbarHost::Create({});
    ASSERT_TRUE(host.has_value());
    FakeSnackbarClock clock;
    EXPECT_EQ(host->enqueue({.text = "\xFF"}, clock.now()).error().code,
              UI::UIErrorCode::InvalidText);
    EXPECT_EQ(host->enqueue({.text = "Action", .actionToken = 1}, clock.now())
                  .error()
                  .code,
              UI::UIErrorCode::InvalidElementDescriptor);
}

TEST(UISnackbarTests, IsFifoTimedAndActionDoesNotRequestFocus)
{
    UI::UISnackbarHostConfig config{};
    config.queueCapacity = 2;
    config.enterDuration = Core::Duration{0.1};
    config.exitDuration = Core::Duration{0.1};
    config.defaultVisibleDuration = Core::Duration{0.5};
    auto host = UI::UISnackbarHost::Create(config);
    ASSERT_TRUE(host.has_value());
    FakeSnackbarClock clock;

    ASSERT_TRUE(host->enqueue(
        {.text = "Saved", .actionLabel = "Undo", .actionToken = 7},
        clock.now()));
    ASSERT_TRUE(host->enqueue({.text = "Imported"}, clock.now()));
    EXPECT_EQ(host->queuedCount(), 2U);
    EXPECT_EQ(host->presentation().phase, UI::UISnackbarPhase::Entering);
    EXPECT_TRUE(host->presentation().hasAction());

    clock.advance(Core::Duration{0.1});
    EXPECT_TRUE(host->update(clock.now()));
    EXPECT_EQ(host->presentation().phase, UI::UISnackbarPhase::Visible);
    EXPECT_EQ(host->activateAction(clock.now()), std::optional<u64>{7});
    EXPECT_EQ(host->presentation().phase, UI::UISnackbarPhase::Exiting);

    clock.advance(Core::Duration{0.1});
    EXPECT_TRUE(host->update(clock.now()));
    EXPECT_EQ(host->presentation().text, "Imported");
}

TEST(UISnackbarTests, QueueCapacityIsBoundedWithoutMutationOnFailure)
{
    UI::UISnackbarHostConfig config{};
    config.queueCapacity = 1;
    auto host = UI::UISnackbarHost::Create(config);
    ASSERT_TRUE(host.has_value());
    FakeSnackbarClock clock;
    ASSERT_TRUE(host->enqueue({.text = "First"}, clock.now()));
    const auto rejected = host->enqueue({.text = "Second"}, clock.now());
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(host->queuedCount(), 1U);
    EXPECT_EQ(host->presentation().text, "First");
}

TEST(UISnackbarTests, RecipePublishesPoliteLiveRegionWithoutMovingFocus)
{
    const auto budget = UI::requiredSnackbarHostBuildBudget({});
    ASSERT_TRUE(budget.has_value());
    EXPECT_EQ(*budget, (UI::UIComponentBuildBudget{
                           .nodes = 5,
                           .textBytes = 6,
                           .behaviors = {.activate = 1},
                       }));

    auto pool = WindowPool::Create(1);
    ASSERT_TRUE(pool.has_value());
    auto window = pool->tryEmplace(0);
    ASSERT_TRUE(window.has_value());
    auto context = UI::UIContext::Create(
        *window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 8,
            .hitSnapshotCapacity = 8,
            .paintSnapshotCapacity = 24,
            .textByteCapacity = 64,
        });
    ASSERT_TRUE(context.has_value());
    auto rootResult = (*context)->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = (*context)->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    auto parts = updater.buildSnackbarHost(root.rootNodeId(), {});
    ASSERT_TRUE(parts.has_value());
    UI::UILayoutStyle visibleHost{};
    visibleHost.placement = UI::UILayoutPlacement::Overlay;
    visibleHost.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    visibleHost.overlay.vertical = UI::UIAxisAlignment::Stretch;
    visibleHost.visibility = UI::UIVisibility::Visible;
    ASSERT_TRUE(updater.setLayoutStyle(parts->root, visibleHost));
    ASSERT_TRUE((*context)->publication().commitLayout({.width = 640.0F, .height = 360.0F}));

    const UI::UISemanticsEntry* message =
        findSemantics((*context)->publication().committedSemantics(), parts->message);
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->liveSetting, UI::UISemanticsLiveSetting::Polite);
    EXPECT_EQ((*context)->input().defaultActionFocus(), UI::UINodeId{});
}

TEST(UISnackbarTests, RecipePreflightFailureLeavesExistingTreeUntouched)
{
    auto pool = WindowPool::Create(1);
    ASSERT_TRUE(pool.has_value());
    auto window = pool->tryEmplace(0);
    ASSERT_TRUE(window.has_value());
    auto context = UI::UIContext::Create(
        *window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 8,
            .hitSnapshotCapacity = 8,
            .paintSnapshotCapacity = 24,
            .textByteCapacity = 5,
        });
    ASSERT_TRUE(context.has_value());
    auto rootResult = (*context)->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = (*context)->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    const usize baselineNodes = (*context)->liveNodeCount();
    const auto rejected =
        updaterResult->buildSnackbarHost(root.rootNodeId(), {});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ((*context)->liveNodeCount(), baselineNodes);
}

} // namespace
} // namespace Tina::Tests
