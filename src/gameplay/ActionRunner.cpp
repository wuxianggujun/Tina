#include <tina/gameplay/Action.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/gameplay/GameplayErrors.hpp>

#include "ActionProgram.hpp"

#include <algorithm>
#include <memory>
#include <new>
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

} // namespace

struct ActionRunner::Impl final {
    struct Instance final {
        // Owned: play() consumes the Action, and the tree has to outlive the run.
        std::unique_ptr<Detail::ActionProgram> program{};
        std::pmr::vector<NodeState> states;
        bool ignoresTimeScale = false;
        bool paused = false;
        // Set when cancel() lands while this instance's own callbacks are running.
        // Checked at every node boundary, so no further node runs, and the tree
        // stays alive until the recursion has unwound.
        bool cancelPending = false;
        // Advance sequence this instance becomes eligible at. An action played from
        // inside a callback first advances on the next advance(), so every action's
        // first frame is identical regardless of where it was played.
        Core::u64 armedAtAdvance = 0;
    };

    using ActionPool = Core::GenerationPool<Instance, Detail::ActionRunnerTag>;

    Impl(const ActionRunnerConfig& configuration, std::pmr::memory_resource& resource,
         ActionPool&& actionPool)
        : config(configuration), memory(&resource), actions(std::move(actionPool)),
          liveActions(std::pmr::polymorphic_allocator<ActionId>{&resource})
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
    ActionRunnerStats stats{};

    [[nodiscard]] Instance* find(ActionId action) noexcept { return actions.tryGet(action); }
    [[nodiscard]] const Instance* find(ActionId action) const noexcept
    {
        return actions.tryGet(action);
    }

    void retire(ActionId action) noexcept
    {
        const auto position = std::find(liveActions.begin(), liveActions.end(), action);
        if (position != liveActions.end()) {
            liveActions.erase(position);
        }
        // Destroys every setter and callback in the tree, releasing what they
        // captured.
        (void)actions.erase(action);
    }

    void reclaimCancelled() noexcept
    {
        for (Core::usize index = 0; index < liveActions.size();) {
            const ActionId action = liveActions[index];
            Instance* const instance = find(action);
            if (instance != nullptr && instance->cancelPending) {
                retire(action);
                continue;
            }
            ++index;
        }
    }

    // Clears a subtree's cursors so a Repeat can run its child again. Iterative
    // rather than recursive: a 256-node tree is shallow, but this runs once per
    // repeat iteration and a stack frame per node is pure overhead.
    void resetSubtree(Instance& instance, Core::usize nodeIndex) noexcept
    {
        // Bounded by MaximumActionNodeCount, so the worklist cannot outgrow it.
        Core::usize pending[MaximumActionNodeCount];
        Core::usize pendingCount = 0;
        pending[pendingCount++] = nodeIndex;

        while (pendingCount > 0) {
            const Core::usize current = pending[--pendingCount];
            instance.states[current] = NodeState{};
            const Detail::ActionNode& node = instance.program->node(current);
            switch (node.kind) {
            case Detail::ActionNodeKind::Sequence:
            case Detail::ActionNodeKind::Parallel:
                for (Core::usize slot = 0; slot < node.childCount; ++slot) {
                    if (pendingCount < MaximumActionNodeCount) {
                        pending[pendingCount++] = instance.program->childIndex(node.firstChild + slot);
                    }
                }
                break;
            case Detail::ActionNodeKind::Repeat:
                if (pendingCount < MaximumActionNodeCount) {
                    pending[pendingCount++] = node.child;
                }
                break;
            case Detail::ActionNodeKind::Tween:
                break;
            }
        }
    }

    // Advances one node by `delta` and reports what it did not consume.
    //
    // Recursive because the tree is: depth is bounded by MaximumActionNodeCount and
    // in practice is a handful of levels, and the alternative -- an explicit stack
    // of resume points -- would have to reimplement the leftover hand-off that the
    // return value expresses directly.
    [[nodiscard]] StepResult step(ActionId owner, Instance& instance, Core::usize nodeIndex,
                                  Core::Duration delta)
    {
        NodeState& state = instance.states[nodeIndex];
        if (state.finished) {
            return StepResult{.leftover = delta, .finished = true};
        }
        // A cancel from inside a callback stops the walk here. Reported as finished
        // so every ancestor unwinds without running another node; the instance is
        // retired after advance() returns, not mid-recursion.
        if (instance.cancelPending) {
            return StepResult{.leftover = Core::Duration{0.0}, .finished = true};
        }

        const Detail::ActionNode& node = instance.program->node(nodeIndex);
        switch (node.kind) {
        case Detail::ActionNodeKind::Tween:
            return stepTween(instance, nodeIndex, node, state, delta);
        case Detail::ActionNodeKind::Sequence:
            return stepSequence(owner, instance, nodeIndex, delta);
        case Detail::ActionNodeKind::Parallel:
            return stepParallel(owner, instance, nodeIndex, delta);
        case Detail::ActionNodeKind::Repeat:
            return stepRepeat(owner, instance, nodeIndex, delta);
        }
        state.finished = true;
        return StepResult{.leftover = delta, .finished = true};
    }

    [[nodiscard]] StepResult stepTween(Instance& instance, Core::usize nodeIndex,
                                       const Detail::ActionNode& node, NodeState& state,
                                       Core::Duration delta)
    {
        // A zero duration applies exactly once at alpha 1. That is what "snap to
        // the end" means, and it is also what makes Action::call() a tween rather
        // than its own node kind.
        if (node.duration.count() <= 0.0) {
            state.finished = true;
            // Re-read through the program: the apply callback is game code and may
            // play or cancel, and the reference above must not be held across it.
            instance.program->node(nodeIndex).apply(1.0F);
            return StepResult{.leftover = delta, .finished = true};
        }

        state.elapsed += delta;
        if (state.elapsed >= node.duration) {
            const Core::Duration leftover = state.elapsed - node.duration;
            state.elapsed = node.duration;
            state.finished = true;
            // The final apply is at exactly 1, not at whatever the accumulated
            // elapsed divides to. A tween that stops one float epsilon short of its
            // authored target is the classic "sprite ends at 199.997" defect.
            instance.program->node(nodeIndex).apply(evaluateEasing(node.easing, 1.0F));
            return StepResult{.leftover = leftover, .finished = true};
        }

        const auto alpha = static_cast<float>(state.elapsed.count() / node.duration.count());
        instance.program->node(nodeIndex).apply(evaluateEasing(node.easing, alpha));
        return StepResult{.leftover = Core::Duration{0.0}, .finished = false};
    }

    [[nodiscard]] StepResult stepSequence(ActionId owner, Instance& instance,
                                          Core::usize nodeIndex, Core::Duration delta)
    {
        Core::Duration remaining = delta;
        for (;;) {
            const Detail::ActionNode& node = instance.program->node(nodeIndex);
            NodeState& state = instance.states[nodeIndex];
            if (state.cursor >= node.childCount) {
                state.finished = true;
                return StepResult{.leftover = remaining, .finished = true};
            }
            const Core::usize childIndex = instance.program->childIndex(node.firstChild + state.cursor);
            const StepResult child = step(owner, instance, childIndex, remaining);
            if (instance.cancelPending) {
                return StepResult{.leftover = Core::Duration{0.0}, .finished = true};
            }
            if (!child.finished) {
                return StepResult{.leftover = Core::Duration{0.0}, .finished = false};
            }
            // The child's leftover carries into the next one: a 0.2s child followed
            // by a 0.3s child, advanced by 0.25s, finishes the first and puts 0.05s
            // into the second. Dropping it is why hand-written sequences drift.
            remaining = child.leftover;
            ++instance.states[nodeIndex].cursor;
        }
    }

    [[nodiscard]] StepResult stepParallel(ActionId owner, Instance& instance,
                                          Core::usize nodeIndex, Core::Duration delta)
    {
        bool allFinished = true;
        // The parallel consumes as much as its longest-running child does, so the
        // leftover is the smallest across children. Taking the largest instead would
        // let a sequence start its next child before the slowest branch here ended.
        Core::Duration smallestLeftover = delta;
        const Core::usize childCount = instance.program->node(nodeIndex).childCount;
        const Core::usize firstChild = instance.program->node(nodeIndex).firstChild;

        for (Core::usize slot = 0; slot < childCount; ++slot) {
            const Core::usize childIndex = instance.program->childIndex(firstChild + slot);
            if (instance.states[childIndex].finished) {
                continue;
            }
            const StepResult child = step(owner, instance, childIndex, delta);
            if (instance.cancelPending) {
                return StepResult{.leftover = Core::Duration{0.0}, .finished = true};
            }
            if (!child.finished) {
                allFinished = false;
                continue;
            }
            smallestLeftover = (std::min)(smallestLeftover, child.leftover);
        }

        if (!allFinished) {
            return StepResult{.leftover = Core::Duration{0.0}, .finished = false};
        }
        instance.states[nodeIndex].finished = true;
        return StepResult{.leftover = smallestLeftover, .finished = true};
    }

    [[nodiscard]] StepResult stepRepeat(ActionId owner, Instance& instance, Core::usize nodeIndex,
                                        Core::Duration delta)
    {
        Core::Duration remaining = delta;
        Core::u32 restarts = 0;
        for (;;) {
            const Core::usize childIndex = instance.program->node(nodeIndex).child;
            const Repeat repeatSpec = instance.program->node(nodeIndex).repeat;
            const StepResult child = step(owner, instance, childIndex, remaining);
            if (instance.cancelPending) {
                return StepResult{.leftover = Core::Duration{0.0}, .finished = true};
            }
            if (!child.finished) {
                return StepResult{.leftover = Core::Duration{0.0}, .finished = false};
            }

            NodeState& state = instance.states[nodeIndex];
            ++state.iterations;
            if (repeatSpec.isComplete(state.iterations)) {
                state.finished = true;
                return StepResult{.leftover = child.leftover, .finished = true};
            }

            remaining = child.leftover;
            // Cleared before the bound is tested, not after. The iteration counted just
            // above has to leave a runnable child behind: returning with the child still
            // marked finished makes the next advance's step() return immediately while
            // this loop counts the iteration anyway, so a finite repeat silently applies
            // fewer times than it was authored to.
            resetSubtree(instance, childIndex);
            // A subtree whose total duration is zero would restart forever here, and
            // the failure looks exactly like a hang rather than like a content
            // error. Bounded and counted instead, so the cause is visible in stats.
            if (++restarts >= config.maximumRepeatIterationsPerAdvance) {
                ++stats.clampedRepeatIterations;
                return StepResult{.leftover = Core::Duration{0.0}, .finished = false};
            }
        }
    }
};

ActionRunner::ActionRunner(Impl* impl) noexcept : m_impl(impl) {}

ActionRunner::~ActionRunner() noexcept
{
    delete m_impl;
    m_impl = nullptr;
}

ActionRunner::ActionRunner(ActionRunner&& other) noexcept
    : m_impl(std::exchange(other.m_impl, nullptr))
{
}

ActionRunner& ActionRunner::operator=(ActionRunner&& other) noexcept
{
    if (this != &other) {
        delete m_impl;
        m_impl = std::exchange(other.m_impl, nullptr);
    }
    return *this;
}

Core::Result<ActionRunner> ActionRunner::Create(ActionRunnerConfig config)
{
    if (config.actionCapacity == 0) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner actionCapacity must be greater than zero");
    }
    if (config.maximumRepeatIterationsPerAdvance == 0) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner maximumRepeatIterationsPerAdvance must be at least 1");
    }

    std::pmr::memory_resource& resource = config.memoryResource != nullptr
        ? *config.memoryResource
        : *std::pmr::get_default_resource();

    auto actions = Impl::ActionPool::Create(config.actionCapacity, resource);
    if (!actions) {
        return Core::failure(GameplayErrorCode::AllocationFailed, actions.error().message);
    }

    try {
        auto* impl = new Impl(config, resource, std::move(*actions));
        // Reserved once so playing never allocates for bookkeeping.
        impl->liveActions.reserve(config.actionCapacity);
        return ActionRunner(impl);
    } catch (const std::bad_alloc&) {
        return Core::failure(GameplayErrorCode::AllocationFailed,
                             "ActionRunner storage allocation failed");
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
    if (m_impl->liveActions.size() >= m_impl->config.actionCapacity) {
        return Core::failure(GameplayErrorCode::CapacityExceeded,
                             "ActionRunner actionCapacity is exhausted");
    }

    Impl::Instance instance{
        .program = std::unique_ptr<Detail::ActionProgram>(
            std::exchange(action.m_program, nullptr)),
        .states = std::pmr::vector<NodeState>{
            std::pmr::polymorphic_allocator<NodeState>{m_impl->memory}},
        .ignoresTimeScale = options.ignoresTimeScale,
        .paused = options.startPaused,
        .cancelPending = false,
        .armedAtAdvance = m_impl->advanceSequence + (m_impl->dispatching ? 1 : 0),
    };
    try {
        instance.states.resize(instance.program->nodeCount());
    } catch (const std::bad_alloc&) {
        return Core::failure(GameplayErrorCode::AllocationFailed,
                             "ActionRunner node state allocation failed");
    }

    Core::Result<ActionId> played = m_impl->actions.tryEmplace(std::move(instance));
    if (!played) {
        return Core::failure(GameplayErrorCode::CapacityExceeded, played.error().message);
    }
    m_impl->liveActions.push_back(*played);
    ++m_impl->stats.startedCount;
    m_impl->stats.activeActionHighWater =
        (std::max)(m_impl->stats.activeActionHighWater, m_impl->liveActions.size());
    return *played;
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
    if (m_impl->dispatching) {
        // This may be the action whose callback is running; destroying the tree
        // here would free the callback mid-invocation.
        instance->cancelPending = true;
        return Core::success();
    }
    m_impl->retire(action);
    return Core::success();
}

void ActionRunner::cancelAll() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    if (m_impl->dispatching) {
        for (const ActionId action : m_impl->liveActions) {
            Impl::Instance* const instance = m_impl->find(action);
            if (instance != nullptr && !instance->cancelPending) {
                instance->cancelPending = true;
                ++m_impl->stats.cancelledCount;
            }
        }
        return;
    }
    m_impl->stats.cancelledCount += m_impl->liveActions.size();
    m_impl->liveActions.clear();
    m_impl->actions.clear();
}

Core::Status ActionRunner::setPaused(ActionId action, bool paused)
{
    if (m_impl == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidConfiguration,
                             "ActionRunner was not created");
    }
    Impl::Instance* const instance = m_impl->find(action);
    if (instance == nullptr || instance->cancelPending) {
        return Core::failure(GameplayErrorCode::InvalidHandle, "action handle is unknown");
    }
    instance->paused = paused;
    return Core::success();
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
    if (m_impl->dispatching) {
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
        impl.dispatching = false;
        ++impl.advanceSequence;
        impl.reclaimCancelled();
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
            impl.step(action, *instance, instance->program->rootIndex(), instanceDelta);

        instance = impl.find(action);
        if (instance == nullptr || instance->cancelPending) {
            continue;
        }
        if (result.finished) {
            // A completed action retires itself. Leaving it live would grow
            // activeCount for the life of the scene and eventually exhaust
            // actionCapacity with trees that can never advance again.
            instance->cancelPending = true;
            ++impl.stats.completedCount;
        }
    }

    return Core::success();
}

Core::usize ActionRunner::activeCount() const noexcept
{
    if (m_impl == nullptr) {
        return 0;
    }
    Core::usize count = 0;
    for (const ActionId action : m_impl->liveActions) {
        const Impl::Instance* const instance = m_impl->find(action);
        if (instance != nullptr && !instance->cancelPending) {
            ++count;
        }
    }
    return count;
}

ActionRunnerStats ActionRunner::stats() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    ActionRunnerStats snapshot = m_impl->stats;
    snapshot.actionCapacity = m_impl->config.actionCapacity;
    snapshot.activeActionCount = activeCount();
    return snapshot;
}

} // namespace Tina::Gameplay
