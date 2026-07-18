#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>

#include "../../../src/runtime/ui/PrimaryWindowUIDisplayCoordinator.hpp"

#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <utility>

namespace Tina::Tests {
namespace {

using PrimaryWindowUIDisplayCoordinator = Runtime::Detail::PrimaryWindowUIDisplayCoordinator;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

struct FrameSpec final {
    std::optional<Platform::WindowId> window{};
    Platform::LogicalExtent logicalExtent{200, 100};
    Platform::FramebufferExtent framebufferExtent{400, 200};
    u64 metricsRevision = 0;
    bool minimized = false;
};

[[nodiscard]] Core::Result<Platform::PlatformFrameView>
buildFrame(Platform::PlatformFrameBuilder& builder, u64 frameId, FrameSpec spec = {})
{
    if (auto status = builder.beginFrame(Platform::PlatformFrameId{frameId}); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    if (spec.window.has_value())
    {
        const u64 metricsRevision = spec.metricsRevision == 0 ? frameId : spec.metricsRevision;
        const Platform::WindowMetricsSnapshot metrics{
            .window = *spec.window,
            .logicalExtent = spec.logicalExtent,
            .framebufferExtent = spec.framebufferExtent,
            .contentScale = {2.0F, 2.0F},
            .revision = metricsRevision,
            .focused = true,
            .minimized = spec.minimized,
            .visible = true,
        };
        const Platform::WindowInputSnapshot input{
            .window = *spec.window,
            .sourceMetricsRevision = metricsRevision,
        };
        if (!builder.setPrimaryWindowSnapshot(metrics, input))
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "Test primary-window snapshot was rejected");
        }
    }

    if (!builder.setGamepadSnapshots({}))
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Test gamepad snapshot was rejected");
    }
    return builder.finishFrame();
}

[[nodiscard]] Render::RenderSurfaceState surfaceFor(u64 metricsRevision,
                                                    Render::RenderSurfaceAvailability availability =
                                                        Render::RenderSurfaceAvailability::Active,
                                                    Platform::FramebufferExtent framebufferExtent = {400, 200})
{
    return Render::RenderSurfaceState{
        .surface = {.owner = 1, .index = 0, .generation = 1},
        .framebufferExtent = {framebufferExtent.width, framebufferExtent.height},
        .contentScale = {2.0F, 2.0F},
        .sourceMetricsRevision = metricsRevision,
        .surfaceRevision = metricsRevision,
        .availability = availability,
    };
}

[[nodiscard]] UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha = 255)
{
    return UI::UIBoxPaint{
        .solidFill = UI::UISolidFill{.color = {.red = red, .green = green, .blue = blue, .alpha = alpha}},
    };
}

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    usize allocationCount = 0;
    usize deallocationCount = 0;
    bool rejectAllocations = false;

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (rejectAllocations)
        {
            throw std::bad_alloc{};
        }
        ++allocationCount;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++deallocationCount;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

class PrimaryWindowUIDisplayCoordinatorTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(2);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;

        auto frameBuilderResult = Platform::PlatformFrameBuilder::Create({
            .inputTransitionCapacity = 8,
            .inputTextByteCapacity = 64,
            .platformEventCapacity = 4,
        });
        ASSERT_TRUE(frameBuilderResult.has_value());
        frameBuilder = std::make_unique<Platform::PlatformFrameBuilder>(std::move(*frameBuilderResult));

        auto contextResult = UI::UIContext::Create(window, {
                                                              .nodeCapacity = 8,
                                                              .rootCapacity = 1,
                                                          });
        ASSERT_TRUE(contextResult.has_value());
        context = std::move(*contextResult);

        auto rootResult = context->rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value());
        root = std::move(*rootResult);
        auto updaterResult = context->treeUpdater(root);
        ASSERT_TRUE(updaterResult.has_value());
        auto& updater = *updaterResult;
        auto panelResult = updater.createPanel(root.rootNodeId());
        ASSERT_TRUE(panelResult.has_value());
        panel = *panelResult;

        UI::UILayoutStyle rootStyle{};
        rootStyle.size.width = UI::UILayoutLength::Px(200.0F);
        rootStyle.size.height = UI::UILayoutLength::Px(100.0F);
        ASSERT_TRUE(updater.setLayoutStyle(root.rootNodeId(), rootStyle).has_value());
        UI::UILayoutStyle panelStyle{};
        panelStyle.size.width = UI::UILayoutLength::Px(100.0F);
        panelStyle.size.height = UI::UILayoutLength::Px(50.0F);
        ASSERT_TRUE(updater.setLayoutStyle(panel, panelStyle).has_value());
        ASSERT_TRUE(updater.setBoxPaint(panel, solidFill(20, 40, 80)).has_value());
        ASSERT_TRUE(context->commitLayout({.width = 200.0F, .height = 100.0F}).has_value());
    }

    [[nodiscard]] PrimaryWindowUIDisplayCoordinator createCoordinator(
        Render::UIDisplayListCapacity capacity = {.commandCount = 8, .clipCount = 8, .batchCount = 8},
        std::pmr::memory_resource& storage = *std::pmr::get_default_resource())
    {
        auto result = PrimaryWindowUIDisplayCoordinator::Create(capacity, storage);
        EXPECT_TRUE(result.has_value());
        return std::move(*result);
    }

    [[nodiscard]] Core::Status addSecondPaintedPanel()
    {
        auto updaterResult = context->treeUpdater(root);
        if (!updaterResult)
        {
            return Core::failure(std::move(updaterResult.error()));
        }
        auto panelResult = updaterResult->createPanel(root.rootNodeId());
        if (!panelResult)
        {
            return Core::failure(std::move(panelResult.error()));
        }
        UI::UILayoutStyle style{};
        style.size.width = UI::UILayoutLength::Px(80.0F);
        style.size.height = UI::UILayoutLength::Px(40.0F);
        if (auto status = updaterResult->setLayoutStyle(*panelResult, style); !status)
        {
            return status;
        }
        if (auto status = updaterResult->setBoxPaint(*panelResult, solidFill(100, 50, 25)); !status)
        {
            return status;
        }
        return context->commitLayout({.width = 200.0F, .height = 100.0F});
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<Platform::PlatformFrameBuilder> frameBuilder;
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root{};
    UI::UINodeId panel{};
};

TEST_F(PrimaryWindowUIDisplayCoordinatorTest, PublishesMappedBorrowAndRejectsASecondBuildForTheSameFrame)
{
    auto coordinator = createCoordinator();
    auto frame = buildFrame(*frameBuilder, 1, {.window = window});
    ASSERT_TRUE(frame.has_value());

    auto build = coordinator.buildForFrame(context.get(), *frame, std::nullopt);
    ASSERT_TRUE(build.has_value()) << (build ? "" : build.error().message);
    ASSERT_EQ(build->displayList.commands().size(), 1U);
    EXPECT_EQ(build->displayList.commands().front().bounds,
              (Render::UIPixelRect{.x = 0, .y = 0, .width = 200, .height = 100}));
    EXPECT_EQ(build->conversionStatistics.sourcePaintEntryCount, 1U);
    EXPECT_EQ(build->displayList.commands().data(), coordinator.publishedView().commands().data());

    auto duplicate = coordinator.buildForFrame(context.get(), *frame, std::nullopt);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_TRUE(coordinator.publishedView().empty());
}

TEST_F(PrimaryWindowUIDisplayCoordinatorTest, CapacityFailureRollsBackWithoutOldOrTruncatedPublication)
{
    auto coordinator = createCoordinator({.commandCount = 1, .clipCount = 1, .batchCount = 1});
    auto firstFrame = buildFrame(*frameBuilder, 1, {.window = window});
    ASSERT_TRUE(firstFrame.has_value());
    auto firstBuild = coordinator.buildForFrame(context.get(), *firstFrame, std::nullopt);
    ASSERT_TRUE(firstBuild.has_value());
    ASSERT_EQ(firstBuild->displayList.commands().size(), 1U);

    ASSERT_TRUE(addSecondPaintedPanel().has_value());
    auto secondFrame = buildFrame(*frameBuilder, 2, {.window = window});
    ASSERT_TRUE(secondFrame.has_value());
    auto failedBuild = coordinator.buildForFrame(context.get(), *secondFrame, std::nullopt);

    ASSERT_FALSE(failedBuild.has_value());
    EXPECT_EQ(failedBuild.error().code, Render::RenderErrorCode::DisplayListCapacityExceeded);
    EXPECT_TRUE(coordinator.publishedView().empty());
    const auto statistics = coordinator.builderStatistics();
    EXPECT_EQ(statistics.committedBuildCount, 1U);
    EXPECT_EQ(statistics.rolledBackBuildCount, 1U);
    EXPECT_EQ(statistics.capacityFailureCount, 1U);
}

TEST_F(PrimaryWindowUIDisplayCoordinatorTest, HeadlessZeroFramebufferAndSuspendedSurfacePublishEmptyFrames)
{
    auto coordinator = createCoordinator();

    auto headless = buildFrame(*frameBuilder, 1);
    ASSERT_TRUE(headless.has_value());
    auto headlessBuild = coordinator.buildForFrame(nullptr, *headless, std::nullopt);
    ASSERT_TRUE(headlessBuild.has_value());
    EXPECT_TRUE(headlessBuild->displayList.empty());
    EXPECT_TRUE(headlessBuild->displayList.clips().empty());
    EXPECT_TRUE(headlessBuild->displayList.batches().empty());
    EXPECT_EQ(headlessBuild->conversionStatistics.sourcePaintEntryCount, 0U);

    auto zeroFramebuffer = buildFrame(*frameBuilder, 2, {
                                                            .window = window,
                                                            .framebufferExtent = {0, 0},
                                                            .minimized = true,
                                                        });
    ASSERT_TRUE(zeroFramebuffer.has_value());
    auto zeroBuild = coordinator.buildForFrame(context.get(), *zeroFramebuffer, std::nullopt);
    ASSERT_TRUE(zeroBuild.has_value());
    EXPECT_TRUE(zeroBuild->displayList.empty());

    auto activeExtentFrame = buildFrame(*frameBuilder, 3, {.window = window});
    ASSERT_TRUE(activeExtentFrame.has_value());
    const auto suspended = surfaceFor(3, Render::RenderSurfaceAvailability::Suspended);
    auto suspendedBuild = coordinator.buildForFrame(context.get(), *activeExtentFrame, suspended);
    ASSERT_TRUE(suspendedBuild.has_value());
    EXPECT_TRUE(suspendedBuild->displayList.empty());

    auto restoredFrame = buildFrame(*frameBuilder, 4, {.window = window});
    ASSERT_TRUE(restoredFrame.has_value());
    auto restoredBuild = coordinator.buildForFrame(context.get(), *restoredFrame, surfaceFor(4));
    ASSERT_TRUE(restoredBuild.has_value());
    EXPECT_EQ(restoredBuild->displayList.commands().size(), 1U);
}

TEST_F(PrimaryWindowUIDisplayCoordinatorTest, MetricsRevisionMismatchInvalidatesThePreviousPublication)
{
    auto coordinator = createCoordinator();
    auto firstFrame = buildFrame(*frameBuilder, 1, {.window = window});
    ASSERT_TRUE(firstFrame.has_value());
    auto firstBuild = coordinator.buildForFrame(context.get(), *firstFrame, surfaceFor(1));
    ASSERT_TRUE(firstBuild.has_value());
    ASSERT_FALSE(firstBuild->displayList.empty());

    auto secondFrame = buildFrame(*frameBuilder, 2, {.window = window});
    ASSERT_TRUE(secondFrame.has_value());
    auto mismatch = coordinator.buildForFrame(context.get(), *secondFrame, surfaceFor(1));

    ASSERT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
    EXPECT_TRUE(coordinator.publishedView().empty());
}

TEST_F(PrimaryWindowUIDisplayCoordinatorTest, ReusesItsFixedStorageForThreeHundredFrames)
{
    TrackingMemoryResource storage;
    {
        auto coordinator = createCoordinator({.commandCount = 8, .clipCount = 8, .batchCount = 8}, storage);
        ASSERT_EQ(storage.allocationCount, 3U);
        storage.rejectAllocations = true;

        const Render::UIDrawCommand* fixedCommands = nullptr;
        for (u64 frameId = 1; frameId <= 300; ++frameId)
        {
            auto frame = buildFrame(*frameBuilder, frameId, {.window = window});
            ASSERT_TRUE(frame.has_value());
            auto build = coordinator.buildForFrame(context.get(), *frame, std::nullopt);
            ASSERT_TRUE(build.has_value()) << (build ? "" : build.error().message);
            ASSERT_EQ(build->displayList.commands().size(), 1U);
            if (fixedCommands == nullptr)
            {
                fixedCommands = build->displayList.commands().data();
            }
            EXPECT_EQ(build->displayList.commands().data(), fixedCommands);
        }

        EXPECT_EQ(storage.allocationCount, 3U);
        EXPECT_EQ(coordinator.builderStatistics().committedBuildCount, 300U);
    }
    EXPECT_EQ(storage.deallocationCount, 3U);
}

} // namespace
} // namespace Tina::Tests
