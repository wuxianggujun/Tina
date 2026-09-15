#include <tina/gameplay/Action.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/gameplay/GameplayErrors.hpp>

#include "ActionProgram.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Tina::Gameplay {

namespace {

// Per-node execution state, one entry per program node. Kept beside the program
// rather than inside it so the same authored tree could be played twice without
// the two runs sharing cursors.
struct NodeState final {
    Core::Duration elapsed{};
    // Sequence: which child of its run is current. Repeat: unused.
    Core::usize cursor = 0;
    // Repeat: completed iterations of the child.
    Core::u32 iterations = 0;
    bool finished = false;
};

// What one node did with the time it was offered.
struct StepResult final {
    // Time the node did not consume. A sequence hands this to its next child,
    // which is what keeps a chain of tweens from drifting one advance's rounding
    // per boundary.
    Core::Duration leftover{};
    bool finished = false;
};

struct StepFrame final {
    Core::usize nodeIndex = 0;
    Core::Duration delta{};
    Core::Duration remaining{};
    Core::usize childCursor = 0;
    Core::u32 restarts = 0;
    bool allFinished = true;
};

} // namespace

struct ActionRunner::Impl final {
    struct Instance final {
        Instance(std::unique_ptr<Detail::ActionProgram> authored, std::pmr::memory_resource* resource,
                 ActionPlayOptions options, Core::u64 firstAdvance)
            : program(std::move(authored)), states(Core::usize{0}, resource), frames(Core::usize{0}, resource),
              ignoresTimeScale(options.ignoresTimeScale), paused(options.startPaused),
              armedAtAdvance(firstAdvance)
        {
            states.resize(program->nodeCount());
            frames.reserve(program->maximumDepth());
        }
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;
        Instance(Instance&&) = delete;
        Instance& operator=(Instance&&) = delete;

        // Owned: play() consumes the Action, and the tree has to outlive the run.
        std::unique_ptr<Detail::ActionProgram> program{};
        std::pmr::vector<NodeState> states;
        std::pmr::vector<StepFrame> frames;
        bool ignoresTimeScale = false;
        bool paused = false;
        // Set when cancel() lands while this instance's own callbacks are running.
        // Checked at every node boundary, so no further node runs, and the tree
        // stays alive until the execution stack has unwound.
        bool cancelPending = false;
        ActionId nextCancelled{};
        // Advance sequence this instance becomes eligible at. An action played from
        // inside a callback first advances on the next advance(), so every action's
        // first frame is identical regardless of where it was played.
        Core::u64 armedAtAdvance = 0;
    };

    using ActionPool = Core::GenerationPool<Instance, Detail::ActionRunnerTag>;

    Impl(const ActionRunnerConfig& configuration, std::pmr::memory_resource& resource,
         ActionPool&& actionPool)
        : config(configuration), memory(&resource), actions(std::move(actionPool)),
          liveActions(Core::usize{0}, std::pmr::polymorphic_allocator<ActionId>{&resource})
    {
    }

    ActionRunnerConfig config{};
    std::pmr::memory_resource* memory = nullptr;
    ActionPool actions;
    // Iteration order. The pool resolves ids but has no stable traversal, and
    // "actions advance in the order they were played" is the only order a game can
    // reason about.
    std::pmr::vector<ActionId> liveActions;
    double timeScale = 1.0;
    Core::u64 advanceSequence = 0;
    bool dispatching = false;
    bool reclaiming = false;
    ActionId cancelledHead{};
    Core::usize activeActions = 0;
    ActionRunnerStats stats{};

    [[nodiscard]] Instance* find(ActionId action) noexcept { return actions.tryGet(action); }
    [[nodiscard]] const Instance* find(ActionId action) const noexcept
    {
        return actions.tryGet(action);
    }

    void markCancelled(ActionId action, Instance& instance) noexcept
    {
        if (!instance.cancelPending) {
            instance.cancelPending = true;
            --activeActions;
            instance.nextCancelled = cancelledHead;
            cancelledHead = action;
        }
    }

    void reclaimCancelled() noexcept
    {
        reclaiming = true;
        while (cancelledHead) {
            const ActionId action = cancelledHead;
            cancelledHead = find(action)->nextCancelled;
            (void)actions.erase(action);
        }
        std::erase_if(liveActions, [this](ActionId action) { return !actions.contains(action); });
        reclaiming = false;
    }

    // Postorder authoring makes every subtree a contiguous node range.
    void resetSubtree(Instance& instance, Core::usize nodeIndex) noexcept
    {
        for (Core::usize index = instance.program->node(nodeIndex).firstSubtreeNode;
             index <= nodeIndex; ++index) {
            instance.states[index] = NodeState{};
        }
    }

    // Advances one node by `delta` and reports what it did not consume.
    //
    // Explicit continuation frames are reserved at play(), so arbitrary authored
    // depth neither consumes the C++ call stack nor allocates during advance().
    [[nodiscard]] StepResult step(Instance& instance, Core::usize nodeIndex,
                                  Core::Duration delta)
    {
        auto& frames = instance.frames;
        frames.clear();
        auto clearFrames = Core::makeScopeExit([&frames]() noexcept { frames.clear(); });
        frames.push_back({.nodeIndex = nodeIndex, .delta = delta, .remaining = delta});
        std::optional<StepResult> completed;
        const auto finish = [&](StepResult result) {
            frames.pop_back();
            completed = result;
        };
        while (!frames.empty()) {
            if (instance.cancelPending) {
                return {.leftover = Core::Duration::zero(), .finished = true};
            }
            StepFrame& frame = frames.back();
            NodeState& state = instance.states[frame.nodeIndex];
            const auto& node = instance.program->node(frame.nodeIndex);
            if (completed) {
                const StepResult child = *completed;
                completed.reset();
                switch (node.kind) {
                case Detail::ActionNodeKind::Sequence:
                    if (!child.finished) {
                        finish({});
                        continue;
                    }
                    frame.remaining = child.leftover;
                    ++state.cursor;
                    break;
                case Detail::ActionNodeKind::Parallel:
                    frame.allFinished = frame.allFinished && child.finished;
                    if (child.finished) {
                        frame.remaining = (std::min)(frame.remaining, child.leftover);
                    }
                    ++frame.childCursor;
                    break;
                case Detail::ActionNodeKind::Repeat:
                    if (!child.finished) {
                        finish({});
                        continue;
                    }
                    ++state.iterations;
                    if (node.repeat.isComplete(state.iterations)) {
                        state.finished = true;
                        finish(child);
                        continue;
                    }
                    frame.remaining = child.leftover;
                    resetSubtree(instance, node.child);
                    if (++frame.restarts >= config.maximumRepeatIterationsPerAdvance) {
                        ++stats.clampedRepeatIterations;
                        finish({});
                        continue;
                    }
                    break;
                case Detail::ActionNodeKind::Speed: {
                    const double leftover = child.leftover.count() / node.speed;
                    finish({.leftover = Core::Duration{leftover}, .finished = child.finished});
                    continue;
                }
                case Detail::ActionNodeKind::Tween:
                    break;
                }
            }
            if (state.finished) {
                finish({.leftover = frame.delta, .finished = true});
                continue;
            }
            switch (node.kind) {
            case Detail::ActionNodeKind::Tween:
                finish(stepTween(instance, frame.nodeIndex, node, state, frame.delta));
                break;
            case Detail::ActionNodeKind::Sequence:
                if (state.cursor == node.childCount) {
                    state.finished = true;
                    finish({.leftover = frame.remaining, .finished = true});
                } else {
                    frames.push_back({.nodeIndex = instance.program->childIndex(node.firstChild + state.cursor),
                                      .delta = frame.remaining, .remaining = frame.remaining});
                }
                break;
            case Detail::ActionNodeKind::Parallel:
                while (frame.childCursor < node.childCount &&
                       instance.states[instance.program->childIndex(node.firstChild + frame.childCursor)].finished) {
                    ++frame.childCursor;
                }
                if (frame.childCursor == node.childCount) {
                    state.finished = frame.allFinished;
                    finish({.leftover = frame.allFinished ? frame.remaining : Core::Duration::zero(),
                            .finished = frame.allFinished});
                } else {
                    frames.push_back({.nodeIndex = instance.program->childIndex(node.firstChild + frame.childCursor),
                                      .delta = frame.delta, .remaining = frame.delta});
                }
                break;
            case Detail::ActionNodeKind::Repeat:
                frames.push_back({.nodeIndex = node.child, .delta = frame.remaining, .remaining = frame.remaining});
                break;
            case Detail::ActionNodeKind::Speed: {
                const Core::Duration scaled{frame.remaining.count() * node.speed};
                frames.push_back({.nodeIndex = node.child, .delta = scaled, .remaining = scaled});
                break;
            }
            }
        }
        return *completed;
    }

    [[nodiscard]] StepResult stepTween(Instance& instance, Core::usize nodeIndex,
                                       const Detail::ActionNode& node, NodeState& state,
                                       Core::Duration delta)
    {
        // A zero duration applies exactly once at alpha 1. That is what "snap to
        // the end" means, and it is also what makes Action::call() a tween rather
        // than its own node kind.
        const auto applyAlpha = [&](float linearAlpha) {
            if (instance.program->node(nodeIndex).reversed) {
                linearAlpha = 1.0F - linearAlpha;
            }
            instance.program->node(nodeIndex).apply(
                evaluateEasing(instance.program->node(nodeIndex).easing, linearAlpha));
        };
        if (node.duration.count() <= 0.0) {
            state.finished = true;
            applyAlpha(1.0F);
            return StepResult{.leftover = delta, .finished = true};
        }

        state.elapsed += delta;
        if (state.elapsed >= node.duration) {
            const Core::Duration leftover = state.elapsed - node.duration;
            state.elapsed = node.duration;
            state.finished = true;
            applyAlpha(1.0F);
            return StepResult{.leftover = leftover, .finished = true};
        }

        const auto alpha = static_cast<float>(state.elapsed.count() / node.duration.count());
        applyAlpha(alpha);
        return StepResult{.leftover = Core::Duration{0.0}, .finished = false};
    }

};

ActionRunner::ActionRunner(Impl* impl) noexcept : m_impl(impl) {}

ActionRunner::~ActionRunner() noexcept
{
    if (m_impl && (m_impl->dispatching || m_impl->reclaiming)) { std::terminate(); }
    delete std::exchange(m_impl, nullptr);
}

ActionRunner::ActionRunner(ActionRunner&& other) noexcept
{
    if (other.m_impl && (other.m_impl->dispatching || other.m_impl->reclaiming)) { std::terminate(); }
    m_impl = std::exchange(other.m_impl, nullptr);
}

ActionRunner& ActionRunner::operator=(ActionRunner&& other) noexcept
{
    if (this != &other) {
        if ((m_impl && (m_impl->dispatching || m_impl->reclaiming)) ||
            (other.m_impl && (other.m_impl->dispatching || other.m_impl->reclaiming))) { std::terminate(); }
        delete std::exchange(m_impl, std::exchange(other.m_impl, nullptr));
    }
    return *this;
}

Core::Result<ActionRunner> ActionRunner::Create(ActionRunnerConfig config)
{
    if (config.maximumRepeatIterationsPerAdvance == 0) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner maximumRepeatIterationsPerAdvance must be at least 1");
    }

    std::pmr::memory_resource& resource = config.memoryResource != nullptr
        ? *config.memoryResource
        : *std::pmr::get_default_resource();

    auto actions = Impl::ActionPool::Create(config.initialActionReserve, resource);
    if (!actions) {
        return Core::failure(std::move(actions.error()).withContext("ActionRunner::Create", "action slots"));
    }

    try {
        auto impl = std::make_unique<Impl>(config, resource, std::move(*actions));
        impl->liveActions.reserve(config.initialActionReserve);
        return ActionRunner(impl.release());
    } catch (const std::bad_alloc&) {
        return Core::failure(GameplayErrorCode::AllocationFailed,
                             "ActionRunner storage allocation failed");
    } catch (const std::length_error&) {
        return Core::failure(GameplayErrorCode::CapacityExceeded,
                             "ActionRunner storage exceeds addressable vector size");
    }
}

Core::Result<ActionId> ActionRunner::play(Action action, ActionPlayOptions options)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    // Reports the authoring failure the action recorded, so the diagnostic names
    // the subexpression that was wrong rather than this call.
    if (Core::Status authoring = action.status(); !authoring) {
        return Core::failure(authoring.error());
    }
    if (m_impl->actions.availableCount() == 0) {
        if (m_impl->actions.capacity() == ActionId::InvalidIndex) {
            return Core::failure(GameplayErrorCode::CapacityExceeded, "ActionId index space is exhausted");
        }
        if (auto status = m_impl->actions.reserve(m_impl->actions.capacity() + 1); !status) {
            return Core::failure(std::move(status.error()));
        }
    }

    try {
        auto& order = m_impl->liveActions;
        if (order.size() == order.max_size()) {
            return Core::failure(GameplayErrorCode::CapacityExceeded, "ActionRunner order storage is exhausted");
        }
        const Core::usize required = (std::max)(m_impl->actions.capacity(), order.size() + 1);
        if (required > order.capacity()) {
            const Core::usize grown = order.capacity() > order.max_size() / 2
                ? order.max_size() : order.capacity() * 2;
            order.reserve((std::max)(required, grown));
        }

        // Construct PMR vectors directly in their stable slot. Moving a temporary
        // vector through a noexcept facade can allocate a Debug iterator proxy.
        auto program = std::unique_ptr<Detail::ActionProgram>(std::exchange(action.m_program, nullptr));
        Core::Result<ActionId> played = m_impl->actions.tryEmplace(
            std::move(program), m_impl->memory, options,
            m_impl->advanceSequence + (m_impl->dispatching ? 1 : 0));
        if (!played) {
            return Core::failure(std::move(played.error()));
        }
        m_impl->liveActions.push_back(*played);
        ++m_impl->activeActions;
        ++m_impl->stats.startedCount;
        m_impl->stats.activeActionHighWater =
            (std::max)(m_impl->stats.activeActionHighWater, m_impl->activeActions);
        return *played;
    } catch (const std::bad_alloc&) {
        return Core::failure(GameplayErrorCode::AllocationFailed,
                             "ActionRunner node state allocation failed");
    } catch (const std::length_error&) {
        return Core::failure(GameplayErrorCode::CapacityExceeded,
                             "ActionRunner storage exceeds addressable vector size");
    }
}

Core::Status ActionRunner::cancel(ActionId action)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    Impl::Instance* const instance = m_impl->find(action);
    if (instance == nullptr || instance->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle,
                             "action handle is unknown or already cancelled");
    }
    ++m_impl->stats.cancelledCount;
    m_impl->markCancelled(action, *instance);
    if (!m_impl->dispatching && !m_impl->reclaiming) {
        m_impl->reclaimCancelled();
    }
    return Core::success();
}

void ActionRunner::cancelAll() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    for (const ActionId action : m_impl->liveActions) {
        Impl::Instance* const instance = m_impl->find(action);
        if (instance != nullptr && !instance->cancelPending) {
            m_impl->markCancelled(action, *instance);
            ++m_impl->stats.cancelledCount;
        }
    }
    if (!m_impl->dispatching && !m_impl->reclaiming) {
        m_impl->reclaimCancelled();
    }
}

Core::Status ActionRunner::pause(ActionId action)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    Impl::Instance* const instance = m_impl->find(action);
    if (instance == nullptr || instance->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle, "action handle is unknown");
    }
    instance->paused = true;
    return Core::success();
}

Core::Status ActionRunner::resume(ActionId action)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    Impl::Instance* const instance = m_impl->find(action);
    if (instance == nullptr || instance->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle, "action handle is unknown");
    }
    instance->paused = false;
    return Core::success();
}

void ActionRunner::pauseAll() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    for (const ActionId action : m_impl->liveActions) {
        if (Impl::Instance* const instance = m_impl->find(action); instance != nullptr && !instance->cancelPending) {
            instance->paused = true;
        }
    }
}

void ActionRunner::resumeAll() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    for (const ActionId action : m_impl->liveActions) {
        if (Impl::Instance* const instance = m_impl->find(action); instance != nullptr && !instance->cancelPending) {
            instance->paused = false;
        }
    }
}

Core::Result<bool> ActionRunner::isPaused(ActionId action) const
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    const Impl::Instance* const instance = m_impl->find(action);
    if (instance == nullptr || instance->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle, "action handle is unknown");
    }
    return instance->paused;
}

bool ActionRunner::isPlaying(ActionId action) const noexcept
{
    if (m_impl == nullptr) {
        return false;
    }
    const Impl::Instance* const instance = m_impl->find(action);
    return instance != nullptr && !instance->cancelPending;
}

Core::Status ActionRunner::setTimeScale(double scale)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    if (!isValidTimeScale(scale)) {
        return Core::failure(GameplayErrorCode::InvalidArgument,
                             "time scale must be finite and non-negative");
    }
    m_impl->timeScale = scale;
    return Core::success();
}

double ActionRunner::timeScale() const noexcept
{
    return m_impl != nullptr ? m_impl->timeScale : 1.0;
}

Core::Status ActionRunner::advance(Core::Duration delta)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    if (!isValidDuration(delta)) {
        return Core::failure(GameplayErrorCode::InvalidArgument,
                             "advance delta must be finite and non-negative");
    }
    if (m_impl->dispatching || m_impl->reclaiming) {
        return Core::failure(GameplayErrorCode::ReentrantDispatch,
                             "ActionRunner::advance was re-entered from an action callback");
    }

    Impl& impl = *m_impl;
    const Core::u64 sequence = impl.advanceSequence;
    ++impl.stats.advanceCount;
    impl.dispatching = true;
    // A guard rather than end-of-function restores: a setter is game code and may
    // throw, and a runner left permanently "dispatching" would refuse every later
    // advance for the rest of the process.
    auto endDispatch = Core::makeScopeExit([&impl]() noexcept {
        impl.reclaimCancelled();
        impl.dispatching = false;
        ++impl.advanceSequence;
    });

    const Core::Duration scaledDelta{delta.count() * impl.timeScale};

    // Indexed over a size captured before the loop: a callback may play new actions,
    // and those are armed for the next advance anyway.
    const Core::usize actionCount = impl.liveActions.size();
    for (Core::usize index = 0; index < actionCount; ++index) {
        // Re-resolved rather than held: a callback may cancel other actions, and
        // liveActions only shrinks after the loop, so an id can outlive its instance
        // within it.
        const ActionId action = impl.liveActions[index];
        Impl::Instance* instance = impl.find(action);
        if (instance == nullptr || instance->cancelPending || instance->paused ||
            instance->armedAtAdvance > sequence) {
            continue;
        }

        const Core::Duration instanceDelta = instance->ignoresTimeScale ? delta : scaledDelta;
        const StepResult result =
            impl.step(*instance, instance->program->rootIndex(), instanceDelta);

        instance = impl.find(action);
        if (instance == nullptr || instance->cancelPending) {
            continue;
        }
        if (result.finished) {
            impl.markCancelled(action, *instance);
            ++impl.stats.completedCount;
        }
    }

    return Core::success();
}

Core::usize ActionRunner::activeCount() const noexcept
{
    return m_impl != nullptr ? m_impl->activeActions : 0;
}

ActionRunnerStats ActionRunner::stats() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    ActionRunnerStats snapshot = m_impl->stats;
    snapshot.reservedActionSlots = m_impl->actions.capacity();
    snapshot.activeActionCount = activeCount();
    return snapshot;
}

} // namespace Tina::Gameplay
