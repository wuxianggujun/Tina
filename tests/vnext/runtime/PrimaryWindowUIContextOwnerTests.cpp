#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>

#include "../../../src/runtime/ui/PrimaryWindowUIContextOwner.hpp"
#include "../../../src/runtime/ui/PrimaryWindowUILayoutCoordinator.hpp"

#include <algorithm>
#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using PrimaryWindowUIContextOwner = Runtime::Detail::PrimaryWindowUIContextOwner;
using PrimaryWindowUILayoutCoordinator = Runtime::Detail::PrimaryWindowUILayoutCoordinator;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

struct WindowFrameSpec final {
    Platform::WindowId window{};
    Platform::LogicalExtent logicalExtent{1280, 720};
    Platform::FramebufferExtent framebufferExtent{1280, 720};
    Platform::ContentScale contentScale{1.0F, 1.0F};
    bool focused = true;
    bool minimized = false;
    bool visible = true;
};

[[nodiscard]] Platform::WindowMetricsSnapshot windowMetrics(const WindowFrameSpec& window, u64 revision = 1)
{
    return {
        .window = window.window,
        .logicalExtent = window.logicalExtent,
        .framebufferExtent = window.framebufferExtent,
        .contentScale = window.contentScale,
        .revision = revision,
        .focused = window.focused,
        .minimized = window.minimized,
        .visible = window.visible,
    };
}

[[nodiscard]] Core::Result<Platform::PlatformFrameView> buildFrame(Platform::PlatformFrameBuilder& builder, u64 frameId,
                                                                   std::optional<WindowFrameSpec> window = std::nullopt)
{
    if (auto status = builder.beginFrame({frameId}); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    if (window.has_value())
    {
        const Platform::WindowMetricsSnapshot metrics = windowMetrics(*window, frameId);
        const Platform::WindowInputSnapshot input{
            .window = window->window,
            .sourceMetricsRevision = frameId,
        };
        if (!builder.setPrimaryWindowSnapshot(metrics, input))
        {
            return Core::failure(Core::CoreErrorCode::Internal, "Test primary window snapshot was rejected");
        }
    }

    if (!builder.setGamepadSnapshots({}))
    {
        return Core::failure(Core::CoreErrorCode::Internal, "Test gamepad snapshot was rejected");
    }
    return builder.finishFrame();
}

class CountingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize currentBytes() const noexcept
    {
        return currentBytes_;
    }

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* allocation = upstream_->allocate(bytes, alignment);
        currentBytes_ += bytes;
        return allocation;
    }

    void do_deallocate(void* allocation, usize bytes, usize alignment) override
    {
        currentBytes_ -= bytes;
        upstream_->deallocate(allocation, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_ = std::pmr::new_delete_resource();
    usize currentBytes_ = 0;
};

class PrimaryWindowUIContextOwnerTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto poolResult = WindowPool::Create(2);
        ASSERT_TRUE(poolResult.has_value()) << (poolResult ? "" : poolResult.error().message);
        windows = std::make_unique<WindowPool>(std::move(*poolResult));

        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value()) << (windowResult ? "" : windowResult.error().message);
        window = *windowResult;

        auto builderResult = Platform::PlatformFrameBuilder::Create({
            .inputTransitionCapacity = 8,
            .inputTextByteCapacity = 256,
            .platformEventCapacity = 4,
        });
        ASSERT_TRUE(builderResult.has_value()) << (builderResult ? "" : builderResult.error().message);
        builder = std::make_unique<Platform::PlatformFrameBuilder>(std::move(*builderResult));
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<Platform::PlatformFrameBuilder> builder;
};

TEST_F(PrimaryWindowUIContextOwnerTest, FrameSelectionBeforeStartupBindingFails)
{
    PrimaryWindowUIContextOwner owner;
    auto frame = buildFrame(*builder, 1);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto selection = owner.selectForFrame(*frame);
    ASSERT_FALSE(selection.has_value());
    EXPECT_EQ(selection.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, InvalidStartupMetricsDoNotPublishAndValidMetricsCanRetry)
{
    PrimaryWindowUIContextOwner owner;
    Platform::WindowMetricsSnapshot invalidExtent = windowMetrics(WindowFrameSpec{.window = window});
    invalidExtent.logicalExtent.width = 0;

    auto failedExtentBinding = owner.bindForStartup(invalidExtent);
    ASSERT_FALSE(failedExtentBinding.has_value());
    EXPECT_EQ(failedExtentBinding.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    Platform::WindowMetricsSnapshot invalidScale = windowMetrics(WindowFrameSpec{.window = window});
    invalidScale.contentScale.x = 0.0F;

    auto failedScaleBinding = owner.bindForStartup(invalidScale);
    ASSERT_FALSE(failedScaleBinding.has_value());
    EXPECT_EQ(failedScaleBinding.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    auto validBinding = owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window}));
    ASSERT_TRUE(validBinding.has_value()) << (validBinding ? "" : validBinding.error().message);
    ASSERT_NE(*validBinding, nullptr);
}

TEST_F(PrimaryWindowUIContextOwnerTest, StartupBindingIsOneShotAfterPublication)
{
    PrimaryWindowUIContextOwner owner;
    ASSERT_TRUE(owner.bindForStartup(std::nullopt).has_value());

    auto repeatedBinding = owner.bindForStartup(std::nullopt);
    ASSERT_FALSE(repeatedBinding.has_value());
    EXPECT_EQ(repeatedBinding.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, ExplicitHeadlessStartupRejectsALaterPrimaryWindow)
{
    PrimaryWindowUIContextOwner owner;
    auto startup = owner.bindForStartup(std::nullopt);
    ASSERT_TRUE(startup.has_value()) << (startup ? "" : startup.error().message);
    EXPECT_EQ(*startup, nullptr);

    auto firstHeadless = buildFrame(*builder, 1);
    ASSERT_TRUE(firstHeadless.has_value()) << (firstHeadless ? "" : firstHeadless.error().message);
    auto firstSelection = owner.selectForFrame(*firstHeadless);
    ASSERT_TRUE(firstSelection.has_value()) << (firstSelection ? "" : firstSelection.error().message);
    EXPECT_EQ(*firstSelection, nullptr);

    auto secondHeadless = buildFrame(*builder, 2);
    ASSERT_TRUE(secondHeadless.has_value()) << (secondHeadless ? "" : secondHeadless.error().message);
    auto secondSelection = owner.selectForFrame(*secondHeadless);
    ASSERT_TRUE(secondSelection.has_value()) << (secondSelection ? "" : secondSelection.error().message);
    EXPECT_EQ(*secondSelection, nullptr);

    auto windowFrame = buildFrame(*builder, 3, WindowFrameSpec{.window = window});
    ASSERT_TRUE(windowFrame.has_value()) << (windowFrame ? "" : windowFrame.error().message);
    auto boundSelection = owner.selectForFrame(*windowFrame);
    ASSERT_FALSE(boundSelection.has_value());
    EXPECT_EQ(boundSelection.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, ReusesTheSameContextForTheBoundPrimaryWindow)
{
    PrimaryWindowUIContextOwner owner;
    auto startup = owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window}));
    ASSERT_TRUE(startup.has_value()) << (startup ? "" : startup.error().message);
    UI::UIContext* const firstContext = *startup;
    ASSERT_NE(firstContext, nullptr);

    auto firstFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstSelection = owner.selectForFrame(*firstFrame);
    ASSERT_TRUE(firstSelection.has_value()) << (firstSelection ? "" : firstSelection.error().message);
    EXPECT_EQ(*firstSelection, firstContext);

    auto secondFrame = buildFrame(*builder, 2, WindowFrameSpec{.window = window});
    ASSERT_TRUE(secondFrame.has_value()) << (secondFrame ? "" : secondFrame.error().message);
    auto secondSelection = owner.selectForFrame(*secondFrame);
    ASSERT_TRUE(secondSelection.has_value()) << (secondSelection ? "" : secondSelection.error().message);
    EXPECT_EQ(*secondSelection, firstContext);
}

TEST_F(PrimaryWindowUIContextOwnerTest, RejectsPrimaryWindowDisappearanceAfterBinding)
{
    PrimaryWindowUIContextOwner owner;
    ASSERT_TRUE(owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window})).has_value());
    auto windowFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(windowFrame.has_value()) << (windowFrame ? "" : windowFrame.error().message);
    ASSERT_TRUE(owner.selectForFrame(*windowFrame).has_value());

    auto headlessFrame = buildFrame(*builder, 2);
    ASSERT_TRUE(headlessFrame.has_value()) << (headlessFrame ? "" : headlessFrame.error().message);
    auto selection = owner.selectForFrame(*headlessFrame);
    ASSERT_FALSE(selection.has_value());
    EXPECT_EQ(selection.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, RejectsAReplacementGenerationAfterBinding)
{
    PrimaryWindowUIContextOwner owner;
    ASSERT_TRUE(owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window})).has_value());
    auto firstFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    ASSERT_TRUE(owner.selectForFrame(*firstFrame).has_value());

    ASSERT_EQ(windows->erase(window), Core::GenerationEraseResult::Erased);
    auto replacementResult = windows->tryEmplace(2);
    ASSERT_TRUE(replacementResult.has_value()) << (replacementResult ? "" : replacementResult.error().message);
    const Platform::WindowId replacement = *replacementResult;
    ASSERT_EQ(replacement.index(), window.index());
    ASSERT_NE(replacement.generation(), window.generation());

    auto replacementFrame = buildFrame(*builder, 2, WindowFrameSpec{.window = replacement});
    ASSERT_TRUE(replacementFrame.has_value()) << (replacementFrame ? "" : replacementFrame.error().message);
    auto selection = owner.selectForFrame(*replacementFrame);
    ASSERT_FALSE(selection.has_value());
    EXPECT_EQ(selection.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, MetricsScaleAndMinimizedChangesDoNotRebindTheContext)
{
    PrimaryWindowUIContextOwner owner;
    auto startup = owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window}));
    ASSERT_TRUE(startup.has_value()) << (startup ? "" : startup.error().message);
    UI::UIContext* const firstContext = *startup;
    ASSERT_NE(firstContext, nullptr);

    auto firstFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstSelection = owner.selectForFrame(*firstFrame);
    ASSERT_TRUE(firstSelection.has_value()) << (firstSelection ? "" : firstSelection.error().message);
    EXPECT_EQ(*firstSelection, firstContext);

    auto minimizedFrame = buildFrame(*builder, 2,
                                     WindowFrameSpec{
                                         .window = window,
                                         .logicalExtent = {640, 360},
                                         .framebufferExtent = {0, 0},
                                         .contentScale = {1.25F, 1.25F},
                                         .focused = false,
                                         .minimized = true,
                                         .visible = true,
                                     });
    ASSERT_TRUE(minimizedFrame.has_value()) << (minimizedFrame ? "" : minimizedFrame.error().message);
    auto minimizedSelection = owner.selectForFrame(*minimizedFrame);
    ASSERT_TRUE(minimizedSelection.has_value()) << (minimizedSelection ? "" : minimizedSelection.error().message);
    EXPECT_EQ(*minimizedSelection, firstContext);

    auto restoredFrame = buildFrame(*builder, 3,
                                    WindowFrameSpec{
                                        .window = window,
                                        .logicalExtent = {800, 450},
                                        .framebufferExtent = {1600, 900},
                                        .contentScale = {2.0F, 2.0F},
                                    });
    ASSERT_TRUE(restoredFrame.has_value()) << (restoredFrame ? "" : restoredFrame.error().message);
    auto restoredSelection = owner.selectForFrame(*restoredFrame);
    ASSERT_TRUE(restoredSelection.has_value()) << (restoredSelection ? "" : restoredSelection.error().message);
    EXPECT_EQ(*restoredSelection, firstContext);
}

TEST_F(PrimaryWindowUIContextOwnerTest, RejectsMetricsRevisionMovingBackwardFromTheStartupSeed)
{
    PrimaryWindowUIContextOwner owner;
    ASSERT_TRUE(owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window}, 2)).has_value());

    auto olderFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(olderFrame.has_value()) << (olderFrame ? "" : olderFrame.error().message);
    auto selection = owner.selectForFrame(*olderFrame);
    ASSERT_FALSE(selection.has_value());
    EXPECT_EQ(selection.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, ShutdownIsIdempotentAndSelectionAfterShutdownFails)
{
    PrimaryWindowUIContextOwner owner;
    ASSERT_TRUE(owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window})).has_value());
    auto frame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    ASSERT_TRUE(owner.selectForFrame(*frame).has_value());

    owner.shutdown();
    owner.shutdown();

    auto selection = owner.selectForFrame(*frame);
    ASSERT_FALSE(selection.has_value());
    EXPECT_EQ(selection.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, SelectRejectsCallsFromAnotherThread)
{
    PrimaryWindowUIContextOwner owner;
    ASSERT_TRUE(owner.bindForStartup(std::nullopt).has_value());
    auto frame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    std::optional<Core::ErrorCode> errorCode;
    std::thread worker([&] {
        auto selection = owner.selectForFrame(*frame);
        if (!selection)
        {
            errorCode = selection.error().code;
        }
    });
    worker.join();

    ASSERT_TRUE(errorCode.has_value());
    EXPECT_EQ(*errorCode, RuntimeErrorCode::WrongOwnerThread);
}

TEST_F(PrimaryWindowUIContextOwnerTest, AllocationFailureDoesNotPublishABindingAndCanRetry)
{
    // Inject OOM as a structured Result rather than throwing from a PMR resource:
    // MSVC Debug CRT can abort/WER on bad_alloc thrown mid-construction of UI
    // storage, which is not the owner contract under test. The owner must leave
    // AwaitingStartup on factory failure and allow a successful retry.
    CountingMemoryResource resource;
    bool failNextCreate = true;
    usize factoryCalls = 0;
    const UI::UIContextCapacityConfig capacities{
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 8,
    };
    PrimaryWindowUIContextFactory factory =
        [&](Platform::WindowId ownerWindow, const UI::UIContextCapacityConfig& requested,
            std::pmr::memory_resource& memoryResource) -> Core::Result<std::unique_ptr<UI::UIContext>> {
        ++factoryCalls;
        EXPECT_EQ(ownerWindow, window);
        EXPECT_EQ(requested.nodeCapacity, capacities.nodeCapacity);
        EXPECT_EQ(&memoryResource, static_cast<std::pmr::memory_resource*>(&resource));
        if (failNextCreate)
        {
            failNextCreate = false;
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "injected UIContext allocation failure");
        }
        return UI::UIContext::Create(ownerWindow, requested, memoryResource);
    };

    PrimaryWindowUIContextOwner owner(capacities, resource, std::move(factory));

    const Platform::WindowMetricsSnapshot metrics = windowMetrics(WindowFrameSpec{.window = window});
    auto failedBinding = owner.bindForStartup(metrics);
    ASSERT_FALSE(failedBinding.has_value());
    EXPECT_EQ(failedBinding.error().code, Core::CoreErrorCode::OutOfMemory);
    EXPECT_EQ(factoryCalls, 1U);
    EXPECT_EQ(resource.currentBytes(), 0U);

    auto retryBinding = owner.bindForStartup(metrics);
    ASSERT_TRUE(retryBinding.has_value()) << (retryBinding ? "" : retryBinding.error().message);
    ASSERT_NE(*retryBinding, nullptr);
    EXPECT_EQ((*retryBinding)->ownerWindow(), window);
    EXPECT_EQ(factoryCalls, 2U);
    EXPECT_GT(resource.currentBytes(), 0U);

    owner.shutdown();
    EXPECT_EQ(resource.currentBytes(), 0U);
}

TEST_F(PrimaryWindowUIContextOwnerTest, PublishesConfiguredCapacitiesThroughContextStatistics)
{
    const UI::UIContextCapacityConfig capacities{
        .nodeCapacity = 16,
        .rootCapacity = 3,
        .dirtyQueueCapacity = 11,
        .layoutSnapshotCapacity = 12,
        .hitSnapshotCapacity = 13,
        .routePathCapacity = 14,
        .routedPointerListenerCapacity = 24,
    };
    PrimaryWindowUIContextOwner owner(capacities);
    auto selection = owner.bindForStartup(windowMetrics(WindowFrameSpec{.window = window}));
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    ASSERT_NE(*selection, nullptr);
    const UI::UIContextStatistics statistics = (*selection)->statistics();
    EXPECT_EQ(statistics.nodeCapacity, capacities.nodeCapacity);
    EXPECT_EQ(statistics.rootCapacity, capacities.rootCapacity);
    EXPECT_EQ(statistics.dirtyQueueCapacity, capacities.dirtyQueueCapacity);
    EXPECT_EQ(statistics.layoutSnapshotCapacity, capacities.layoutSnapshotCapacity);
    EXPECT_EQ(statistics.hitSnapshotCapacity, capacities.hitSnapshotCapacity);
    EXPECT_EQ(statistics.routePathCapacity, capacities.routePathCapacity);
    EXPECT_EQ(statistics.routedPointerListenerCapacity, capacities.routedPointerListenerCapacity);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorHeadlessNoOpConsumesTheFrameAttempt)
{
    PrimaryWindowUILayoutCoordinator coordinator;
    ASSERT_TRUE(coordinator.commitForStartup(nullptr, std::nullopt).has_value());
    auto frame = buildFrame(*builder, 1);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    EXPECT_TRUE(coordinator.commitForFrame(nullptr, *frame).has_value());
    const Core::Status retry = coordinator.commitForFrame(nullptr, *frame);
    ASSERT_FALSE(retry.has_value());
    EXPECT_EQ(retry.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorRequiresAndConsumesOneStartupAttempt)
{
    PrimaryWindowUILayoutCoordinator coordinator;
    auto frame = buildFrame(*builder, 1);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    const Core::Status beforeStartup = coordinator.commitForFrame(nullptr, *frame);
    ASSERT_FALSE(beforeStartup.has_value());
    EXPECT_EQ(beforeStartup.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    ASSERT_TRUE(coordinator.commitForStartup(nullptr, std::nullopt).has_value());
    const Core::Status repeatedStartup = coordinator.commitForStartup(nullptr, std::nullopt);
    ASSERT_FALSE(repeatedStartup.has_value());
    EXPECT_EQ(repeatedStartup.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorStartupPublishesInitialRootSnapshot)
{
    auto contextResult =
        UI::UIContext::Create(window, UI::UIContextCapacityConfig{.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    auto context = std::move(*contextResult);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
    auto root = std::move(*rootResult);
    ASSERT_TRUE(context->rootBuilder().createButton(root.rootNodeId()).has_value());

    PrimaryWindowUILayoutCoordinator coordinator;
    ASSERT_TRUE(coordinator
                    .commitForStartup(context.get(), windowMetrics(WindowFrameSpec{
                                                         .window = window,
                                                         .logicalExtent = {320, 180},
                                                     }))
                    .has_value());

    EXPECT_EQ(context->committedStructure().size(), 2U);
    EXPECT_EQ(context->committedLayout().size(), 2U);
    EXPECT_EQ(context->committedHit().size(), 2U);
    EXPECT_EQ(context->statistics().layoutRevision, 1U);
    EXPECT_EQ(context->statistics().hitRevision, 1U);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorPublishesEmptyContextAndSkipsUnchangedFrames)
{
    auto contextResult =
        UI::UIContext::Create(window, UI::UIContextCapacityConfig{.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    auto context = std::move(*contextResult);
    PrimaryWindowUILayoutCoordinator coordinator;
    const Platform::WindowMetricsSnapshot startupMetrics =
        windowMetrics(WindowFrameSpec{.window = window, .logicalExtent = {320, 180}});
    ASSERT_TRUE(coordinator.commitForStartup(context.get(), startupMetrics).has_value());

    auto firstFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window, .logicalExtent = {320, 180}});
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    ASSERT_TRUE(coordinator.commitForFrame(context.get(), *firstFrame).has_value());
    const UI::UIContextStatistics firstStatistics = context->statistics();
    EXPECT_EQ(firstStatistics.layoutRevision, 1U);
    EXPECT_EQ(firstStatistics.hitRevision, 1U);
    EXPECT_EQ(firstStatistics.lastLayoutPassCount, 0U);

    auto unchangedFrame = buildFrame(*builder, 2, WindowFrameSpec{.window = window, .logicalExtent = {320, 180}});
    ASSERT_TRUE(unchangedFrame.has_value()) << (unchangedFrame ? "" : unchangedFrame.error().message);
    ASSERT_TRUE(coordinator.commitForFrame(context.get(), *unchangedFrame).has_value());
    const UI::UIContextStatistics unchangedStatistics = context->statistics();
    EXPECT_EQ(unchangedStatistics.layoutRevision, firstStatistics.layoutRevision);
    EXPECT_EQ(unchangedStatistics.hitRevision, firstStatistics.hitRevision);
    EXPECT_EQ(unchangedStatistics.lastLayoutPassCount, 0U);
    EXPECT_EQ(unchangedStatistics.lastHitRebuildCount, 0U);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorUsesLogicalExtentOnly)
{
    auto contextResult =
        UI::UIContext::Create(window, UI::UIContextCapacityConfig{.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    auto context = std::move(*contextResult);
    PrimaryWindowUILayoutCoordinator coordinator;
    const Platform::WindowMetricsSnapshot startupMetrics = windowMetrics(WindowFrameSpec{
        .window = window,
        .logicalExtent = {640, 360},
        .framebufferExtent = {1280, 720},
        .contentScale = {2.0F, 2.0F},
    });
    ASSERT_TRUE(coordinator.commitForStartup(context.get(), startupMetrics).has_value());

    auto firstFrame = buildFrame(*builder, 1,
                                 WindowFrameSpec{
                                     .window = window,
                                     .logicalExtent = {640, 360},
                                     .framebufferExtent = {1280, 720},
                                     .contentScale = {2.0F, 2.0F},
                                 });
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    ASSERT_TRUE(coordinator.commitForFrame(context.get(), *firstFrame).has_value());
    const u64 firstRevision = context->statistics().layoutRevision;

    auto minimizedFrame = buildFrame(*builder, 2,
                                     WindowFrameSpec{
                                         .window = window,
                                         .logicalExtent = {640, 360},
                                         .framebufferExtent = {0, 0},
                                         .contentScale = {1.25F, 1.5F},
                                         .focused = false,
                                         .minimized = true,
                                     });
    ASSERT_TRUE(minimizedFrame.has_value()) << (minimizedFrame ? "" : minimizedFrame.error().message);
    ASSERT_TRUE(coordinator.commitForFrame(context.get(), *minimizedFrame).has_value());
    EXPECT_EQ(context->statistics().layoutRevision, firstRevision);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);

    auto resizedFrame = buildFrame(*builder, 3,
                                   WindowFrameSpec{
                                       .window = window,
                                       .logicalExtent = {800, 450},
                                       .framebufferExtent = {1600, 900},
                                       .contentScale = {2.0F, 2.0F},
                                   });
    ASSERT_TRUE(resizedFrame.has_value()) << (resizedFrame ? "" : resizedFrame.error().message);
    ASSERT_TRUE(coordinator.commitForFrame(context.get(), *resizedFrame).has_value());
    EXPECT_EQ(context->statistics().layoutRevision, firstRevision + 1);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorRejectsFrameIdFallback)
{
    PrimaryWindowUILayoutCoordinator coordinator;
    ASSERT_TRUE(coordinator.commitForStartup(nullptr, std::nullopt).has_value());
    auto secondFrame = buildFrame(*builder, 2);
    ASSERT_TRUE(secondFrame.has_value()) << (secondFrame ? "" : secondFrame.error().message);
    ASSERT_TRUE(coordinator.commitForFrame(nullptr, *secondFrame).has_value());

    auto firstFrame = buildFrame(*builder, 1);
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    const Core::Status fallback = coordinator.commitForFrame(nullptr, *firstFrame);
    ASSERT_FALSE(fallback.has_value());
    EXPECT_EQ(fallback.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorRejectsMissingPairAndConsumesAttempt)
{
    auto contextResult =
        UI::UIContext::Create(window, UI::UIContextCapacityConfig{.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    auto context = std::move(*contextResult);
    PrimaryWindowUILayoutCoordinator coordinator;
    ASSERT_TRUE(
        coordinator.commitForStartup(context.get(), windowMetrics(WindowFrameSpec{.window = window})).has_value());

    auto windowFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(windowFrame.has_value()) << (windowFrame ? "" : windowFrame.error().message);
    const Core::Status missingContext = coordinator.commitForFrame(nullptr, *windowFrame);
    ASSERT_FALSE(missingContext.has_value());
    EXPECT_EQ(missingContext.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    const Core::Status retry = coordinator.commitForFrame(context.get(), *windowFrame);
    ASSERT_FALSE(retry.has_value());
    EXPECT_EQ(retry.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    auto headlessFrame = buildFrame(*builder, 2);
    ASSERT_TRUE(headlessFrame.has_value()) << (headlessFrame ? "" : headlessFrame.error().message);
    const Core::Status missingWindow = coordinator.commitForFrame(context.get(), *headlessFrame);
    ASSERT_FALSE(missingWindow.has_value());
    EXPECT_EQ(missingWindow.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorRejectsContextFromAnotherWindow)
{
    auto secondWindowResult = windows->tryEmplace(2);
    ASSERT_TRUE(secondWindowResult.has_value()) << (secondWindowResult ? "" : secondWindowResult.error().message);
    const Platform::WindowId secondWindow = *secondWindowResult;
    auto contextResult =
        UI::UIContext::Create(secondWindow, UI::UIContextCapacityConfig{.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    auto context = std::move(*contextResult);
    PrimaryWindowUILayoutCoordinator coordinator;
    ASSERT_TRUE(coordinator.commitForStartup(context.get(), windowMetrics(WindowFrameSpec{.window = secondWindow}))
                    .has_value());

    auto frame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    const Core::Status status = coordinator.commitForFrame(context.get(), *frame);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorRejectsCallsFromAnotherThread)
{
    PrimaryWindowUILayoutCoordinator coordinator;
    ASSERT_TRUE(coordinator.commitForStartup(nullptr, std::nullopt).has_value());
    auto frame = buildFrame(*builder, 1);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    std::optional<Core::ErrorCode> errorCode;

    std::thread worker([&] {
        const Core::Status status = coordinator.commitForFrame(nullptr, *frame);
        if (!status)
        {
            errorCode = status.error().code;
        }
    });
    worker.join();

    ASSERT_TRUE(errorCode.has_value());
    EXPECT_EQ(*errorCode, RuntimeErrorCode::WrongOwnerThread);
    EXPECT_TRUE(coordinator.commitForFrame(nullptr, *frame).has_value());
}

TEST_F(PrimaryWindowUIContextOwnerTest, LayoutCoordinatorCapacityFailureIsAtomicAndCannotRetryTheFrame)
{
    auto contextResult = UI::UIContext::Create(window, UI::UIContextCapacityConfig{
                                                           .nodeCapacity = 4,
                                                           .rootCapacity = 1,
                                                           .dirtyQueueCapacity = 4,
                                                           .layoutSnapshotCapacity = 2,
                                                       });
    ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
    auto context = std::move(*contextResult);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
    auto root = std::move(*rootResult);
    ASSERT_TRUE(context->rootBuilder().createPanel(root.rootNodeId()).has_value());
    PrimaryWindowUILayoutCoordinator coordinator;
    ASSERT_TRUE(coordinator
                    .commitForStartup(context.get(), windowMetrics(WindowFrameSpec{
                                                         .window = window,
                                                         .logicalExtent = {100, 100},
                                                     }))
                    .has_value());
    const UI::UICommittedStructureView oldStructure = context->committedStructure();
    const UI::UICommittedLayoutView oldLayout = context->committedLayout();
    const UI::UICommittedHitView oldHit = context->committedHit();
    const UI::UIContextStatistics oldStatistics = context->statistics();
    const u64 oldStructureRevision = oldStructure.revision();
    const u64 oldLayoutRevision = oldLayout.layoutRevision();
    const u64 oldHitRevision = oldHit.hitRevision();
    const u64 oldHitStructureRevision = oldHit.structureRevision();
    const u64 oldHitLayoutRevision = oldHit.layoutRevision();
    const u64 oldHitPaintOrderRevision = oldHit.paintOrderRevision();
    const usize oldStructureSize = oldStructure.size();
    const usize oldLayoutSize = oldLayout.size();
    const usize oldHitSize = oldHit.size();
    const usize oldHitTargetCount = oldStatistics.committedHitTargetCount;

    ASSERT_TRUE(context->rootBuilder().createPanel(root.rootNodeId()).has_value());
    auto frame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    const Core::Status failedCommit = coordinator.commitForFrame(context.get(), *frame);
    ASSERT_FALSE(failedCommit.has_value());
    EXPECT_EQ(failedCommit.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedStructure().revision(), oldStructureRevision);
    EXPECT_EQ(context->committedStructure().size(), oldStructureSize);
    EXPECT_EQ(context->committedLayout().layoutRevision(), oldLayoutRevision);
    EXPECT_EQ(context->committedLayout().size(), oldLayoutSize);
    const UI::UICommittedHitView hitAfterFailure = context->committedHit();
    EXPECT_EQ(hitAfterFailure.hitRevision(), oldHitRevision);
    EXPECT_EQ(hitAfterFailure.structureRevision(), oldHitStructureRevision);
    EXPECT_EQ(hitAfterFailure.layoutRevision(), oldHitLayoutRevision);
    EXPECT_EQ(hitAfterFailure.paintOrderRevision(), oldHitPaintOrderRevision);
    EXPECT_EQ(hitAfterFailure.size(), oldHitSize);
    EXPECT_EQ(context->statistics().committedHitTargetCount, oldHitTargetCount);
    EXPECT_TRUE(context->statistics().dirty);
    EXPECT_TRUE(context->statistics().layoutDirty);

    const Core::Status retry = coordinator.commitForFrame(context.get(), *frame);
    ASSERT_FALSE(retry.has_value());
    EXPECT_EQ(retry.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

} // namespace
} // namespace Tina::Tests
