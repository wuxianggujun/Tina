#include <tina/animation3d/BlendTree3D.hpp>

#include <tina/animation3d/PoseBlend3D.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::Animation3D {

BlendTree3D::BlendTree3D(std::pmr::vector<Node> nodes, std::pmr::vector<Core::u16> inputs,
                         std::pmr::vector<float> thresholds,
                         std::pmr::vector<const ClipSampler3D*> clips,
                         std::pmr::vector<float> parameters, std::pmr::vector<Pose3D> nodePoses,
                         std::pmr::vector<Pose3D> referencePoses, Core::u16 rootNode,
                         Core::u16 parameterCount) noexcept
    : m_nodes(std::move(nodes)), m_inputs(std::move(inputs)), m_thresholds(std::move(thresholds)),
      m_clips(std::move(clips)), m_parameters(std::move(parameters)),
      m_nodePoses(std::move(nodePoses)), m_referencePoses(std::move(referencePoses)),
      m_rootNode(rootNode), m_parameterCount(parameterCount)
{
}

Core::Result<BlendTree3D> BlendTree3D::Create(const BlendTree3DDesc& desc,
                                             const Skeleton3D& skeleton,
                                             std::span<const ClipSampler3D* const> clips,
                                             std::pmr::memory_resource& resource)
{
    if (desc.nodes.empty() || desc.nodes.size() > MaximumBlendTreeNodeCount) {
        return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                             "blend tree node count must be between 1 and the node bound");
    }
    if (desc.rootNode >= desc.nodes.size()) {
        return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                             "blend tree root node index is out of range");
    }
    if (desc.parameterCount > MaximumBlendTreeParameters) {
        return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                             "blend tree parameter count exceeds the bound");
    }
    for (const ClipSampler3D* const clip : clips) {
        if (clip == nullptr) {
            return Core::failure(Animation3DErrorCode::InvalidArgument,
                                 "blend tree clip list contains a null sampler");
        }
        if (clip->jointCount() != skeleton.jointCount()) {
            return Core::failure(Animation3DErrorCode::SkeletonMismatch,
                                 "blend tree clip joint count does not match the skeleton");
        }
    }

    try {
        std::pmr::vector<Node> nodes{std::pmr::polymorphic_allocator<Node>{&resource}};
        std::pmr::vector<Core::u16> inputs{std::pmr::polymorphic_allocator<Core::u16>{&resource}};
        std::pmr::vector<float> thresholds{std::pmr::polymorphic_allocator<float>{&resource}};
        std::pmr::vector<const ClipSampler3D*> ownedClips{
            std::pmr::polymorphic_allocator<const ClipSampler3D*>{&resource}};
        std::pmr::vector<float> parameters{std::pmr::polymorphic_allocator<float>{&resource}};
        std::pmr::vector<Pose3D> nodePoses{std::pmr::polymorphic_allocator<Pose3D>{&resource}};
        std::pmr::vector<Pose3D> referencePoses{std::pmr::polymorphic_allocator<Pose3D>{&resource}};
        nodes.reserve(desc.nodes.size());
        ownedClips.assign(clips.begin(), clips.end());
        parameters.resize(desc.parameterCount, 0.0F);

        for (Core::u16 index = 0; index < desc.nodes.size(); ++index) {
            const BlendTreeNodeDesc& source = desc.nodes[index];
            Node node{};
            node.kind = source.kind;
            node.parameter = source.parameter;
            node.weight = source.weight;

            if (source.parameter != BlendParameterNone && source.parameter >= desc.parameterCount) {
                return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                     "blend tree node references an out-of-range parameter");
            }
            if (!std::isfinite(source.weight)) {
                return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                     "blend tree node constant weight must be finite");
            }
            if (source.inputs.size() > MaximumBlendTreeNodeCount ||
                source.thresholds.size() > MaximumBlendTreeNodeCount) {
                return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                     "blend tree node input count exceeds the fixed bound");
            }

            // Inputs must have lower indices than the node consuming them. That is what
            // makes the tree acyclic by construction and lets evaluate() run as one forward
            // pass instead of a recursive walk with a visited set.
            for (const Core::u16 input : source.inputs) {
                if (input >= index) {
                    return Core::failure(
                        Animation3DErrorCode::InvalidBlendTree,
                        "blend tree node inputs must have lower indices than the node");
                }
            }

            switch (source.kind) {
            case BlendTreeNodeKind::Clip:
                if (!source.inputs.empty() || !source.thresholds.empty() ||
                    source.parameter != BlendParameterNone ||
                    source.referenceClipIndex != BlendTreeNodeNone) {
                    return Core::failure(
                        Animation3DErrorCode::InvalidBlendTree,
                        "blend tree clip node cannot declare inputs, thresholds, or a weight parameter");
                }
                if (source.clipIndex >= ownedClips.size()) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree clip node references an unknown clip");
                }
                if (!std::isfinite(source.speed)) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree clip speed must be finite");
                }
                node.clipIndex = source.clipIndex;
                node.speed = source.speed;
                node.playhead = ownedClips[source.clipIndex]->startPlayhead();
                break;

            case BlendTreeNodeKind::Blend2:
            case BlendTreeNodeKind::Additive:
                if (source.inputs.size() != 2U) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree Blend2/Additive nodes need exactly two inputs");
                }
                if (!source.thresholds.empty()) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree Blend2/Additive nodes cannot declare thresholds");
                }
                if (source.kind != BlendTreeNodeKind::Additive &&
                    source.referenceClipIndex != BlendTreeNodeNone) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree Blend2 node cannot declare an additive reference clip");
                }
                if (source.kind == BlendTreeNodeKind::Additive &&
                    source.referenceClipIndex != BlendTreeNodeNone &&
                    source.referenceClipIndex >= ownedClips.size()) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "additive node references an unknown reference clip");
                }
                node.referenceClipIndex = source.referenceClipIndex;
                if (source.kind == BlendTreeNodeKind::Additive) {
                    // Resolved here, once: the reference is a bind pose or a clip's first
                    // frame, neither of which changes, so sampling it per frame would be a
                    // per-frame allocation producing a constant.
                    auto reference = Pose3D::Create(skeleton.jointCount(), resource);
                    if (!reference) {
                        return Core::failure(reference.error());
                    }
                    if (source.referenceClipIndex == BlendTreeNodeNone) {
                        if (Core::Status status = skeleton.writeBindPose(*reference); !status) {
                            return Core::failure(status.error());
                        }
                    } else if (Core::Status status = ownedClips[source.referenceClipIndex]->sample(
                                   0.0F, skeleton, *reference);
                               !status) {
                        return Core::failure(status.error());
                    }
                    node.referencePoseIndex = static_cast<Core::u16>(referencePoses.size());
                    referencePoses.push_back(std::move(*reference));
                }
                break;

            case BlendTreeNodeKind::Blend1D: {
                if (source.referenceClipIndex != BlendTreeNodeNone) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree Blend1D node cannot declare an additive reference clip");
                }
                if (source.inputs.size() < 2U) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree Blend1D needs at least two inputs");
                }
                if (source.thresholds.size() != source.inputs.size()) {
                    return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                         "blend tree Blend1D needs one threshold per input");
                }
                // Strictly increasing: equal thresholds make the straddling pair ambiguous,
                // and a descending list would silently select the wrong pair.
                for (Core::usize slot = 0; slot < source.thresholds.size(); ++slot) {
                    if (!std::isfinite(source.thresholds[slot])) {
                        return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                             "blend tree Blend1D thresholds must be finite");
                    }
                    if (slot > 0U && !(source.thresholds[slot] > source.thresholds[slot - 1U])) {
                        return Core::failure(
                            Animation3DErrorCode::InvalidBlendTree,
                            "blend tree Blend1D thresholds must be strictly increasing");
                    }
                }
                node.firstThreshold = static_cast<Core::u16>(thresholds.size());
                thresholds.insert(thresholds.end(), source.thresholds.begin(),
                                  source.thresholds.end());
                break;
            }
            default:
                return Core::failure(Animation3DErrorCode::InvalidBlendTree,
                                     "blend tree node kind is not a known value");
            }

            node.firstInput = static_cast<Core::u16>(inputs.size());
            node.inputCount = static_cast<Core::u16>(source.inputs.size());
            inputs.insert(inputs.end(), source.inputs.begin(), source.inputs.end());
            nodes.push_back(node);
        }

        // One output buffer per node. A shared input is then evaluated once and read by
        // every consumer, rather than re-evaluated or overwritten by a stack discipline.
        nodePoses.reserve(nodes.size());
        for (Core::usize index = 0; index < nodes.size(); ++index) {
            auto pose = Pose3D::Create(skeleton.jointCount(), resource);
            if (!pose) {
                return Core::failure(pose.error());
            }
            nodePoses.push_back(std::move(*pose));
        }

        return BlendTree3D(std::move(nodes), std::move(inputs), std::move(thresholds),
                           std::move(ownedClips), std::move(parameters), std::move(nodePoses),
                           std::move(referencePoses), desc.rootNode, desc.parameterCount);
    } catch (const std::bad_alloc&) {
        return Core::failure(Animation3DErrorCode::AllocationFailed,
                             "blend tree allocation failed");
    }
}

Core::Status BlendTree3D::setParameter(BlendParameterIndex index, float value) noexcept
{
    if (index >= m_parameters.size()) {
        return Core::failure(Animation3DErrorCode::InvalidHandle,
                             "blend tree parameter index is out of range");
    }
    if (!std::isfinite(value)) {
        return Core::failure(Animation3DErrorCode::InvalidArgument,
                             "blend tree parameter must be finite");
    }
    m_parameters[index] = value;
    return Core::success();
}

Core::Result<float> BlendTree3D::parameter(BlendParameterIndex index) const noexcept
{
    if (index >= m_parameters.size()) {
        return Core::failure(Animation3DErrorCode::InvalidHandle,
                             "blend tree parameter index is out of range");
    }
    return m_parameters[index];
}

float BlendTree3D::resolveWeight(const Node& node) const noexcept
{
    if (node.parameter == BlendParameterNone || node.parameter >= m_parameters.size()) {
        return node.weight;
    }
    return m_parameters[node.parameter];
}

Core::Status BlendTree3D::advance(Core::Duration delta, float speedScale) noexcept
{
    if (!std::isfinite(speedScale)) {
        return Core::failure(Animation3DErrorCode::InvalidArgument,
                             "blend tree speed scale must be finite");
    }
    for (Node& node : m_nodes) {
        if (node.kind != BlendTreeNodeKind::Clip) {
            continue;
        }
        const ClipSampler3D* const clip = m_clips[node.clipIndex];
        auto advanced = clip->advance(node.playhead, delta, node.speed * speedScale);
        if (!advanced) {
            return Core::failure(advanced.error());
        }
        node.playhead = *advanced;
    }
    return Core::success();
}

Core::Status BlendTree3D::evaluate(const Skeleton3D& skeleton, Pose3D& outPose) const
{
    if (outPose.jointCount() != skeleton.jointCount()) {
        return Core::failure(Animation3DErrorCode::SkeletonMismatch,
                             "blend tree output pose does not match the skeleton");
    }

    // One forward pass: a node's inputs all have lower indices, so their poses are written
    // before this node reads them.
    for (Core::u16 index = 0; index < m_nodes.size(); ++index) {
        const Node& node = m_nodes[index];
        Pose3D& target = m_nodePoses[index];

        switch (node.kind) {
        case BlendTreeNodeKind::Clip: {
            const ClipSampler3D* const clip = m_clips[node.clipIndex];
            if (Core::Status status = clip->sample(node.playhead.timeSeconds, skeleton, target);
                !status) {
                return status;
            }
            break;
        }

        case BlendTreeNodeKind::Blend2: {
            const Pose3D& from = m_nodePoses[m_inputs[node.firstInput]];
            const Pose3D& to = m_nodePoses[m_inputs[node.firstInput + 1U]];
            blendPair(target, from, to, resolveWeight(node));
            break;
        }

        case BlendTreeNodeKind::Blend1D: {
            const float value = resolveWeight(node);
            const std::span<const float> thresholds{m_thresholds.data() + node.firstThreshold,
                                                    node.inputCount};
            // Clamped to the outer thresholds rather than extrapolated: a speed above the
            // fastest clip should play the fastest clip, not an invented faster pose.
            if (value <= thresholds.front()) {
                target.copyFrom(m_nodePoses[m_inputs[node.firstInput]]);
                break;
            }
            if (value >= thresholds.back()) {
                target.copyFrom(
                    m_nodePoses[m_inputs[node.firstInput + node.inputCount - 1U]]);
                break;
            }
            // Only the two straddling inputs contribute. A chain of Blend2 would leave the
            // middle clips contributing at values where they should be silent.
            Core::u16 upper = 1U;
            while (upper < node.inputCount && thresholds[upper] < value) {
                ++upper;
            }
            const Core::u16 lower = static_cast<Core::u16>(upper - 1U);
            const float span = thresholds[upper] - thresholds[lower];
            const float alpha = span > 0.0F ? ((value - thresholds[lower]) / span) : 0.0F;
            blendPair(target, m_nodePoses[m_inputs[node.firstInput + lower]],
                      m_nodePoses[m_inputs[node.firstInput + upper]], alpha);
            break;
        }

        case BlendTreeNodeKind::Additive: {
            const Pose3D& base = m_nodePoses[m_inputs[node.firstInput]];
            const Pose3D& source = m_nodePoses[m_inputs[node.firstInput + 1U]];
            target.copyFrom(base);
            // The reference was resolved once at Create -- either the skeleton's bind pose or
            // the reference clip's first frame. Re-sampling it here would allocate and
            // re-sample every frame to produce a value that cannot change.
            blendAdditive(target, source, m_referencePoses[node.referencePoseIndex],
                          resolveWeight(node), JointMask{});
            break;
        }
        }
    }

    outPose.copyFrom(m_nodePoses[m_rootNode]);
    return Core::success();
}

Core::Result<ClipPlayhead3D> BlendTree3D::leafPlayhead(Core::u16 node) const noexcept
{
    if (node >= m_nodes.size() || m_nodes[node].kind != BlendTreeNodeKind::Clip) {
        return Core::failure(Animation3DErrorCode::InvalidHandle,
                             "blend tree node is not a clip leaf");
    }
    return m_nodes[node].playhead;
}

void BlendTree3D::restart() noexcept
{
    for (Node& node : m_nodes) {
        if (node.kind == BlendTreeNodeKind::Clip) {
            node.playhead = m_clips[node.clipIndex]->startPlayhead();
        }
    }
}

} // namespace Tina::Animation3D
