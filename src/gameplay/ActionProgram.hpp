#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/gameplay/Action.hpp>
#include <tina/gameplay/Easing.hpp>
#include <tina/gameplay/GameplayTypes.hpp>

#include <vector>

namespace Tina::Gameplay::Detail {

enum class ActionNodeKind : Core::u8 {
    Tween = 0,
    Sequence,
    Parallel,
    Repeat,
};

// One authored node. Children are indices into the owning program's flat vector
// rather than pointers, so moving the program (or the vector growing while a
// nested factory appends) cannot invalidate them -- which pointers between
// separately-authored subtrees provably would, since each subtree starts life in
// its own program and is spliced into the parent's.
struct ActionNode final {
    ActionNodeKind kind = ActionNodeKind::Tween;

    // Tween.
    Core::Duration duration{};
    Easing easing = Easing::Linear;
    TweenApply apply{};

    // Sequence / Parallel: a contiguous run in the program's child index list.
    // Contiguous because children are spliced in one batch at authoring time and
    // never inserted afterwards, so a run beats a per-node vector.
    Core::usize firstChild = 0;
    Core::usize childCount = 0;

    // Repeat.
    Repeat repeat = Repeat::once();
    Core::usize child = 0;
};

// Flat node storage for one authored Action tree. Node 0 is never the root: index
// 0 is reserved so a default-constructed reference is distinguishable from a real
// one, and the root index is stored explicitly.
class ActionProgram final {
  public:
    ActionProgram() = default;

    ActionProgram(const ActionProgram&) = delete;
    ActionProgram& operator=(const ActionProgram&) = delete;
    ActionProgram(ActionProgram&&) noexcept = default;
    ActionProgram& operator=(ActionProgram&&) noexcept = default;

    [[nodiscard]] Core::usize nodeCount() const noexcept { return m_nodes.size(); }
    [[nodiscard]] Core::usize childIndexCount() const noexcept { return m_childIndices.size(); }
    [[nodiscard]] Core::usize rootIndex() const noexcept { return m_rootIndex; }
    void setRootIndex(Core::usize index) noexcept { m_rootIndex = index; }

    [[nodiscard]] ActionNode& node(Core::usize index) noexcept { return m_nodes[index]; }
    [[nodiscard]] const ActionNode& node(Core::usize index) const noexcept { return m_nodes[index]; }

    [[nodiscard]] Core::usize childIndex(Core::usize slot) const noexcept
    {
        return m_childIndices[slot];
    }

    [[nodiscard]] std::vector<ActionNode>& nodes() noexcept { return m_nodes; }
    [[nodiscard]] std::vector<Core::usize>& childIndices() noexcept { return m_childIndices; }

  private:
    std::vector<ActionNode> m_nodes;
    std::vector<Core::usize> m_childIndices;
    Core::usize m_rootIndex = 0;
};

} // namespace Tina::Gameplay::Detail
