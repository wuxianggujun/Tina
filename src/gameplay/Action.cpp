#include <tina/gameplay/Action.hpp>

#include <tina/gameplay/GameplayErrors.hpp>

#include "ActionProgram.hpp"

#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
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
        node.firstSubtreeNode += nodeOffset;
        switch (node.kind) {
        case Detail::ActionNodeKind::Sequence:
        case Detail::ActionNodeKind::Parallel:
            node.firstChild += childOffset;
            break;
        case Detail::ActionNodeKind::Repeat:
        case Detail::ActionNodeKind::Speed:
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
        auto* previous = std::exchange(m_program, std::exchange(other.m_program, nullptr));
        m_failureCode = other.m_failureCode;
        m_failureMessage = std::exchange(other.m_failureMessage, nullptr);
        other.m_failureCode = Core::ErrorCode{};
        delete previous;
    }
    return *this;
}

void Action::reset() noexcept
{
    auto* previous = std::exchange(m_program, nullptr);
    m_failureCode = Core::ErrorCode{};
    m_failureMessage = nullptr;
    delete previous;
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
        program->setRoot(0, 1);
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
    Core::usize childDepth = 0;
    for (const Action& child : children) {
        constexpr Core::usize maximum = (std::numeric_limits<Core::usize>::max)();
        if (child.m_program->nodeCount() > maximum - totalNodes ||
            child.m_program->childIndexCount() > maximum - totalChildIndices) {
            for (Action& other : children) {
                other.reset();
            }
            return Action(GameplayErrorCode::CapacityExceeded, "Action storage size overflowed");
        }
        totalNodes += child.m_program->nodeCount();
        totalChildIndices += child.m_program->childIndexCount();
        childDepth = (std::max)(childDepth, child.m_program->maximumDepth());
    }

    Action result;
    try {
        auto program = std::make_unique<Detail::ActionProgram>();
        program->nodes().reserve(totalNodes);
        program->childIndices().reserve(totalChildIndices);

        // Reserve the parent's edge run first, eliminating a temporary roots
        // allocation. All potentially throwing storage growth precedes splice.
        program->childIndices().resize(children.size());
        for (Core::usize index = 0; index < children.size(); ++index) {
            program->childIndices()[index] = spliceProgram(*program, *children[index].m_program);
        }

        const Core::usize parentIndex = program->nodes().size();
        program->nodes().push_back(Detail::ActionNode{
            .kind = kind,
            .firstChild = 0,
            .childCount = children.size(),
        });
        program->setRoot(parentIndex, childDepth + 1);
        result.m_program = program.release();
    } catch (const std::bad_alloc&) {
        Action failure(GameplayErrorCode::AllocationFailed, "Action node allocation failed");
        for (Action& other : children) {
            other.reset();
        }
        return failure;
    } catch (const std::length_error&) {
        for (Action& other : children) {
            other.reset();
        }
        return Action(GameplayErrorCode::CapacityExceeded, "Action storage exceeds addressable vector size");
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
    Action result;
    try {
        // The child is consumed, so append to its existing flat program instead
        // of copying the entire subtree at every nesting level.
        auto program = std::unique_ptr<Detail::ActionProgram>(std::exchange(child.m_program, nullptr));
        const Core::usize childRoot = program->rootIndex();
        const Core::usize parentIndex = program->nodes().size();
        program->nodes().push_back(Detail::ActionNode{
            .kind = Detail::ActionNodeKind::Repeat,
            .repeat = repeatSpec,
            .child = childRoot,
        });
        program->setRoot(parentIndex, program->maximumDepth() + 1);
        result.m_program = program.release();
    } catch (const std::bad_alloc&) {
        return Action(GameplayErrorCode::AllocationFailed, "Action node allocation failed");
    } catch (const std::length_error&) {
        return Action(GameplayErrorCode::CapacityExceeded, "Action storage exceeds addressable vector size");
    }
    return result;
}

Action Action::speed(double scale, Action child)
{
    if (!std::isfinite(scale) || scale <= 0.0) {
        return Action(GameplayErrorCode::InvalidArgument, "speed must be finite and strictly positive");
    }
    if (child.failed()) {
        return Action(child.m_failureCode, child.m_failureMessage);
    }
    if (!child.hasValue()) {
        return Action(GameplayErrorCode::InvalidSequence, "speed child is empty or was already consumed");
    }
    Action result;
    try {
        auto program = std::unique_ptr<Detail::ActionProgram>(std::exchange(child.m_program, nullptr));
        const Core::usize childRoot = program->rootIndex();
        const Core::usize parentIndex = program->nodes().size();
        program->nodes().push_back(Detail::ActionNode{
            .kind = Detail::ActionNodeKind::Speed,
            .speed = scale,
            .child = childRoot,
        });
        program->setRoot(parentIndex, program->maximumDepth() + 1);
        result.m_program = program.release();
    } catch (const std::bad_alloc&) {
        return Action(GameplayErrorCode::AllocationFailed, "Action node allocation failed");
    } catch (const std::length_error&) {
        return Action(GameplayErrorCode::CapacityExceeded, "Action storage exceeds addressable vector size");
    }
    return result;
}

namespace {

void reverseNode(Detail::ActionProgram& program, Core::usize index)
{
    Detail::ActionNode& node = program.node(index);
    switch (node.kind) {
    case Detail::ActionNodeKind::Tween:
        node.reversed = !node.reversed;
        break;
    case Detail::ActionNodeKind::Sequence: {
        Core::usize left = node.firstChild;
        Core::usize right = node.firstChild + node.childCount;
        while (right > left) {
            --right;
            std::swap(program.childIndices()[left], program.childIndices()[right]);
            ++left;
        }
        for (Core::usize child = 0; child < node.childCount; ++child) {
            reverseNode(program, program.childIndex(node.firstChild + child));
        }
        break;
    }
    case Detail::ActionNodeKind::Parallel:
        for (Core::usize child = 0; child < node.childCount; ++child) {
            reverseNode(program, program.childIndex(node.firstChild + child));
        }
        break;
    case Detail::ActionNodeKind::Repeat:
    case Detail::ActionNodeKind::Speed:
        reverseNode(program, node.child);
        break;
    }
}

} // namespace

Action Action::reverse(Action child)
{
    if (child.failed()) {
        return Action(child.m_failureCode, child.m_failureMessage);
    }
    if (!child.hasValue()) {
        return Action(GameplayErrorCode::InvalidSequence, "reverse child is empty or was already consumed");
    }
    reverseNode(*child.m_program, child.m_program->rootIndex());
    return child;
}

} // namespace Tina::Gameplay
