#include <gtest/gtest.h>

#include "detail/UISliderChangeCallbackRegistry.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using SliderChangeCallbackRegistry = UI::Detail::UISliderChangeCallbackRegistry;

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return allocationCount_; }
    [[nodiscard]] usize deallocationCount() const noexcept { return deallocationCount_; }
    [[nodiscard]] usize currentBytes() const noexcept { return currentBytes_; }

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

    CallbackDestructionProbe(bool& active, bool& destroyedDuringInvocationValue,
                             usize& destructionCountValue) noexcept
        : invocationActive(&active), destroyedDuringInvocation(&destroyedDuringInvocationValue),
          destructionCount(&destructionCountValue)
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
        if (destructionCount != nullptr)
        {
            ++*destructionCount;
        }
        if (invocationActive != nullptr && destroyedDuringInvocation != nullptr && *invocationActive)
        {
            *destroyedDuringInvocation = true;
        }
    }
};

static_assert(std::is_nothrow_move_constructible_v<CallbackDestructionProbe>);
static_assert(std::is_nothrow_destructible_v<CallbackDestructionProbe>);

struct ReentrantRegistrationState final {
    SliderChangeCallbackRegistry* registry = nullptr;
    UI::UINodeId slider{};
    usize destructionCount = 0;
    usize invocationCount = 0;
    bool registrationSucceeded = false;
};

struct ReentrantRegistrationProbe final {
    ReentrantRegistrationState* state = nullptr;
    bool ownsProbe = true;

    explicit ReentrantRegistrationProbe(ReentrantRegistrationState& registrationState) noexcept
        : state(&registrationState)
    {
    }

    ReentrantRegistrationProbe(const ReentrantRegistrationProbe&) = delete;
    ReentrantRegistrationProbe& operator=(const ReentrantRegistrationProbe&) = delete;

    ReentrantRegistrationProbe(ReentrantRegistrationProbe&& other) noexcept
        : state(other.state), ownsProbe(std::exchange(other.ownsProbe, false))
    {
    }

    ReentrantRegistrationProbe& operator=(ReentrantRegistrationProbe&&) = delete;

    ~ReentrantRegistrationProbe() noexcept
    {
        if (!ownsProbe)
        {
            return;
        }
        ++state->destructionCount;
        auto registration = state->registry->stage(
            state->slider,
            UI::UISliderChangeCallback{[registrationState = state](
                                           const UI::UISliderChangeEvent&) noexcept {
                ++registrationState->invocationCount;
            }},
            false);
        if (!registration)
        {
            return;
        }
        state->registry->commit(*registration, false);
        state->registrationSucceeded = state->registry->capture(state->slider).hasValue();
    }
};

static_assert(std::is_nothrow_move_constructible_v<ReentrantRegistrationProbe>);
static_assert(std::is_nothrow_destructible_v<ReentrantRegistrationProbe>);

class UISliderChangeCallbackRegistryTests : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows_ = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows_->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());

        UI::UIContextCapacityConfig capacities{
            .nodeCapacity = RegistryCapacity,
            .rootCapacity = 1,
        };
        capacities.applyDefaultProductChrome = false;
        auto contextResult = UI::UIContext::Create(*windowResult, capacities);
        ASSERT_TRUE(contextResult.has_value())
            << (contextResult ? "" : contextResult.error().message);
        context_ = std::move(*contextResult);

        auto rootResult = context_->rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
        root_ = std::move(*rootResult);

        auto firstSliderResult = context_->rootBuilder().createSlider(root_.rootNodeId());
        ASSERT_TRUE(firstSliderResult.has_value())
            << (firstSliderResult ? "" : firstSliderResult.error().message);
        firstSlider_ = *firstSliderResult;

        auto secondSliderResult = context_->rootBuilder().createSlider(root_.rootNodeId());
        ASSERT_TRUE(secondSliderResult.has_value())
            << (secondSliderResult ? "" : secondSliderResult.error().message);
        secondSlider_ = *secondSliderResult;
    }

    [[nodiscard]] UI::UISliderChangeEvent eventFor(UI::UINodeId slider, float value = 0.5F,
                                                    u64 sourceSequence = 1) const noexcept
    {
        return UI::UISliderChangeEvent{
            .sliderNode = slider,
            .value = value,
            .platformFrame = Platform::PlatformFrameId{sourceSequence},
            .sourceSequence = sourceSequence,
        };
    }

    static constexpr usize RegistryCapacity = 4;
    std::unique_ptr<WindowPool> windows_;
    std::unique_ptr<UI::UIContext> context_;
    UI::UIRootOwner root_;
    UI::UINodeId firstSlider_{};
    UI::UINodeId secondSlider_{};
};

TEST_F(UISliderChangeCallbackRegistryTests, StagedCallbackIsInvisibleUntilCommit)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize invocationCount = 0;
    auto registrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{[&invocationCount](const UI::UISliderChangeEvent&) noexcept {
            ++invocationCount;
        }},
        false);
    ASSERT_TRUE(registrationResult.has_value())
        << (registrationResult ? "" : registrationResult.error().message);
    const UI::Detail::UISliderChangeCallbackRegistration registration = *registrationResult;

    EXPECT_TRUE(registry.canCommit(registration));
    EXPECT_EQ(registry.activeCount(), 0U);
    EXPECT_FALSE(registry.capture(firstSlider_).hasValue());

    registry.commit(registration, false);
    EXPECT_EQ(registry.activeCount(), 1U);
    const UI::Detail::UISliderChangeCallbackInvocation invocation = registry.capture(firstSlider_);
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstSlider_), false);
    EXPECT_EQ(invocationCount, 1U);
}

TEST_F(UISliderChangeCallbackRegistryTests, CommitInvokeClearAndSlotReusePreserveGenerationSafety)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize firstInvocationCount = 0;
    auto firstRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{[&firstInvocationCount](const UI::UISliderChangeEvent&) noexcept {
            ++firstInvocationCount;
        }},
        false);
    ASSERT_TRUE(firstRegistrationResult.has_value())
        << (firstRegistrationResult ? "" : firstRegistrationResult.error().message);
    const UI::Detail::UISliderChangeCallbackRegistration firstRegistration = *firstRegistrationResult;
    registry.commit(firstRegistration, false);
    const UI::Detail::UISliderChangeCallbackInvocation staleInvocation = registry.capture(firstSlider_);
    ASSERT_TRUE(staleInvocation.hasValue());
    registry.invoke(staleInvocation, eventFor(firstSlider_), false);
    EXPECT_EQ(firstInvocationCount, 1U);

    registry.clear(firstSlider_, false);
    EXPECT_EQ(registry.activeCount(), 0U);
    EXPECT_FALSE(registry.capture(firstSlider_).hasValue());

    usize replacementInvocationCount = 0;
    auto replacementRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{
            [&replacementInvocationCount](const UI::UISliderChangeEvent&) noexcept {
                ++replacementInvocationCount;
            }},
        false);
    ASSERT_TRUE(replacementRegistrationResult.has_value())
        << (replacementRegistrationResult ? "" : replacementRegistrationResult.error().message);
    const UI::Detail::UISliderChangeCallbackRegistration replacementRegistration =
        *replacementRegistrationResult;
    EXPECT_EQ(replacementRegistration.callbackIndex, firstRegistration.callbackIndex);
    EXPECT_NE(replacementRegistration.generation, firstRegistration.generation);
    registry.commit(replacementRegistration, false);

    registry.invoke(staleInvocation, eventFor(firstSlider_), false);
    EXPECT_EQ(firstInvocationCount, 1U);
    EXPECT_EQ(replacementInvocationCount, 0U);

    const UI::Detail::UISliderChangeCallbackInvocation replacementInvocation =
        registry.capture(firstSlider_);
    ASSERT_TRUE(replacementInvocation.hasValue());
    registry.invoke(replacementInvocation, eventFor(firstSlider_, 0.75F, 2), false);
    EXPECT_EQ(replacementInvocationCount, 1U);
}

TEST_F(UISliderChangeCallbackRegistryTests, ReplacementKeepsOneActiveCallback)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize oldInvocationCount = 0;
    auto oldRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{[&oldInvocationCount](const UI::UISliderChangeEvent&) noexcept {
            ++oldInvocationCount;
        }},
        false);
    ASSERT_TRUE(oldRegistrationResult.has_value())
        << (oldRegistrationResult ? "" : oldRegistrationResult.error().message);
    registry.commit(*oldRegistrationResult, false);
    const UI::Detail::UISliderChangeCallbackInvocation oldInvocation = registry.capture(firstSlider_);
    ASSERT_TRUE(oldInvocation.hasValue());

    usize newInvocationCount = 0;
    auto newRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{[&newInvocationCount](const UI::UISliderChangeEvent&) noexcept {
            ++newInvocationCount;
        }},
        false);
    ASSERT_TRUE(newRegistrationResult.has_value())
        << (newRegistrationResult ? "" : newRegistrationResult.error().message);
    EXPECT_TRUE(newRegistrationResult->replacing);
    EXPECT_EQ(registry.activeCount(), 1U);
    registry.invoke(oldInvocation, eventFor(firstSlider_), false);
    EXPECT_EQ(oldInvocationCount, 1U);

    registry.commit(*newRegistrationResult, false);
    EXPECT_EQ(registry.activeCount(), 1U);
    registry.invoke(oldInvocation, eventFor(firstSlider_), false);
    EXPECT_EQ(oldInvocationCount, 1U);
    const UI::Detail::UISliderChangeCallbackInvocation newInvocation = registry.capture(firstSlider_);
    ASSERT_TRUE(newInvocation.hasValue());
    registry.invoke(newInvocation, eventFor(firstSlider_), false);
    EXPECT_EQ(newInvocationCount, 1U);
}

TEST_F(UISliderChangeCallbackRegistryTests, ReplacementRejectsReusedPreviousSlotGeneration)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize originalInvocationCount = 0;
    auto originalRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{[&originalInvocationCount](const UI::UISliderChangeEvent&) noexcept {
            ++originalInvocationCount;
        }},
        false);
    ASSERT_TRUE(originalRegistrationResult.has_value())
        << (originalRegistrationResult ? "" : originalRegistrationResult.error().message);
    registry.commit(*originalRegistrationResult, false);

    usize staleReplacementInvocationCount = 0;
    auto staleReplacementResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{
            [&staleReplacementInvocationCount](const UI::UISliderChangeEvent&) noexcept {
                ++staleReplacementInvocationCount;
            }},
        false);
    ASSERT_TRUE(staleReplacementResult.has_value())
        << (staleReplacementResult ? "" : staleReplacementResult.error().message);
    ASSERT_TRUE(staleReplacementResult->replacing);

    registry.clear(firstSlider_, false);
    usize interleavedInvocationCount = 0;
    auto interleavedRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{
            [&interleavedInvocationCount](const UI::UISliderChangeEvent&) noexcept {
                ++interleavedInvocationCount;
            }},
        false);
    ASSERT_TRUE(interleavedRegistrationResult.has_value())
        << (interleavedRegistrationResult ? "" : interleavedRegistrationResult.error().message);
    EXPECT_EQ(interleavedRegistrationResult->callbackIndex,
              staleReplacementResult->previousCallbackIndex);
    EXPECT_NE(interleavedRegistrationResult->generation,
              staleReplacementResult->previousCallbackGeneration);
    registry.commit(*interleavedRegistrationResult, false);

    EXPECT_FALSE(registry.canCommit(*staleReplacementResult));
    registry.rollback(*staleReplacementResult, false);
    EXPECT_EQ(registry.activeCount(), 1U);
    EXPECT_EQ(originalInvocationCount, 0U);
    EXPECT_EQ(staleReplacementInvocationCount, 0U);

    const UI::Detail::UISliderChangeCallbackInvocation invocation = registry.capture(firstSlider_);
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstSlider_), false);
    EXPECT_EQ(interleavedInvocationCount, 1U);
}

TEST_F(UISliderChangeCallbackRegistryTests, RollbackPreservesCommittedCallback)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize committedInvocationCount = 0;
    auto committedRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{
            [&committedInvocationCount](const UI::UISliderChangeEvent&) noexcept {
                ++committedInvocationCount;
            }},
        false);
    ASSERT_TRUE(committedRegistrationResult.has_value())
        << (committedRegistrationResult ? "" : committedRegistrationResult.error().message);
    registry.commit(*committedRegistrationResult, false);

    usize rolledBackInvocationCount = 0;
    auto rolledBackRegistrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{
            [&rolledBackInvocationCount](const UI::UISliderChangeEvent&) noexcept {
                ++rolledBackInvocationCount;
            }},
        false);
    ASSERT_TRUE(rolledBackRegistrationResult.has_value())
        << (rolledBackRegistrationResult ? "" : rolledBackRegistrationResult.error().message);
    ASSERT_TRUE(rolledBackRegistrationResult->replacing);
    registry.rollback(*rolledBackRegistrationResult, false);

    EXPECT_EQ(registry.activeCount(), 1U);
    const UI::Detail::UISliderChangeCallbackInvocation invocation = registry.capture(firstSlider_);
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstSlider_), false);
    EXPECT_EQ(committedInvocationCount, 1U);
    EXPECT_EQ(rolledBackInvocationCount, 0U);
}

TEST_F(UISliderChangeCallbackRegistryTests, RollbackAfterCommitDoesNotClearCommittedCallback)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize invocationCount = 0;
    auto registrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{[&invocationCount](const UI::UISliderChangeEvent&) noexcept {
            ++invocationCount;
        }},
        false);
    ASSERT_TRUE(registrationResult.has_value())
        << (registrationResult ? "" : registrationResult.error().message);

    registry.commit(*registrationResult, false);
    registry.rollback(*registrationResult, false);

    EXPECT_EQ(registry.activeCount(), 1U);
    const UI::Detail::UISliderChangeCallbackInvocation invocation = registry.capture(firstSlider_);
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstSlider_), false);
    EXPECT_EQ(invocationCount, 1U);
}

TEST_F(UISliderChangeCallbackRegistryTests, StaleNodeIdCannotClearReusedNodeCallback)
{
    auto updaterResult = context_->treeUpdater(root_);
    ASSERT_TRUE(updaterResult.has_value())
        << (updaterResult ? "" : updaterResult.error().message);
    UI::UITreeUpdater updater = std::move(*updaterResult);
    ASSERT_TRUE(updater.destroy(firstSlider_).has_value());

    auto replacementSliderResult = context_->rootBuilder().createSlider(root_.rootNodeId());
    ASSERT_TRUE(replacementSliderResult.has_value())
        << (replacementSliderResult ? "" : replacementSliderResult.error().message);
    const UI::UINodeId replacementSlider = *replacementSliderResult;
    ASSERT_EQ(replacementSlider.index(), firstSlider_.index());
    ASSERT_NE(replacementSlider.generation(), firstSlider_.generation());

    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize invocationCount = 0;
    auto registrationResult = registry.stage(
        replacementSlider,
        UI::UISliderChangeCallback{[&invocationCount](const UI::UISliderChangeEvent&) noexcept {
            ++invocationCount;
        }},
        false);
    ASSERT_TRUE(registrationResult.has_value())
        << (registrationResult ? "" : registrationResult.error().message);
    registry.commit(*registrationResult, false);

    registry.clear(firstSlider_, false);

    EXPECT_EQ(registry.activeCount(), 1U);
    const UI::Detail::UISliderChangeCallbackInvocation invocation = registry.capture(replacementSlider);
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(replacementSlider), false);
    EXPECT_EQ(invocationCount, 1U);
}

TEST_F(UISliderChangeCallbackRegistryTests, SelfClearDefersCallbackDestructionUntilAfterInvocation)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    struct State final {
        SliderChangeCallbackRegistry* registry = nullptr;
        UI::UINodeId slider{};
        usize invocationCount = 0;
        usize destructionCount = 0;
        bool invocationActive = false;
        bool destroyedDuringInvocation = false;
        bool sawOperationInProgress = false;
    } state{.registry = &registry, .slider = firstSlider_};

    auto registrationResult = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{
            [statePointer = &state,
             probe = CallbackDestructionProbe{state.invocationActive,
                                              state.destroyedDuringInvocation,
                                              state.destructionCount}](
                const UI::UISliderChangeEvent&) mutable noexcept {
                statePointer->invocationActive = true;
                ++statePointer->invocationCount;
                statePointer->sawOperationInProgress = statePointer->registry->operationInProgress();
                statePointer->registry->clear(statePointer->slider, false);
                statePointer->invocationActive = false;
            }},
        false);
    ASSERT_TRUE(registrationResult.has_value())
        << (registrationResult ? "" : registrationResult.error().message);
    registry.commit(*registrationResult, false);
    const UI::Detail::UISliderChangeCallbackInvocation invocation = registry.capture(firstSlider_);
    ASSERT_TRUE(invocation.hasValue());

    registry.invoke(invocation, eventFor(firstSlider_), false);

    EXPECT_EQ(state.invocationCount, 1U);
    EXPECT_EQ(state.destructionCount, 1U);
    EXPECT_TRUE(state.sawOperationInProgress);
    EXPECT_FALSE(state.destroyedDuringInvocation);
    EXPECT_FALSE(registry.operationInProgress());
    EXPECT_EQ(registry.activeCount(), 0U);
    EXPECT_FALSE(registry.capture(firstSlider_).hasValue());
}

TEST_F(UISliderChangeCallbackRegistryTests, DeferredReclaimRequiresExplicitNonDeferredPass)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    usize destructionCount = 0;
    bool invocationActive = false;
    bool destroyedDuringInvocation = false;
    auto registrationResult = registry.stage(
        secondSlider_,
        UI::UISliderChangeCallback{
            [probe = CallbackDestructionProbe{invocationActive, destroyedDuringInvocation,
                                              destructionCount}](
                const UI::UISliderChangeEvent&) mutable noexcept {}},
        false);
    ASSERT_TRUE(registrationResult.has_value())
        << (registrationResult ? "" : registrationResult.error().message);
    registry.commit(*registrationResult, false);

    registry.clear(secondSlider_, true);
    EXPECT_EQ(registry.activeCount(), 0U);
    EXPECT_FALSE(registry.capture(secondSlider_).hasValue());
    EXPECT_EQ(destructionCount, 0U);
    registry.reclaim(true);
    EXPECT_EQ(destructionCount, 0U);

    registry.reclaim(false);
    EXPECT_EQ(destructionCount, 1U);
    EXPECT_FALSE(destroyedDuringInvocation);
}

TEST_F(UISliderChangeCallbackRegistryTests, FullCapacityReplacementUsesTransactionSlotUntilReclaim)
{
    SliderChangeCallbackRegistry registry(RegistryCapacity, *std::pmr::get_default_resource());
    const UI::UINodeId nodes[] = {
        root_.rootNodeId(),
        firstSlider_,
        secondSlider_,
    };
    for (const UI::UINodeId node : nodes)
    {
        auto registration = registry.stage(
            node, UI::UISliderChangeCallback{[](const UI::UISliderChangeEvent&) noexcept {}}, false);
        ASSERT_TRUE(registration.has_value())
            << (registration ? "" : registration.error().message);
        registry.commit(*registration, false);
    }

    auto thirdSliderResult = context_->rootBuilder().createSlider(root_.rootNodeId());
    ASSERT_TRUE(thirdSliderResult.has_value())
        << (thirdSliderResult ? "" : thirdSliderResult.error().message);
    auto fourthRegistration = registry.stage(
        *thirdSliderResult,
        UI::UISliderChangeCallback{[](const UI::UISliderChangeEvent&) noexcept {}}, false);
    ASSERT_TRUE(fourthRegistration.has_value())
        << (fourthRegistration ? "" : fourthRegistration.error().message);
    registry.commit(*fourthRegistration, false);
    ASSERT_EQ(registry.activeCount(), RegistryCapacity);

    auto replacement = registry.stage(
        firstSlider_, UI::UISliderChangeCallback{[](const UI::UISliderChangeEvent&) noexcept {}}, false);
    ASSERT_TRUE(replacement.has_value()) << (replacement ? "" : replacement.error().message);
    registry.commit(*replacement, true);
    EXPECT_EQ(registry.activeCount(), RegistryCapacity);

    auto blockedReplacement = registry.stage(
        firstSlider_, UI::UISliderChangeCallback{[](const UI::UISliderChangeEvent&) noexcept {}}, true);
    ASSERT_FALSE(blockedReplacement.has_value());
    EXPECT_EQ(blockedReplacement.error().code, UI::UIErrorCode::CapacityExceeded);

    registry.reclaim(false);
    auto recoveredReplacement = registry.stage(
        firstSlider_, UI::UISliderChangeCallback{[](const UI::UISliderChangeEvent&) noexcept {}}, false);
    ASSERT_TRUE(recoveredReplacement.has_value())
        << (recoveredReplacement ? "" : recoveredReplacement.error().message);
    registry.rollback(*recoveredReplacement, false);
    EXPECT_EQ(registry.activeCount(), RegistryCapacity);
}

TEST_F(UISliderChangeCallbackRegistryTests, CallbackDestructionCanReenterRegistrationSafely)
{
    ObservingMemoryResource resource;
    SliderChangeCallbackRegistry registry(RegistryCapacity, resource);
    const usize constructionAllocationCount = resource.allocationCount();
    ReentrantRegistrationState state{
        .registry = &registry,
        .slider = firstSlider_,
    };

    auto registration = registry.stage(
        firstSlider_,
        UI::UISliderChangeCallback{
            [probe = ReentrantRegistrationProbe{state}](
                const UI::UISliderChangeEvent&) mutable noexcept {}},
        false);
    ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
    registry.commit(*registration, false);

    registry.clear(firstSlider_, false);

    EXPECT_EQ(state.destructionCount, 1U);
    EXPECT_TRUE(state.registrationSucceeded);
    EXPECT_EQ(registry.activeCount(), 1U);
    EXPECT_EQ(resource.allocationCount(), constructionAllocationCount);
    const UI::Detail::UISliderChangeCallbackInvocation invocation = registry.capture(firstSlider_);
    ASSERT_TRUE(invocation.hasValue());
    registry.invoke(invocation, eventFor(firstSlider_), false);
    EXPECT_EQ(state.invocationCount, 1U);
}

TEST_F(UISliderChangeCallbackRegistryTests, RepeatedSteadyStateOperationsDoNotAllocateAndReleasePmrStorage)
{
    ObservingMemoryResource resource;
    {
        SliderChangeCallbackRegistry registry(RegistryCapacity, resource);
        const usize constructionAllocationCount = resource.allocationCount();
        ASSERT_GT(constructionAllocationCount, 0U);
        usize invocationCount = 0;

        for (u64 iteration = 0; iteration < 300; ++iteration)
        {
            auto registrationResult = registry.stage(
                firstSlider_,
                UI::UISliderChangeCallback{
                    [&invocationCount](const UI::UISliderChangeEvent&) noexcept {
                        ++invocationCount;
                    }},
                false);
            ASSERT_TRUE(registrationResult.has_value())
                << "iteration=" << iteration << ' '
                << (registrationResult ? "" : registrationResult.error().message);
            registry.commit(*registrationResult, false);
            const UI::Detail::UISliderChangeCallbackInvocation invocation =
                registry.capture(firstSlider_);
            ASSERT_TRUE(invocation.hasValue()) << "iteration=" << iteration;
            registry.invoke(invocation, eventFor(firstSlider_, 0.5F, iteration + 1), false);
            registry.clear(firstSlider_, false);
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
