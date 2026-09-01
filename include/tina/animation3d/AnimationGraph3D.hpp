#pragma once

#include <tina/animation3d/BlendTree3D.hpp>
#include <tina/animation3d/ClipSampler3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/gameplay/Easing.hpp>
#include <tina/math/Vec.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace Tina::Animation3D {

class AnimationGraph3D;

// Handles into one graph's state and layer tables.
//
// Not Core::GenerationId: that type can only be minted by a GenerationPool, and these tables
// never erase an entry, so a pool's free list and per-slot generations would be dead weight.
// What is still needed is that a handle from one graph cannot address another's slot, so each
// handle carries the owning graph's token -- the same protection a pool's owner token gives,
// without the storage a pool implies.
template <typename Tag>
class GraphHandle final {
  public:
    constexpr GraphHandle() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept { return m_owner != 0U; }
    [[nodiscard]] constexpr Core::u16 index() const noexcept { return m_index; }
    explicit constexpr operator bool() const noexcept { return hasValue(); }

    friend constexpr bool operator==(const GraphHandle&, const GraphHandle&) noexcept = default;

  private:
    friend class AnimationGraph3D;

    constexpr GraphHandle(Core::u32 owner, Core::u16 index) noexcept
        : m_owner(owner), m_index(index)
    {
    }

    [[nodiscard]] constexpr Core::u32 owner() const noexcept { return m_owner; }

    Core::u32 m_owner = 0;
    Core::u16 m_index = 0;
};

namespace Detail {
struct StateTag final {
};
struct LayerTag final {
};
} // namespace Detail

using StateId = GraphHandle<Detail::StateTag>;
using LayerId = GraphHandle<Detail::LayerTag>;

inline constexpr Core::u16 MaximumStateCount = 64;
inline constexpr Core::u16 MaximumLayerCount = 8;
inline constexpr Core::u16 MaximumTransitionCount = 128;

// How a layer combines with what the layers below it produced.
enum class LayerBlendMode : Core::u8 {
    // Replaces the lower result for masked joints. The ordinary "upper body aims a weapon
    // while the legs walk" layer.
    Override = 0,
    // Adds relative to a reference pose. Needs a reference clip, because subtracting the
    // bind pose when the clip was authored against its own first frame doubles every offset.
    Additive = 1,
};

// A state's pose source. A state is either one clip or one blend tree, never both: allowing
// both would mean two playheads to keep in sync and two restart semantics.
enum class StateSourceKind : Core::u8 {
    Clip = 0,
    BlendTree = 1,
};

struct StateDesc final {
    StateSourceKind kind = StateSourceKind::Clip;
    // Clip source: index into the clip list this graph was built with.
    Core::u16 clipIndex = 0;
    // BlendTree source: index into the tree list.
    Core::u16 blendTreeIndex = 0;
    float speed = 1.0F;
    // Re-enters from the clip start. False resumes where this state left off, which is what
    // a locomotion state wants when it is re-entered mid-stride.
    bool restartOnEnter = true;
};

struct TransitionDesc final {
    StateId from{};
    StateId to{};
    Core::Duration duration{0.2};
    Gameplay::Easing easing = Gameplay::Easing::QuadraticInOut;
    // Interrupts an in-flight transition. Without this, a transition requested while another
    // is running is either dropped (input feels ignored) or stacked (poses accumulate);
    // both are wrong, so the choice is explicit per transition.
    bool canInterrupt = true;
};

struct LayerDesc final {
    LayerBlendMode mode = LayerBlendMode::Override;
    float weight = 1.0F;
    // Empty mask means every joint.
    JointMask mask{};
    // Additive mode only. BlendTreeNodeNone means the skeleton's bind pose.
    Core::u16 referenceClipIndex = BlendTreeNodeNone;
};

// Translation and rotation the root joint accumulated over one advance, lifted out of the
// pose so a character controller can apply it to the entity rather than to the skeleton.
//
// This is what "root motion" means here: the animation stops moving the root joint, and the
// delta becomes the caller's to apply. Leaving the root in the pose *and* reporting a delta
// would move the character twice, which is the classic root-motion defect.
struct RootMotionDelta3D final {
    Math::Vec3 translation{};
    Math::Quaternion rotation{0.0F, 0.0F, 0.0F, 1.0F};
    // True when the clip crossed a Loop boundary or bounced at a PingPong endpoint during
    // this advance. The delta follows the unfolded time path rather than subtracting folded
    // endpoints, which would lose full traversals or use the wrong direction.
    bool wrapped = false;
};

struct RootMotionConfig final {
    bool enabled = false;
    // Which joint carries the motion. Usually joint 0, but a rig may have a dedicated root
    // above the hips, so it is named rather than assumed.
    Core::u16 rootJoint = 0;
    bool applyTranslationXZ = true;
    // Vertical motion usually belongs to the controller (gravity, jump arcs), not the clip.
    bool applyTranslationY = false;
    bool applyRotation = true;
};

struct AnimationGraph3DConfig final {
    Core::u16 stateCapacity = 32;
    Core::u16 transitionCapacity = 64;
    Core::u16 layerCapacity = 4;
    RootMotionConfig rootMotion{};
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct AnimationGraph3DStats final {
    Core::u16 stateCount = 0;
    Core::u16 transitionCount = 0;
    Core::u16 layerCount = 0;
    Core::u64 advanceCount = 0;
    Core::u64 transitionsStarted = 0;
    // Transitions refused because one was in flight and it was not interruptible. Non-zero
    // means requests are being dropped, which is a tuning signal rather than an error.
    Core::u64 transitionsRefused = 0;
    Core::u64 evaluationFailures = 0;
    bool transitionInFlight = false;
};

// The owner that turns states, transitions and layers into one pose per frame.
//
// It exists because the pieces below it compose in exactly one correct order and every
// mistake in that order is silent: advancing a state you are fading out of makes its clips
// run at double speed, sampling before advancing renders a frame behind, and applying root
// motion without removing it from the pose moves the character twice.
//
// Layer 0 is the base layer and always covers every joint -- a graph whose base layer were
// masked would leave unmasked joints undefined, and "undefined" here means whatever the
// pose buffer happened to hold last frame.
//
// Single-owner and not thread-safe. Time arrives as an explicit delta for the same reason
// the Gameplay Scheduler's does: the frame loop owns the fixed/frame split (ADR 0015).
class AnimationGraph3D final {
  public:
    // Clips and trees are borrowed and must outlive the graph. Clip samplers are immutable
    // and may be shared by character instances. Blend trees own mutable parameters and
    // playheads, so each live graph must receive distinct tree instances.
    [[nodiscard]] static Core::Result<AnimationGraph3D> Create(
        const Skeleton3D& skeleton, std::span<const ClipSampler3D* const> clips,
        std::span<BlendTree3D* const> blendTrees, AnimationGraph3DConfig config = {});

    ~AnimationGraph3D() noexcept;

    AnimationGraph3D(const AnimationGraph3D&) = delete;
    AnimationGraph3D& operator=(const AnimationGraph3D&) = delete;
    AnimationGraph3D(AnimationGraph3D&& other) noexcept;
    AnimationGraph3D& operator=(AnimationGraph3D&& other) noexcept;

    [[nodiscard]] Core::Result<LayerId> addLayer(const LayerDesc& desc);
    // Layer 0 is created by Create and always animates every joint.
    [[nodiscard]] LayerId baseLayer() const noexcept;

    [[nodiscard]] Core::Result<StateId> addState(LayerId layer, const StateDesc& desc);
    [[nodiscard]] Core::Status addTransition(LayerId layer, const TransitionDesc& desc);

    // Enters a state with no blend. For the initial state, or a hard cut.
    [[nodiscard]] Core::Status setState(LayerId layer, StateId state);

    // Crossfades to `state` using the authored transition from the current one. Fails
    // InvalidTransition when no transition connects them -- rather than falling back to a
    // default duration, because a missing transition is an authoring gap and a silent
    // default hides it for as long as the animation looks merely "a bit snappy".
    [[nodiscard]] Core::Status requestTransition(LayerId layer, StateId state);
    // Crossfades with an explicit duration, ignoring authored transitions. For code-driven
    // blends that no state graph describes.
    [[nodiscard]] Core::Status crossfadeTo(LayerId layer, StateId state, Core::Duration duration,
                                           Gameplay::Easing easing = Gameplay::Easing::QuadraticInOut);

    [[nodiscard]] Core::Result<StateId> currentState(LayerId layer) const noexcept;
    [[nodiscard]] Core::Result<bool> isTransitioning(LayerId layer) const noexcept;

    [[nodiscard]] Core::Status setLayerWeight(LayerId layer, float weight);
    [[nodiscard]] Core::Status setLayerMask(LayerId layer, const JointMask& mask);
    [[nodiscard]] Core::Result<float> layerWeight(LayerId layer) const noexcept;

    // Advances every layer's playheads and transition progress. Does not produce a pose:
    // evaluate() does, so a caller may skip evaluation for an off-screen character while
    // keeping its animation clock running.
    [[nodiscard]] Core::Status advance(Core::Duration delta);

    // Composes every layer into one pose. Must follow advance() in the same frame:
    // evaluating first renders one frame behind.
    [[nodiscard]] Core::Status evaluate(Pose3D& outPose);

    // Writes `globalPose * inverseBind` for the last evaluated pose, in the layout Render's
    // skinned palette expects. Fails NotBound before the first successful evaluate().
    [[nodiscard]] Core::Status writeSkinningMatrices(std::span<float> outMatrices) const;

    // The pose produced by the last successful evaluate(). Borrowed, and invalidated by the
    // next evaluate().
    [[nodiscard]] const Pose3D& pose() const noexcept;

    // Root motion accumulated by the last advance(), if root motion is enabled. The root
    // joint has already been neutralised in the evaluated pose, so applying this to the
    // entity moves it exactly once.
    [[nodiscard]] RootMotionDelta3D rootMotion() const noexcept;

    [[nodiscard]] AnimationGraph3DStats stats() const noexcept;

  private:
    struct Impl;

    explicit AnimationGraph3D(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Animation3D
