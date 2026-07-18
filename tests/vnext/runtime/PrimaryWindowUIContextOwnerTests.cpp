#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/RuntimeErrors.hpp>

#include "../../../src/runtime/ui/PrimaryWindowUIContextOwner.hpp"

#include <algorithm>
#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using PrimaryWindowUIContextOwner = Runtime::Detail::PrimaryWindowUIContextOwner;
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

[[nodiscard]] Core::Result<Platform::PlatformFrameView> buildFrame(Platform::PlatformFrameBuilder& builder, u64 frameId,
                                                                   std::optional<WindowFrameSpec> window = std::nullopt)
{
    if (auto status = builder.beginFrame({frameId}); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    if (window.has_value())
    {
        const Platform::WindowMetricsSnapshot metrics{
            .window = window->window,
            .logicalExtent = window->logicalExtent,
            .framebufferExtent = window->framebufferExtent,
            .contentScale = window->contentScale,
            .revision = frameId,
            .focused = window->focused,
            .minimized = window->minimized,
            .visible = window->visible,
        };
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

class FailFirstAllocationMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize currentBytes() const noexcept
    {
        return currentBytes_;
    }

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (failNextAllocation_)
        {
            failNextAllocation_ = false;
            throw std::bad_alloc{};
        }
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
    bool failNextAllocation_ = true;
};

class PrimaryWindowUIContextOwnerTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto poolResult = WindowPool::Create(1);
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

TEST_F(PrimaryWindowUIContextOwnerTest, HeadlessFramesRemainUnboundUntilTheFirstPrimaryWindow)
{
    PrimaryWindowUIContextOwner owner;

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
    ASSERT_TRUE(boundSelection.has_value()) << (boundSelection ? "" : boundSelection.error().message);
    ASSERT_NE(*boundSelection, nullptr);
    EXPECT_EQ((*boundSelection)->ownerWindow(), window);
}

TEST_F(PrimaryWindowUIContextOwnerTest, ReusesTheSameContextForTheBoundPrimaryWindow)
{
    PrimaryWindowUIContextOwner owner;
    auto firstFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstSelection = owner.selectForFrame(*firstFrame);
    ASSERT_TRUE(firstSelection.has_value()) << (firstSelection ? "" : firstSelection.error().message);
    ASSERT_NE(*firstSelection, nullptr);
    UI::UIContext* const firstContext = *firstSelection;

    auto secondFrame = buildFrame(*builder, 2, WindowFrameSpec{.window = window});
    ASSERT_TRUE(secondFrame.has_value()) << (secondFrame ? "" : secondFrame.error().message);
    auto secondSelection = owner.selectForFrame(*secondFrame);
    ASSERT_TRUE(secondSelection.has_value()) << (secondSelection ? "" : secondSelection.error().message);
    EXPECT_EQ(*secondSelection, firstContext);
}

TEST_F(PrimaryWindowUIContextOwnerTest, RejectsPrimaryWindowDisappearanceAfterBinding)
{
    PrimaryWindowUIContextOwner owner;
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
    auto firstFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstSelection = owner.selectForFrame(*firstFrame);
    ASSERT_TRUE(firstSelection.has_value()) << (firstSelection ? "" : firstSelection.error().message);
    ASSERT_NE(*firstSelection, nullptr);
    UI::UIContext* const firstContext = *firstSelection;

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

TEST_F(PrimaryWindowUIContextOwnerTest, ShutdownIsIdempotentAndSelectionAfterShutdownFails)
{
    PrimaryWindowUIContextOwner owner;
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
    FailFirstAllocationMemoryResource resource;
    PrimaryWindowUIContextOwner owner(
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .routePathCapacity = 8,
            .routedPointerListenerCapacity = 8,
        },
        resource);

    auto firstFrame = buildFrame(*builder, 1, WindowFrameSpec{.window = window});
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto failedSelection = owner.selectForFrame(*firstFrame);
    ASSERT_FALSE(failedSelection.has_value());
    EXPECT_EQ(failedSelection.error().code, Core::CoreErrorCode::OutOfMemory);

    auto retryFrame = buildFrame(*builder, 2, WindowFrameSpec{.window = window});
    ASSERT_TRUE(retryFrame.has_value()) << (retryFrame ? "" : retryFrame.error().message);
    auto retrySelection = owner.selectForFrame(*retryFrame);
    ASSERT_TRUE(retrySelection.has_value()) << (retrySelection ? "" : retrySelection.error().message);
    ASSERT_NE(*retrySelection, nullptr);
    EXPECT_EQ((*retrySelection)->ownerWindow(), window);
    EXPECT_GT(resource.currentBytes(), 0U);

    owner.shutdown();
    EXPECT_EQ(resource.currentBytes(), 0U);
}

} // namespace
} // namespace Tina::Tests
