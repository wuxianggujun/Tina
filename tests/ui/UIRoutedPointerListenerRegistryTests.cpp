#include <gtest/gtest.h>

#include "detail/UIRoutedPointerListenerRegistry.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using ListenerRegistry = UI::Detail::UIRoutedPointerListenerRegistry;
using ListenerRegistration = UI::Detail::UIRoutedPointerListenerRegistration;
using ListenerStatePublisher = UI::Detail::UIRoutedPointerListenerStatePublisher;

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

struct TokenMirror final {
    struct Slot final {
        u32 generation = 0;
        bool active = false;
    };

    static void publish(void* context, u32 slot, u32 generation, bool active) noexcept
    {
        auto& mirror = *static_cast<TokenMirror*>(context);
        if (slot >= mirror.slots.size())
        {
            mirror.outOfRange = true;
            return;
        }
        Slot& state = mirror.slots[slot];
        if (active || state.generation == generation)
        {
            state = Slot{.generation = generation, .active = active};
        }
        ++mirror.publicationCount;
    }

    [[nodiscard]] ListenerStatePublisher publisher() noexcept
    {
        return ListenerStatePublisher{.context = this, .publish = &TokenMirror::publish};
    }

    std::array<Slot, 8> slots{};
    usize publicationCount = 0;
    bool outOfRange = false;
};

struct DestructionProbe final {
    usize* destructionCount = nullptr;
    bool ownsProbe = true;

    explicit DestructionProbe(usize& count) noexcept : destructionCount(&count)
    {
    }

    DestructionProbe(const DestructionProbe&) = delete;
    DestructionProbe& operator=(const DestructionProbe&) = delete;

    DestructionProbe(DestructionProbe&& other) noexcept
        : destructionCount(other.destructionCount), ownsProbe(std::exchange(other.ownsProbe, false))
    {
    }

    DestructionProbe& operator=(DestructionProbe&&) = delete;

    ~DestructionProbe() noexcept
    {
        if (ownsProbe)
        {
            ++*destructionCount;
        }
    }
};

struct RegistryReentryState final {
    ListenerRegistry* registry = nullptr;
    UI::UINodeId node{};
    usize destructionCount = 0;
    bool registrationSucceeded = false;
};

struct RegistryReentryProbe final {
    RegistryReentryState* state = nullptr;
    bool ownsProbe = true;

    explicit RegistryReentryProbe(RegistryReentryState& value) noexcept : state(&value)
    {
    }

    RegistryReentryProbe(const RegistryReentryProbe&) = delete;
    RegistryReentryProbe& operator=(const RegistryReentryProbe&) = delete;

    RegistryReentryProbe(RegistryReentryProbe&& other) noexcept
        : state(other.state), ownsProbe(std::exchange(other.ownsProbe, false))
    {
    }

    RegistryReentryProbe& operator=(RegistryReentryProbe&&) = delete;

    ~RegistryReentryProbe() noexcept
    {
        if (!ownsProbe)
        {
            return;
        }
        ++state->destructionCount;
        auto registration = state->registry->stage(
            UI::UIRoutedPointerListenerDesc{
                .node = state->node,
                .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                .phases = UI::UIEventPhaseMask::Target,
            },
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}}, false);
        if (!registration)
        {
            return;
        }
        state->registrationSucceeded = state->registry->commit(*registration, {}, false).has_value();
    }
};

struct ContextReentryState final {
    UI::UIContext* context = nullptr;
    UI::UINodeId node{};
    usize destructionCount = 0;
    Core::ErrorCode error = UI::UIErrorCode::InvalidRoutedPointerListener;
    bool registrationSucceeded = false;
};

struct ContextReentryProbe final {
    ContextReentryState* state = nullptr;
    bool ownsProbe = true;

    explicit ContextReentryProbe(ContextReentryState& value) noexcept : state(&value)
    {
    }

    ContextReentryProbe(const ContextReentryProbe&) = delete;
    ContextReentryProbe& operator=(const ContextReentryProbe&) = delete;

    ContextReentryProbe(ContextReentryProbe&& other) noexcept
        : state(other.state), ownsProbe(std::exchange(other.ownsProbe, false))
    {
    }

    ContextReentryProbe& operator=(ContextReentryProbe&&) = delete;

    ~ContextReentryProbe() noexcept
    {
        if (!ownsProbe)
        {
            return;
        }
        ++state->destructionCount;
        auto registration = state->context->input().addRoutedPointerListener(
            UI::UIRoutedPointerListenerDesc{
                .node = state->node,
                .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                .phases = UI::UIEventPhaseMask::Target,
            },
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
        state->registrationSucceeded = registration.has_value();
        if (!registration)
        {
            state->error = registration.error().code;
        }
    }
};

static_assert(std::is_nothrow_move_constructible_v<DestructionProbe>);
static_assert(std::is_nothrow_move_constructible_v<RegistryReentryProbe>);
static_assert(std::is_nothrow_move_constructible_v<ContextReentryProbe>);

class UIRoutedPointerListenerRegistryTests : public testing::Test {
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
            .routedPointerListenerCapacity = ListenerCapacity,
        };
        capacities.applyDefaultProductChrome = false;
        auto contextResult = UI::UIContext::Create(*windowResult, capacities);
        ASSERT_TRUE(contextResult.has_value())
            << (contextResult ? "" : contextResult.error().message);
        context_ = std::move(*contextResult);

        auto rootResult = context_->authoring().rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
        root_ = std::move(*rootResult);

        auto firstNodeResult = context_->authoring().rootBuilder().createElement(
            root_.rootNodeId(), UI::makePanelElement());
        ASSERT_TRUE(firstNodeResult.has_value())
            << (firstNodeResult ? "" : firstNodeResult.error().message);
        firstNode_ = *firstNodeResult;

        auto secondNodeResult = context_->authoring().rootBuilder().createElement(
            root_.rootNodeId(), UI::makePanelElement());
        ASSERT_TRUE(secondNodeResult.has_value())
            << (secondNodeResult ? "" : secondNodeResult.error().message);
        secondNode_ = *secondNodeResult;
    }

    [[nodiscard]] static UI::UIRoutedPointerListenerDesc descriptor(
        UI::UINodeId node, UI::UIRoutedPointerEventKind kind = UI::UIRoutedPointerEventKind::ButtonDown,
        UI::UIEventPhaseMask phases = UI::UIEventPhaseMask::Target) noexcept
    {
        return UI::UIRoutedPointerListenerDesc{.node = node, .kind = kind, .phases = phases};
    }

    [[nodiscard]] static UI::UIRoutedPointerEvent event(
        UI::UIRoutedPointerEventKind kind = UI::UIRoutedPointerEventKind::ButtonDown) noexcept
    {
        return UI::Detail::UIRoutedPointerEventAccess::Create(
            UI::UIPointerInputEvent{.sourceSequence = 1, .kind = kind});
    }

    static void commit(ListenerRegistry& registry, const ListenerRegistration& registration,
                       ListenerStatePublisher publisher = {}, bool deferReclaim = false)
    {
        Core::Status status = registry.commit(registration, publisher, deferReclaim);
        ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
    }

    static constexpr usize NodeCapacity = 6;
    static constexpr usize ListenerCapacity = 4;
    std::unique_ptr<WindowPool> windows_;
    std::unique_ptr<UI::UIContext> context_;
    UI::UIRootOwner root_;
    UI::UINodeId firstNode_{};
    UI::UINodeId secondNode_{};
};

TEST_F(UIRoutedPointerListenerRegistryTests, StageCommitRollbackAndSerialBoundaryPreserveFifo)
{
    ListenerRegistry registry(NodeCapacity, ListenerCapacity, *std::pmr::get_default_resource());
    std::array<u32, 4> order{};
    usize orderSize = 0;
    auto first = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[&order, &orderSize](UI::UIRoutedPointerEvent&) noexcept {
            order[orderSize++] = 1;
        }},
        false);
    ASSERT_TRUE(first.has_value()) << (first ? "" : first.error().message);

    auto beforeCommit = event();
    EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                UI::UIEventPhaseMask::Target, 1, beforeCommit),
              0U);
    commit(registry, *first);

    auto rolledBack = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[&orderSize](UI::UIRoutedPointerEvent&) noexcept {
            orderSize = 99;
        }},
        false);
    ASSERT_TRUE(rolledBack.has_value()) << (rolledBack ? "" : rolledBack.error().message);
    registry.rollback(*rolledBack, false);

    auto second = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[&order, &orderSize](UI::UIRoutedPointerEvent&) noexcept {
            order[orderSize++] = 2;
        }},
        false);
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    commit(registry, *second);

    auto firstBoundary = event();
    EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                UI::UIEventPhaseMask::Target, 1, firstBoundary),
              1U);
    ASSERT_EQ(orderSize, 1U);
    EXPECT_EQ(order[0], 1U);

    orderSize = 0;
    auto secondBoundary = event();
    EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                UI::UIEventPhaseMask::Target, 2, secondBoundary),
              2U);
    EXPECT_EQ(orderSize, 2U);
    EXPECT_EQ(order[0], 1U);
    EXPECT_EQ(order[1], 2U);
    EXPECT_EQ(registry.activeCount(), 2U);
    EXPECT_EQ(registry.highWater(), 2U);
}

TEST_F(UIRoutedPointerListenerRegistryTests, KindPhaseAndImmediatePropagationFilterDispatch)
{
    ListenerRegistry registry(NodeCapacity, ListenerCapacity, *std::pmr::get_default_resource());
    usize targetCount = 0;
    usize bubbleCount = 0;
    usize blockedCount = 0;
    auto target = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[&targetCount](UI::UIRoutedPointerEvent& routedEvent) noexcept {
            ++targetCount;
            routedEvent.stopImmediatePropagation();
        }},
        false);
    auto blocked = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[&blockedCount](UI::UIRoutedPointerEvent&) noexcept {
            ++blockedCount;
        }},
        false);
    auto bubble = registry.stage(
        descriptor(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown, UI::UIEventPhaseMask::Bubble),
        UI::UIRoutedPointerCallback{[&bubbleCount](UI::UIRoutedPointerEvent&) noexcept {
            ++bubbleCount;
        }},
        false);
    ASSERT_TRUE(target && blocked && bubble);
    commit(registry, *target);
    commit(registry, *blocked);
    commit(registry, *bubble);

    auto targetEvent = event();
    EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                UI::UIEventPhaseMask::Target, registry.registrationSerial(), targetEvent),
              1U);
    EXPECT_EQ(targetCount, 1U);
    EXPECT_EQ(blockedCount, 0U);
    EXPECT_EQ(bubbleCount, 0U);
}

TEST_F(UIRoutedPointerListenerRegistryTests, ListenerAddedDuringDispatchStartsAtNextBoundary)
{
    ListenerRegistry registry(NodeCapacity, ListenerCapacity, *std::pmr::get_default_resource());
    struct State final {
        ListenerRegistry* registry = nullptr;
        UI::UINodeId node{};
        usize firstCount = 0;
        usize secondCount = 0;
        usize addedCount = 0;
        bool added = false;
    } state{.registry = &registry, .node = firstNode_};

    auto first = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[statePointer = &state](UI::UIRoutedPointerEvent&) noexcept {
            ++statePointer->firstCount;
            if (statePointer->added)
            {
                return;
            }
            auto added = statePointer->registry->stage(
                descriptor(statePointer->node),
                UI::UIRoutedPointerCallback{[statePointer](UI::UIRoutedPointerEvent&) noexcept {
                    ++statePointer->addedCount;
                }},
                true);
            if (added)
            {
                statePointer->added =
                    statePointer->registry->commit(*added, {}, true).has_value();
            }
        }},
        false);
    auto second = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[statePointer = &state](UI::UIRoutedPointerEvent&) noexcept {
            ++statePointer->secondCount;
        }},
        false);
    ASSERT_TRUE(first && second);
    commit(registry, *first);
    commit(registry, *second);
    const u64 firstBoundary = registry.registrationSerial();

    auto firstRoute = event();
    EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                UI::UIEventPhaseMask::Target, firstBoundary, firstRoute),
              2U);
    EXPECT_TRUE(state.added);
    EXPECT_EQ(state.addedCount, 0U);

    auto secondRoute = event();
    EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                UI::UIEventPhaseMask::Target, registry.registrationSerial(), secondRoute),
              3U);
    EXPECT_EQ(state.firstCount, 2U);
    EXPECT_EQ(state.secondCount, 2U);
    EXPECT_EQ(state.addedCount, 1U);
}

TEST_F(UIRoutedPointerListenerRegistryTests, ResetLaterListenerSkipsAndDefersDestruction)
{
    ListenerRegistry registry(NodeCapacity, ListenerCapacity, *std::pmr::get_default_resource());
    struct State final {
        ListenerRegistry* registry = nullptr;
        u32 slot = UI::Detail::InvalidRoutedPointerListenerIndex;
        u32 generation = 0;
        usize firstCount = 0;
        usize secondCount = 0;
        usize destructionCount = 0;
    } state{.registry = &registry};

    auto first = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{[statePointer = &state](UI::UIRoutedPointerEvent&) noexcept {
            ++statePointer->firstCount;
            static_cast<void>(statePointer->registry->deactivate(
                statePointer->slot, statePointer->generation, {}, false));
        }},
        false);
    auto second = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{
            [statePointer = &state, probe = DestructionProbe{state.destructionCount}](
                UI::UIRoutedPointerEvent&) mutable noexcept {
                ++statePointer->secondCount;
            }},
        false);
    ASSERT_TRUE(first && second);
    state.slot = second->listenerIndex;
    state.generation = second->generation;
    commit(registry, *first);
    commit(registry, *second);

    auto routedEvent = event();
    EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                UI::UIEventPhaseMask::Target, registry.registrationSerial(), routedEvent),
              1U);
    EXPECT_EQ(state.firstCount, 1U);
    EXPECT_EQ(state.secondCount, 0U);
    EXPECT_EQ(state.destructionCount, 0U);
    EXPECT_EQ(registry.activeCount(), 1U);

    registry.reclaim(false);
    EXPECT_EQ(state.destructionCount, 1U);
}

TEST_F(UIRoutedPointerListenerRegistryTests, ClearNodePublishesBeforeGenerationSafeSlotReuse)
{
    ListenerRegistry registry(NodeCapacity, 2, *std::pmr::get_default_resource());
    TokenMirror mirror;
    std::array<ListenerRegistration, 2> old{};
    for (usize index = 0; index < old.size(); ++index)
    {
        auto registration = registry.stage(
            descriptor(firstNode_),
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}}, false);
        ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
        old[index] = *registration;
        commit(registry, old[index], mirror.publisher());
    }
    ASSERT_EQ(mirror.publicationCount, 2U);

    registry.clearNode(firstNode_.index(), mirror.publisher());
    EXPECT_EQ(registry.activeCount(), 0U);
    EXPECT_EQ(mirror.publicationCount, 4U);
    EXPECT_FALSE(mirror.outOfRange);
    for (const ListenerRegistration& registration : old)
    {
        EXPECT_FALSE(mirror.slots[registration.listenerIndex].active);
        EXPECT_EQ(mirror.slots[registration.listenerIndex].generation, registration.generation);
    }

    auto blocked = registry.stage(
        descriptor(firstNode_), UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}}, true);
    EXPECT_FALSE(blocked.has_value());
    registry.reclaim(false);

    auto replacement = registry.stage(
        descriptor(firstNode_), UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}}, false);
    ASSERT_TRUE(replacement.has_value()) << (replacement ? "" : replacement.error().message);
    commit(registry, *replacement, mirror.publisher());
    EXPECT_FALSE(registry.deactivate(old[0].listenerIndex, old[0].generation, mirror.publisher(), false));
    EXPECT_TRUE(mirror.slots[replacement->listenerIndex].active);
    EXPECT_EQ(mirror.slots[replacement->listenerIndex].generation, replacement->generation);
}

TEST_F(UIRoutedPointerListenerRegistryTests, CallbackDestructionCanReenterRegistrationSafely)
{
    ListenerRegistry registry(NodeCapacity, 2, *std::pmr::get_default_resource());
    RegistryReentryState state{.registry = &registry, .node = firstNode_};
    auto registration = registry.stage(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{
            [probe = RegistryReentryProbe{state}](UI::UIRoutedPointerEvent&) mutable noexcept {}},
        false);
    ASSERT_TRUE(registration.has_value()) << (registration ? "" : registration.error().message);
    commit(registry, *registration);

    EXPECT_TRUE(registry.deactivate(registration->listenerIndex, registration->generation, {}, false));
    EXPECT_EQ(state.destructionCount, 1U);
    EXPECT_TRUE(state.registrationSucceeded);
    EXPECT_EQ(registry.activeCount(), 1U);
}

TEST_F(UIRoutedPointerListenerRegistryTests, NodeDestroyMakesCallbackDestructorReentryStale)
{
    ContextReentryState state{.context = context_.get(), .node = firstNode_};
    auto listener = context_->input().addRoutedPointerListener(
        descriptor(firstNode_),
        UI::UIRoutedPointerCallback{
            [probe = ContextReentryProbe{state}](UI::UIRoutedPointerEvent&) mutable noexcept {}});
    ASSERT_TRUE(listener.has_value()) << (listener ? "" : listener.error().message);

    auto updaterResult = context_->authoring().treeUpdater(root_);
    ASSERT_TRUE(updaterResult.has_value()) << (updaterResult ? "" : updaterResult.error().message);
    UI::UITreeUpdater updater = std::move(*updaterResult);
    ASSERT_TRUE(updater.destroy(firstNode_).has_value());

    EXPECT_FALSE(listener->isActive());
    EXPECT_EQ(state.destructionCount, 1U);
    EXPECT_FALSE(state.registrationSucceeded);
    EXPECT_EQ(state.error, UI::UIErrorCode::InvalidNode);
}

TEST_F(UIRoutedPointerListenerRegistryTests, RepeatedSteadyStateOperationsDoNotAllocateAndReleasePmrStorage)
{
    ObservingMemoryResource resource;
    {
        ListenerRegistry registry(NodeCapacity, ListenerCapacity, resource);
        const usize constructionAllocationCount = resource.allocationCount();
        ASSERT_GT(constructionAllocationCount, 0U);
        usize invocationCount = 0;

        for (u64 iteration = 0; iteration < 300; ++iteration)
        {
            auto registration = registry.stage(
                descriptor(firstNode_),
                UI::UIRoutedPointerCallback{[&invocationCount](UI::UIRoutedPointerEvent&) noexcept {
                    ++invocationCount;
                }},
                false);
            ASSERT_TRUE(registration.has_value())
                << "iteration=" << iteration << ' '
                << (registration ? "" : registration.error().message);
            commit(registry, *registration);
            auto routedEvent = event();
            EXPECT_EQ(registry.dispatch(firstNode_, UI::UIRoutedPointerEventKind::ButtonDown,
                                        UI::UIEventPhaseMask::Target, registry.registrationSerial(), routedEvent),
                      1U);
            EXPECT_TRUE(registry.deactivate(registration->listenerIndex, registration->generation, {}, false));
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
