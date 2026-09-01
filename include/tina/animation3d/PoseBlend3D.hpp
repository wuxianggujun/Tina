#pragma once

#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/core/base/Types.hpp>

namespace Tina::Animation3D {

// Pose-level operations every higher layer is built from: crossfade, blend trees, layers
// and additive overlays all reduce to these.
//
// All of them are free functions over Pose3D rather than members, because the interesting
// argument is which poses and which mask, not which object owns the operation. They are
// noexcept and allocation-free: a blend tree evaluates several per frame.
//
// Rotation always goes through Math::slerp, never component-wise lerp. A component-wise
// quaternion blend is the classic animation defect: it is cheap and looks almost right,
// then collapses toward zero length near 180 degrees apart and the joint snaps.
//
// Every blend here reads `alpha` unclamped only where documented; the general contract is
// that alpha outside [0,1] is clamped, because a weight arriving from a state machine mid
// transition should never be able to extrapolate a pose into a nonsense orientation.

// destination = lerp(destination, source, alpha), for joints the mask includes.
//
// Named "overwrite" rather than "blend" because that is what alpha 1 does: the source
// replaces the destination for masked joints. This is the layer operator -- an override
// layer at full weight ignores whatever the layers below produced.
void blendOverwrite(Pose3D& destination, const Pose3D& source, float alpha,
                    const JointMask& mask) noexcept;

// destination = destination + (source - reference) * alpha, for joints the mask includes.
//
// Additive needs a reference pose to subtract, and getting that reference wrong is the
// single most common additive-animation defect: subtracting the bind pose when the clip was
// authored against its own first frame doubles every offset. The reference is therefore an
// explicit parameter with no default -- a caller must state what the clip's neutral is.
//
// Rotation composes as `destination * (reference^-1 * source)^alpha` via slerp from
// identity, so an additive rotation of alpha 0 is exactly the destination.
void blendAdditive(Pose3D& destination, const Pose3D& source, const Pose3D& reference,
                   float alpha, const JointMask& mask) noexcept;

// output = lerp(from, to, alpha) for every joint, ignoring masks.
//
// The two-input blend a blend tree node performs. Separate from blendOverwrite because a
// tree node writes into its own output rather than mutating an input: a node's inputs may
// be shared with a sibling node, and mutating one would make evaluation order observable.
void blendPair(Pose3D& output, const Pose3D& from, const Pose3D& to, float alpha) noexcept;

// Whether every transform in the pose is finite with a normalizable rotation. Called once
// per evaluated pose rather than per blend: checking inside each blend would triple the
// cost of a three-node tree to catch the same corruption at the same frame.
[[nodiscard]] bool isPoseFinite(const Pose3D& pose) noexcept;

// Renormalizes every rotation. Repeated slerp accumulates length drift, and a pose that
// feeds another blend next frame drifts again -- so a graph normalizes once at its root
// rather than trusting each operator to leave a unit quaternion behind.
void normalizeRotations(Pose3D& pose) noexcept;

} // namespace Tina::Animation3D
