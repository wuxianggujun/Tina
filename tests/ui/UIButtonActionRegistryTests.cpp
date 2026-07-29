#include <gtest/gtest.h>

#include "detail/UIButtonActionRegistry.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using ButtonActionRegistry = UI::Detail::UIButtonActionRegistry;
using ButtonActionRegistration = UI::Detail::UIButtonActionRegistration;

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return allocationCount_;
    }

    [[nodiscard]] usize deallocationCount() const noexcept
    {
        return deallocationCount_;
    }

    [[nodiscard]] usize currentBytes() const noexcept
    {
        return currentBytes_;
    }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocationCount_;
        currentBytes_ += bytes;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++deallocationCount_;
        currentBytes_ -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize allocationCount_ = 0;
    usize deallocationCount_ = 0;
    usize currentBytes_ = 0;
};

struct CallbackDestructionProbe final {
    bool* invocationActive = nullptr;
    bool* destroyedDuringInvocation = nullptr;
    usize* destructionCount = nullptr;
    bool ownsProbe = true;

    CallbackDestructionProbe(bool& active, bool& destroyed, usize& count) noexcept
        : invocationActive(&active), destroyedDuringInvocation(&destroyed), destructionCount(&count)
    {
    }

    CallbackDestructionProbe(const CallbackDestructionProbe&) = delete;
    CallbackDestructionProbe& operator=(const CallbackDestructionProbe&) = delete;

    CallbackDestructionProbe(CallbackDestructionProbe&& other) noexcept
        : invocationActive(other.invocationActive),
          destroyedDuringInvocation(other.destroyedDuringInvocation),
          destructionCount(other.destructionCount), ownsProbe(std::exchange(other.ownsProbe, false))
    {
    }

    CallbackDestructionProbe& operator=(CallbackDestructionProbe&&) = delete;

    ~CallbackDestructionProbe() noexcept
    {
        if (!ownsProbe)
        {
            return;
        }
        ++*destructionCount;
        if (*invocationActive)
        {
            *destroyedDuringInvocation = true;
        }
    }
};

static_assert(std::is_nothrow_move_constructible_v<CallbackDestructionProbe>);
static_assert(std::is_nothrow_destructible_v<CallbackDestructionProbe>);

class UIButtonActionRegistryTests : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows_ = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows_->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());

        UI::UIContextCapacityConfig capacities{
            .nodeCapacity = NodeCapacity,
            .rootCapacity = 1,
            .buttonActionCapacity = ActionCapacity,
        };
        capacities.applyDefaultProductChrome = false;
        auto contextResult = UI::UIContext::Create(*windowResult, capacities);
        ASSERT_TRUE(contextResult.has_value())
            << (contextResult ? "" : contextResult.error().message);
        context_ = std::move(*contextResult);

        auto rootResult = context_->rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
        root_ = std::move(*rootResult);

        auto firstButtonResult = context_->rootBuilder().createButton(root_.rootNodeId());
        ASSERT_TRUE(firstButtonResult.has_value())
            << (firstButtonResult ? "" : firstButtonResult.error().message);
        firstButton_ = *firstButtonResult;

        auto secondButtonResult = context_->rootBuilder().createButton(root_.rootNodeId());
        ASSERT_TRUE(secondButtonResult.has_value())
            << (secondButtonResult ? "" : secondButtonResult.error().message);
        secondButton_ = *secondButtonResult;
    }

    [[nodiscard]] UI::UIButtonActionEvent eventFor(UI::UINodeId button, u64 sequence = 1) const noexcept
    {
        return UI::UIButtonActionEvent{
            .buttonNode = button,
            .source = UI::UIButtonActivationSource::PrimaryPointer,
            .platformFrame = Platform::PlatformFrameId{sequence},
            .sourceSequence = sequence,
        };
    }

    static void commit(ButtonActionRegistry& registry, const ButtonActionRegistration& registration,
                       bool deferReclaim = false)
    {
        Core::Status status = registry.commit(registration, deferReclaim);
        ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
    }

    static constexpr usize NodeCapacity = 4;
    static constexpr usize ActionCapacity = 2;
    std::unique_ptr<WindowPool> windows_;
    std::unique_ptr<UI::UIContext> context_;
    UI::UIRootOwner root_;
    UI::UINodeId firstButton_{};
    UI::UINodeId secondButton_{};
};

TEST_F(UIButtonActionRegistryTests, StageCommitCaptureAndRegistrationBoundary)
{
    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    usize invocationCount = 0;
    auto registration = registry.stage(
        firstButton_,
        UI::UIButtonActionCallback{[&invocationCount](const UI::UIButtonActionEvent&) noexcept {
            ++invocationCount;
        }},
        false);
    ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);

    EXPECT_TRUE(registry.canCommit(*registration));
    EXPECT_EQ(registry.activeCount(), 0U);
    EXPECT_FALSE(registry.capture(firstButton_, 0).hasValue());
    commit(registry, *registration);

    EXPECT_EQ(registry.activeCount(), 1U);
    EXPECT_EQ(registry.highWater(), 1U);
    EXPECT_EQ(registry.registrationSerial(), 1U);
    EXPECT_FALSE(registry.capture(firstButton_, 0).hasValue());
    const UI::Detail::UIButtonActionInvocation invocation = registry.capture(firstButton_, 1);
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstButton_), 1, false);
    EXPECT_EQ(invocationCount, 1U);
}

TEST_F(UIButtonActionRegistryTests, ReplacementPreservesCapturedActionForCurrentRoute)
{
    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    usize oldInvocationCount = 0;
    auto original = registry.stage(
        firstButton_,
        UI::UIButtonActionCallback{[&oldInvocationCount](const UI::UIButtonActionEvent&) noexcept {
            ++oldInvocationCount;
        }},
        false);
    ASSERT_TRUE(original.has_value()) << (original ? "" : original.error().message);
    commit(registry, *original);
    const UI::Detail::UIButtonActionInvocation captured = registry.capture(firstButton_, 1);
    ASSERT_TRUE(captured.hasValue());

    usize newInvocationCount = 0;
    auto replacement = registry.stage(
        firstButton_,
        UI::UIButtonActionCallback{[&newInvocationCount](const UI::UIButtonActionEvent&) noexcept {
            ++newInvocationCount;
        }},
        true);
    ASSERT_TRUE(replacement.has_value()) << (replacement ? "" : replacement.error().message);
    commit(registry, *replacement, true);
    EXPECT_EQ(registry.activeCount(), 1U);

    registry.invoke(captured, eventFor(firstButton_), 7, true);
    EXPECT_EQ(oldInvocationCount, 1U);
    EXPECT_EQ(newInvocationCount, 0U);
    EXPECT_FALSE(registry.capture(firstButton_, 1).hasValue());
    const UI::Detail::UIButtonActionInvocation nextRoute = registry.capture(firstButton_, 2);
    ASSERT_TRUE(nextRoute.hasValue());
    registry.reclaim(false);
    registry.invoke(nextRoute, eventFor(firstButton_, 2), 8, false);
    EXPECT_EQ(newInvocationCount, 1U);
}

TEST_F(UIButtonActionRegistryTests, ClearRouteBarrierBlocksCapturedAction)
{
    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    usize invocationCount = 0;
    auto registration = registry.stage(
        firstButton_,
        UI::UIButtonActionCallback{[&invocationCount](const UI::UIButtonActionEvent&) noexcept {
            ++invocationCount;
        }},
        false);
    ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
    commit(registry, *registration);
    const UI::Detail::UIButtonActionInvocation captured = registry.capture(firstButton_, 1);
    ASSERT_TRUE(captured.hasValue());

    registry.clear(firstButton_, 9, true);
    registry.invoke(captured, eventFor(firstButton_), 9, true);
    EXPECT_EQ(invocationCount, 0U);
    EXPECT_EQ(registry.activeCount(), 0U);
    EXPECT_FALSE(registry.capture(firstButton_, registry.registrationSerial()).hasValue());
    registry.reclaim(false);
}

TEST_F(UIButtonActionRegistryTests, ReplacementRejectsReusedPreviousSlotGeneration)
{
    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    auto original = registry.stage(
        firstButton_, UI::UIButtonActionCallback{[](const UI::UIButtonActionEvent&) noexcept {}}, false);
    ASSERT_TRUE(original.has_value()) << (original ? "" : original.error().message);
    commit(registry, *original);

    auto staleReplacement = registry.stage(
        firstButton_, UI::UIButtonActionCallback{[](const UI::UIButtonActionEvent&) noexcept {}}, false);
    ASSERT_TRUE(staleReplacement.has_value())
        << (staleReplacement ? "" : staleReplacement.error().message);
    registry.clear(firstButton_, 0, false);

    usize interleavedInvocationCount = 0;
    auto interleaved = registry.stage(
        firstButton_,
        UI::UIButtonActionCallback{
            [&interleavedInvocationCount](const UI::UIButtonActionEvent&) noexcept {
                ++interleavedInvocationCount;
            }},
        false);
    ASSERT_TRUE(interleaved.has_value()) << (interleaved ? "" : interleaved.error().message);
    EXPECT_EQ(interleaved->actionIndex, staleReplacement->previousActionIndex);
    EXPECT_NE(interleaved->generation, staleReplacement->previousActionGeneration);
    commit(registry, *interleaved);

    EXPECT_FALSE(registry.canCommit(*staleReplacement));
    registry.rollback(*staleReplacement, false);
    EXPECT_EQ(registry.activeCount(), 1U);
    const UI::Detail::UIButtonActionInvocation invocation =
        registry.capture(firstButton_, registry.registrationSerial());
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstButton_), 1, false);
    EXPECT_EQ(interleavedInvocationCount, 1U);
}

TEST_F(UIButtonActionRegistryTests, RollbackAfterCommitDoesNotClearCommittedAction)
{
    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    usize invocationCount = 0;
    auto registration = registry.stage(
        firstButton_,
        UI::UIButtonActionCallback{[&invocationCount](const UI::UIButtonActionEvent&) noexcept {
            ++invocationCount;
        }},
        false);
    ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
    commit(registry, *registration);

    registry.rollback(*registration, false);

    EXPECT_EQ(registry.activeCount(), 1U);
    const UI::Detail::UIButtonActionInvocation invocation =
        registry.capture(firstButton_, registry.registrationSerial());
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstButton_), 1, false);
    EXPECT_EQ(invocationCount, 1U);
}

TEST_F(UIButtonActionRegistryTests, StaleNodeIdCannotClearReusedNodeAction)
{
    auto updaterResult = context_->treeUpdater(root_);
    ASSERT_TRUE(updaterResult.has_value()) << (updaterResult ? "" : updaterResult.error().message);
    UI::UITreeUpdater updater = std::move(*updaterResult);
    ASSERT_TRUE(updater.destroy(firstButton_).has_value());
    auto replacementButtonResult = context_->rootBuilder().createButton(root_.rootNodeId());
    ASSERT_TRUE(replacementButtonResult.has_value())
        << (replacementButtonResult ? "" : replacementButtonResult.error().message);
    const UI::UINodeId replacementButton = *replacementButtonResult;
    ASSERT_EQ(replacementButton.index(), firstButton_.index());
    ASSERT_NE(replacementButton.generation(), firstButton_.generation());

    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    usize invocationCount = 0;
    auto registration = registry.stage(
        replacementButton,
        UI::UIButtonActionCallback{[&invocationCount](const UI::UIButtonActionEvent&) noexcept {
            ++invocationCount;
        }},
        false);
    ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
    commit(registry, *registration);

    registry.clear(firstButton_, 4, false);

    EXPECT_EQ(registry.activeCount(), 1U);
    const UI::Detail::UIButtonActionInvocation invocation =
        registry.capture(replacementButton, registry.registrationSerial());
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(replacementButton), 4, false);
    EXPECT_EQ(invocationCount, 1U);
}

TEST_F(UIButtonActionRegistryTests, FullCapacityReplacementUsesTransactionSlotUntilReclaim)
{
    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    for (const UI::UINodeId button : {firstButton_, secondButton_})
    {
        auto registration = registry.stage(
            button, UI::UIButtonActionCallback{[](const UI::UIButtonActionEvent&) noexcept {}}, false);
        ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
        commit(registry, *registration);
    }
    ASSERT_EQ(registry.activeCount(), ActionCapacity);
    ASSERT_EQ(registry.highWater(), ActionCapacity);

    auto replacement = registry.stage(
        firstButton_, UI::UIButtonActionCallback{[](const UI::UIButtonActionEvent&) noexcept {}}, false);
    ASSERT_TRUE(replacement.has_value()) << (replacement ? "" : replacement.error().message);
    commit(registry, *replacement, true);

    auto blocked = registry.stage(
        firstButton_, UI::UIButtonActionCallback{[](const UI::UIButtonActionEvent&) noexcept {}}, true);
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, UI::UIErrorCode::CapacityExceeded);

    registry.reclaim(false);
    auto recovered = registry.stage(
        firstButton_, UI::UIButtonActionCallback{[](const UI::UIButtonActionEvent&) noexcept {}}, false);
    ASSERT_TRUE(recovered.has_value()) << (recovered ? "" : recovered.error().message);
    registry.rollback(*recovered, false);
    EXPECT_EQ(registry.activeCount(), ActionCapacity);
    EXPECT_EQ(registry.highWater(), ActionCapacity);
}

TEST_F(UIButtonActionRegistryTests, SelfClearDefersCallbackDestructionUntilAfterInvocation)
{
    ButtonActionRegistry registry(NodeCapacity, ActionCapacity, *std::pmr::get_default_resource());
    struct State final {
        ButtonActionRegistry* registry = nullptr;
        UI::UINodeId button{};
        usize invocationCount = 0;
        usize destructionCount = 0;
        bool invocationActive = false;
        bool destroyedDuringInvocation = false;
        bool sawOperationInProgress = false;
    } state{.registry = &registry, .button = firstButton_};

    auto registration = registry.stage(
        firstButton_,
        UI::UIButtonActionCallback{
            [statePointer = &state,
             probe = CallbackDestructionProbe{state.invocationActive,
                                              state.destroyedDuringInvocation,
                                              state.destructionCount}](
                const UI::UIButtonActionEvent&) mutable noexcept {
                statePointer->invocationActive = true;
                ++statePointer->invocationCount;
                statePointer->sawOperationInProgress = statePointer->registry->operationInProgress();
                statePointer->registry->clear(statePointer->button, 12, false);
                statePointer->invocationActive = false;
            }},
        false);
    ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
    commit(registry, *registration);
    const UI::Detail::UIButtonActionInvocation invocation = registry.capture(firstButton_, 1);
    ASSERT_TRUE(invocation.hasValue());

    registry.invoke(invocation, eventFor(firstButton_), 12, false);

    EXPECT_EQ(state.invocationCount, 1U);
    EXPECT_EQ(state.destructionCount, 1U);
    EXPECT_TRUE(state.sawOperationInProgress);
    EXPECT_FALSE(state.destroyedDuringInvocation);
    EXPECT_FALSE(registry.operationInProgress());
    EXPECT_EQ(registry.activeCount(), 0U);
}

TEST_F(UIButtonActionRegistryTests, RepeatedSteadyStateOperationsDoNotAllocateAndReleasePmrStorage)
{
    ObservingMemoryResource resource;
    {
        ButtonActionRegistry registry(NodeCapacity, ActionCapacity, resource);
        const usize constructionAllocationCount = resource.allocationCount();
        ASSERT_GT(constructionAllocationCount, 0U);
        usize invocationCount = 0;

        for (u64 iteration = 0; iteration < 300; ++iteration)
        {
            auto registration = registry.stage(
                firstButton_,
                UI::UIButtonActionCallback{
                    [&invocationCount](const UI::UIButtonActionEvent&) noexcept {
                        ++invocationCount;
                    }},
                false);
            ASSERT_TRUE(registration.has_value())
                << "iteration=" << iteration << ' '
                << (registration ? "" : registration.error().message);
            commit(registry, *registration);
            const UI::Detail::UIButtonActionInvocation invocation =
                registry.capture(firstButton_, registry.registrationSerial());
            ASSERT_TRUE(invocation.hasValue()) << "iteration=" << iteration;
            registry.invoke(invocation, eventFor(firstButton_, iteration + 1), iteration + 1, false);
            registry.clear(firstButton_, 0, false);
            ASSERT_EQ(registry.activeCount(), 0U) << "iteration=" << iteration;
        }

        EXPECT_EQ(invocationCount, 300U);
        EXPECT_EQ(resource.allocationCount(), constructionAllocationCount);
        EXPECT_GT(resource.currentBytes(), 0U);
    }

    EXPECT_EQ(resource.currentBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

} // namespace
} // namespace Tina::Tests
