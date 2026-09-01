#pragma once

#include <tina/animation3d/ClipSampler3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Animation3D {

// Hard authoring bound. A tree larger than this is a loop building nodes rather than an
// authored intent, and an unbounded tree would let one state allocate without limit.
inline constexpr Core::u16 MaximumBlendTreeNodeCount = 64;
inline constexpr Core::u16 MaximumBlendTreeParameters = 16;
inline constexpr Core::u16 BlendTreeNodeNone = 0xFFFF;

// Which parameter drives a blend node's weight. Parameters are indices rather than names
// because a tree is evaluated every frame and per-frame name lookup in a node walk is the
// cost that does not show up until a character has a dozen trees.
using BlendParameterIndex = Core::u16;
inline constexpr BlendParameterIndex BlendParameterNone = 0xFFFF;

enum class BlendTreeNodeKind : Core::u8 {
    // Leaf: samples one clip at its own playhead.
    Clip = 0,
    // Two inputs interpolated by one parameter in [0,1].
    Blend2 = 1,
    // N inputs positioned on a 1D axis, interpolated between the two straddling the
    // parameter value. This is the "walk/run/sprint by speed" node, and it is a distinct
    // kind rather than a chain of Blend2 because a chain makes the middle clips contribute
    // at parameter values where they should be silent.
    Blend1D = 2,
    // Adds a source onto a base relative to the source's own reference pose.
    Additive = 3,
};

struct BlendTreeNodeDesc final {
    BlendTreeNodeKind kind = BlendTreeNodeKind::Clip;

    // Clip: index into the tree's clip list.
    Core::u16 clipIndex = 0;
    // Clip playback speed. Negative plays the clip backwards.
    float speed = 1.0F;

    // Blend2 / Additive: exactly two inputs. Blend1D: up to MaximumBlendTreeNodeCount.
    std::span<const Core::u16> inputs{};
    // Blend1D only: one threshold per input, strictly increasing. The parameter is clamped
    // to the outer thresholds, so a value below the first or above the last yields that
    // endpoint input rather than extrapolating.
    std::span<const float> thresholds{};

    // Weight source. BlendParameterNone means a constant `weight`.
    BlendParameterIndex parameter = BlendParameterNone;
    float weight = 0.0F;

    // Additive only: the pose the source is relative to. Without this an additive node
    // doubles every offset when the clip was authored against its own first frame rather
    // than the bind pose, which is the most common additive defect.
    // ReferenceNone means the skeleton's bind pose.
    Core::u16 referenceClipIndex = BlendTreeNodeNone;
};

struct BlendTree3DDesc final {
    // Nodes in evaluation order: a node's inputs must all have lower indices. That makes
    // the tree acyclic by construction rather than by a cycle check, the same trick the
    // skeleton uses for parents.
    std::span<const BlendTreeNodeDesc> nodes{};
    Core::u16 rootNode = 0;
    Core::u16 parameterCount = 0;
};

// A bounded blend tree over one skeleton, evaluated into one pose per frame.
//
// It owns per-node pose buffers rather than a stack, because a node's inputs may be shared:
// a Blend1D and an Additive can both read the same clip leaf, and a stack would evaluate it
// twice or make evaluation order observable.
class BlendTree3D final {
  public:
    // Clips are borrowed, not owned: several trees and several states share the same clip,
    // and copying wire data per tree would multiply a character's animation memory by the
    // number of trees referencing it. The samplers must outlive this tree.
    [[nodiscard]] static Core::Result<BlendTree3D> Create(
        const BlendTree3DDesc& desc, const Skeleton3D& skeleton,
        std::span<const ClipSampler3D* const> clips,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    BlendTree3D(const BlendTree3D&) = delete;
    BlendTree3D& operator=(const BlendTree3D&) = delete;
    BlendTree3D(BlendTree3D&&) noexcept = default;
    BlendTree3D& operator=(BlendTree3D&&) = delete;

    [[nodiscard]] Core::u16 nodeCount() const noexcept
    {
        return static_cast<Core::u16>(m_nodes.size());
    }
    [[nodiscard]] Core::u16 parameterCount() const noexcept { return m_parameterCount; }

    [[nodiscard]] Core::Status setParameter(BlendParameterIndex index, float value) noexcept;
    [[nodiscard]] Core::Result<float> parameter(BlendParameterIndex index) const noexcept;

    // Advances every clip leaf's playhead. Separate from evaluate() because a state machine
    // must be able to evaluate a tree it is fading out of without advancing it -- otherwise
    // a fading state's clips run at double rate for the duration of the transition.
    [[nodiscard]] Core::Status advance(Core::Duration delta, float speedScale = 1.0F) noexcept;

    // Samples and blends into `outPose`. Const, so a transition can evaluate both its source
    // and destination tree in one frame.
    [[nodiscard]] Core::Status evaluate(const Skeleton3D& skeleton, Pose3D& outPose) const;

    // Playhead of one clip leaf, for root motion. Returns InvalidHandle for a non-leaf.
    [[nodiscard]] Core::Result<ClipPlayhead3D> leafPlayhead(Core::u16 node) const noexcept;
    // Restarts every leaf. A state re-entered from the beginning must not resume mid-clip.
    void restart() noexcept;

  private:
    struct Node final {
        BlendTreeNodeKind kind = BlendTreeNodeKind::Clip;
        Core::u16 clipIndex = 0;
        float speed = 1.0F;
        Core::u16 firstInput = 0;
        Core::u16 inputCount = 0;
        Core::u16 firstThreshold = 0;
        BlendParameterIndex parameter = BlendParameterNone;
        float weight = 0.0F;
        Core::u16 referenceClipIndex = BlendTreeNodeNone;
        // Additive only: slot in m_referencePoses. Resolved once at Create, because the
        // reference is either a bind pose or a clip's first frame and neither changes.
        Core::u16 referencePoseIndex = 0;
        ClipPlayhead3D playhead{};
    };

    BlendTree3D(std::pmr::vector<Node> nodes, std::pmr::vector<Core::u16> inputs,
                std::pmr::vector<float> thresholds,
                std::pmr::vector<const ClipSampler3D*> clips,
                std::pmr::vector<float> parameters, std::pmr::vector<Pose3D> nodePoses,
                std::pmr::vector<Pose3D> referencePoses, Core::u16 rootNode,
                Core::u16 parameterCount) noexcept;

    [[nodiscard]] float resolveWeight(const Node& node) const noexcept;

    std::pmr::vector<Node> m_nodes;
    std::pmr::vector<Core::u16> m_inputs;
    std::pmr::vector<float> m_thresholds;
    std::pmr::vector<const ClipSampler3D*> m_clips;
    std::pmr::vector<float> m_parameters;
    // One output buffer per node, so a shared input is evaluated once and read twice.
    // Mutable because evaluate() is const from the caller's perspective -- it produces a
    // pose without changing playback state -- while still needing its scratch space.
    mutable std::pmr::vector<Pose3D> m_nodePoses;
    // One per additive node, filled at Create. Not mutable: an additive reference is fixed
    // for the tree's life, which is the whole reason it can be precomputed.
    std::pmr::vector<Pose3D> m_referencePoses;
    Core::u16 m_rootNode = 0;
    Core::u16 m_parameterCount = 0;
};

} // namespace Tina::Animation3D
