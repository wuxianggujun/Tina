#include <tina/gameplay/Action.hpp>

#include <tina/gameplay/GameplayErrors.hpp>

#include "ActionProgram.hpp"

#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace Tina::Gameplay {

namespace {

// Copies `source`'s nodes onto the end of `destination` and returns where its root
// landed. Child indices are rebased by the offset, which is the whole reason nodes
// reference each other by index: a subtree is authored in its own program and then
// relocated into its parent's, and any pointer taken before that move would dangle.
[[nodiscard]] Core::usize spliceProgram(Detail::ActionProgram& destination,
                                       Detail::ActionProgram& source)
{
    const Core::usize nodeOffset = destination.nodes().size();
    const Core::usize childOffset = destination.childIndices().size();

    for (Core::usize index = 0; index < source.nodeCount(); ++index) {
        Detail::ActionNode node = std::move(source.node(index));
        switch (node.kind) {
        case Detail::ActionNodeKind::Sequence:
        case Detail::ActionNodeKind::Parallel:
            node.firstChild += childOffset;
            break;
        case Detail::ActionNodeKind::Repeat:
            node.child += nodeOffset;
            break;
        case Detail::ActionNodeKind::Tween:
            break;
        }
        destination.nodes().push_back(std::move(node));
    }
    for (const Core::usize childIndex : source.childIndices()) {
        destination.childIndices().push_back(childIndex + nodeOffset);
    }
    return source.rootIndex() + nodeOffset;
}

} // namespace

Action::Action(Core::ErrorCode code, const char* message) noexcept
    : m_failureCode(code), m_failureMessage(message)
{
}

Action::~Action() noexcept
{
    reset();
}

Action::Action(Action&& other) noexcept
    : m_program(std::exchange(other.m_program, nullptr)),
      m_failureCode(other.m_failureCode),
      m_failureMessage(std::exchange(other.m_failureMessage, nullptr))
{
    other.m_failureCode = Core::ErrorCode{};
}

Action& Action::operator=(Action&& other) noexcept
{
    if (this != &other) {
        reset();
        m_program = std::exchange(other.m_program, nullptr);
        m_failureCode = other.m_failureCode;
        m_failureMessage = std::exchange(other.m_failureMessage, nullptr);
        other.m_failureCode = Core::ErrorCode{};
    }
    return *this;
}

void Action::reset() noexcept
{
    delete m_program;
    m_program = nullptr;
    m_failureCode = Core::ErrorCode{};
    m_failureMessage = nullptr;
}

bool Action::hasValue() const noexcept
{
    return m_program != nullptr && m_failureMessage == nullptr;
}

Core::usize Action::nodeCount() const noexcept
{
    return m_program != nullptr ? m_program->nodeCount() : 0;
}

Core::Status Action::status() const
{
    if (m_failureMessage != nullptr) {
        return Core::failure(m_failureCode, m_failureMessage);
    }
    if (m_program == nullptr) {
        return Core::failure(GameplayErrorCode::InvalidSequence,
                             "Action is empty; it was default-constructed or already played");
    }
    return Core::success();
}

Action Action::tween(Core::Duration duration, Easing easing, TweenApply apply)
{
    if (!isValidDuration(duration)) {
        return Action(GameplayErrorCode::InvalidArgument,
                      "tween duration must be finite and non-negative");
    }
    if (!isValidEasing(easing)) {
        return Action(GameplayErrorCode::InvalidArgument, "tween easing is not a known curve");
    }
    if (!apply) {
        return Action(GameplayErrorCode::MissingCallback, "tween apply callback is empty");
    }

    Action result;
    try {
        auto program = std::make_unique<Detail::ActionProgram>();
        program->nodes().push_back(Detail::ActionNode{
            .kind = Detail::ActionNodeKind::Tween,
            .duration = duration,
            .easing = easing,
            .apply = std::move(apply),
        });
        program->setRootIndex(0);
        result.m_program = program.release();
    } catch (const std::bad_alloc&) {
        return Action(GameplayErrorCode::AllocationFailed, "Action node allocation failed");
    }
    return result;
}

Action Action::delay(Core::Duration duration)
{
    // A delay is a tween whose apply does nothing, rather than its own node kind:
    // the timing rule is identical, and a second kind would be a second place for
    // the remainder-carrying logic to disagree with itself.
    return tween(duration, Easing::Linear, [](float) noexcept {});
}

Action Action::sequence(std::span<Action> children)
{
    return combine(children, true);
}

Action Action::parallel(std::span<Action> children)
{
    return combine(children, false);
}

Action Action::combine(std::span<Action> children, bool sequential)
{
    const Detail::ActionNodeKind kind = sequential ? Detail::ActionNodeKind::Sequence
                                                   : Detail::ActionNodeKind::Parallel;
    if (children.empty()) {
        return Action(GameplayErrorCode::InvalidSequence,
                      "sequence/parallel requires at least one child");
    }
    // The first authoring failure among the children wins, and it is reported with
    // the child's own code and message. That is the point of fail-late authoring:
    // the diagnostic names the subexpression that was wrong, not the combinator
    // that happened to notice.
    for (Action& child : children) {
        if (child.failed()) {
            Action failure(child.m_failureCode, child.m_failureMessage);
            for (Action& other : children) {
                other.reset();
            }
            return failure;
        }
        if (!child.hasValue()) {
            Action failure(GameplayErrorCode::InvalidSequence,
                           "sequence/parallel child is empty or was already consumed");
            for (Action& other : children) {
                other.reset();
            }
            return failure;
        }
    }

    Core::usize totalNodes = 1;
    // Every child index the spliced subtrees already carry, plus one per child for
    // this node's own run. Reserved exactly, because spliceProgram must not
    // reallocate mid-splice: a throw there would leave the children half moved-from.
    Core::usize totalChildIndices = children.size();
    for (const Action& child : children) {
        totalNodes += child.m_program->nodeCount();
        totalChildIndices += child.m_program->childIndexCount();
    }
    if (totalNodes > MaximumActionNodeCount) {
        Action failure(GameplayErrorCode::CapacityExceeded,
                       "Action tree exceeds MaximumActionNodeCount");
        for (Action& other : children) {
            other.reset();
        }
        return failure;
    }

    Action result;
    try {
        auto program = std::make_unique<Detail::ActionProgram>();
        program->nodes().reserve(totalNodes);
        program->childIndices().reserve(totalChildIndices);

        // Children are spliced first so the parent node's firstChild run is already
        // final when it is written.
        std::vector<Core::usize> roots;
        roots.reserve(children.size());
        for (Action& child : children) {
            roots.push_back(spliceProgram(*program, *child.m_program));
        }

        const Core::usize firstChild = program->childIndices().size();
        for (const Core::usize root : roots) {
            program->childIndices().push_back(root);
        }

        const Core::usize parentIndex = program->nodes().size();
        program->nodes().push_back(Detail::ActionNode{
            .kind = kind,
            .firstChild = firstChild,
            .childCount = children.size(),
        });
        program->setRootIndex(parentIndex);
        result.m_program = program.release();
    } catch (const std::bad_alloc&) {
        Action failure(GameplayErrorCode::AllocationFailed, "Action node allocation failed");
        for (Action& other : children) {
            other.reset();
        }
        return failure;
    }

    // The children's storage was moved out, so releasing it here is what makes
    // "consumed exactly once" true rather than merely documented.
    for (Action& child : children) {
        child.reset();
    }
    return result;
}

Action Action::repeat(Repeat repeatSpec, Action child)
{
    if (!repeatSpec.isValid()) {
        return Action(GameplayErrorCode::InvalidArgument,
                      "repeat count must be at least 1 unless infinite");
    }
    if (child.failed()) {
        return Action(child.m_failureCode, child.m_failureMessage);
    }
    if (!child.hasValue()) {
        return Action(GameplayErrorCode::InvalidSequence,
                      "repeat child is empty or was already consumed");
    }
    if (child.m_program->nodeCount() + 1 > MaximumActionNodeCount) {
        return Action(GameplayErrorCode::CapacityExceeded,
                      "Action tree exceeds MaximumActionNodeCount");
    }

    Action result;
    try {
        auto program = std::make_unique<Detail::ActionProgram>();
        program->nodes().reserve(child.m_program->nodeCount() + 1);
        program->childIndices().reserve(child.m_program->childIndexCount());
        const Core::usize childRoot = spliceProgram(*program, *child.m_program);
        const Core::usize parentIndex = program->nodes().size();
        program->nodes().push_back(Detail::ActionNode{
            .kind = Detail::ActionNodeKind::Repeat,
            .repeat = repeatSpec,
            .child = childRoot,
        });
        program->setRootIndex(parentIndex);
        result.m_program = program.release();
    } catch (const std::bad_alloc&) {
        return Action(GameplayErrorCode::AllocationFailed, "Action node allocation failed");
    }
    return result;
}

} // namespace Tina::Gameplay
