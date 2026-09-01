#include <tina/animation3d/AnimationGraph3D.hpp>

#include <tina/animation3d/PoseBlend3D.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/math/Quaternion.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <new>
#include <utility>

namespace Tina::Animation3D {

namespace {

struct RootSample final {
    Math::Vec3 translation{};
    Math::Quaternion rotation{0.0F, 0.0F, 0.0F, 1.0F};
};

[[nodiscard]] Core::Result<RootSample> sampleRoot(const ClipSampler3D& clip,
                                                 const Skeleton3D& skeleton, Pose3D& scratch,
                                                 float timeSeconds, Core::u16 rootJoint) noexcept
{
    if (Core::Status status = clip.sample(timeSeconds, skeleton, scratch); !status) {
        return Core::failure(status.error());
    }
    const Scene::LocalTransform& root = scratch.at(rootJoint);
    return RootSample{.translation = root.position, .rotation = root.rotation};
}

[[nodiscard]] RootSample rootDeltaBetween(const RootSample& from,
                                          const RootSample& to) noexcept
{
    return RootSample{
        .translation = to.translation - from.translation,
        .rotation = Math::conjugate(Math::normalized(from.rotation)) *
                    Math::normalized(to.rotation),
    };
}

[[nodiscard]] RootSample composeRootDeltas(const RootSample& first,
                                           const RootSample& second) noexcept
{
    return RootSample{
        .translation = first.translation + second.translation,
        .rotation = Math::normalized(first.rotation * second.rotation),
    };
}

[[nodiscard]] Math::Quaternion quaternionPower(Math::Quaternion base,
                                               Core::u32 exponent) noexcept
{
    Math::Quaternion result{0.0F, 0.0F, 0.0F, 1.0F};
    base = Math::normalized(base);
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = Math::normalized(result * base);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = Math::normalized(base * base);
        }
    }
    return result;
}

[[nodiscard]] RootSample repeatRootDelta(const RootSample& delta, Core::u32 count) noexcept
{
    return RootSample{
        .translation = delta.translation * static_cast<float>(count),
        .rotation = quaternionPower(delta.rotation, count),
    };
}

// Reconstructs the path through clip time instead of subtracting the folded endpoints.
// Loop wraps are discontinuities in sample time but continuous root motion; PingPong bounces
// reverse direction and therefore cancel a forward traversal with the following backward one.
[[nodiscard]] Core::Result<RootSample> accumulateRootMotion(
    AssetFormat::AnimationClip3DPlaybackMode mode, const ClipPlayhead3D& playhead,
    const RootSample& previous, const RootSample& current, const RootSample& atStart,
    const RootSample& atEnd) noexcept
{
    if (!playhead.wrappedThisAdvance || playhead.cyclesCompleted == 0U) {
        return rootDeltaBetween(previous, current);
    }

    const bool startedBackward = playhead.advancedBackwardThisAdvance;
    const Core::u32 crossings = playhead.cyclesCompleted;
    switch (mode) {
    case AssetFormat::AnimationClip3DPlaybackMode::Once:
        return Core::failure(AnimationErrorCode::InvalidConfiguration,
                             "a once playhead reported a boundary wrap");

    case AssetFormat::AnimationClip3DPlaybackMode::Loop: {
        const RootSample& firstBoundary = startedBackward ? atStart : atEnd;
        const RootSample& restartBoundary = startedBackward ? atEnd : atStart;
        RootSample accumulated = rootDeltaBetween(previous, firstBoundary);
        if (crossings > 1U) {
            const RootSample wholeCycle = startedBackward ? rootDeltaBetween(atEnd, atStart)
                                                          : rootDeltaBetween(atStart, atEnd);
            accumulated = composeRootDeltas(
                accumulated, repeatRootDelta(wholeCycle, crossings - 1U));
        }
        return composeRootDeltas(accumulated, rootDeltaBetween(restartBoundary, current));
    }

    case AssetFormat::AnimationClip3DPlaybackMode::PingPong: {
        const RootSample& firstBoundary = startedBackward ? atStart : atEnd;
        RootSample accumulated = rootDeltaBetween(previous, firstBoundary);

        // Full traversals between the first and last bounce alternate direction. Adjacent
        // forward/backward pairs cancel, so only one traversal remains when the count is odd.
        const Core::u32 fullTraversals = crossings - 1U;
        if ((fullTraversals & 1U) != 0U) {
            const RootSample traversalAfterFirstBounce =
                startedBackward ? rootDeltaBetween(atStart, atEnd)
                                : rootDeltaBetween(atEnd, atStart);
            accumulated = composeRootDeltas(accumulated, traversalAfterFirstBounce);
        }

        const bool finishesBackward =
            (crossings & 1U) == 0U ? startedBackward : !startedBackward;
        const RootSample& lastBoundary = finishesBackward ? atEnd : atStart;
        return composeRootDeltas(accumulated, rootDeltaBetween(lastBoundary, current));
    }
    }

    return Core::failure(AnimationErrorCode::InvalidConfiguration,
                         "root motion clip has an unknown playback mode");
}

} // namespace

struct AnimationGraph3D::Impl final {
    struct State final {
        StateSourceKind kind = StateSourceKind::Clip;
        Core::u16 layerIndex = 0;
        Core::u16 clipIndex = 0;
        Core::u16 blendTreeIndex = 0;
        float speed = 1.0F;
        bool restartOnEnter = true;
        ClipPlayhead3D playhead{};
    };

    struct Transition final {
        Core::u16 fromIndex = 0;
        Core::u16 toIndex = 0;
        Core::Duration duration{};
        Gameplay::Easing easing = Gameplay::Easing::QuadraticInOut;
        bool canInterrupt = true;
    };

    struct Layer final {
        LayerBlendMode mode = LayerBlendMode::Override;
        float weight = 1.0F;
        JointMask mask{};
        Core::u16 referenceClipIndex = BlendTreeNodeNone;
        // Slot in referencePoses for an additive layer.
        Core::u16 referencePoseIndex = 0;

        std::pmr::vector<Core::u16> stateIndices;
        std::pmr::vector<Core::u16> transitionIndices;

        // Index into the graph's state array, or none when the layer has no state yet.
        Core::u16 currentState = BlendTreeNodeNone;
        Core::u16 previousState = BlendTreeNodeNone;
        Core::Duration transitionElapsed{};
        Core::Duration transitionDuration{};
        Gameplay::Easing transitionEasing = Gameplay::Easing::QuadraticInOut;
        bool transitioning = false;
    };

    Impl(const Skeleton3D& skeletonRef, std::pmr::memory_resource& resourceRef,
         AnimationGraph3DConfig configValue)
        : skeleton(&skeletonRef), resource(&resourceRef), config(configValue),
          clips(std::pmr::polymorphic_allocator<const ClipSampler3D*>{&resourceRef}),
          blendTrees(std::pmr::polymorphic_allocator<BlendTree3D*>{&resourceRef}),
          states(std::pmr::polymorphic_allocator<State>{&resourceRef}),
          transitions(std::pmr::polymorphic_allocator<Transition>{&resourceRef}),
          layers(std::pmr::polymorphic_allocator<Layer>{&resourceRef}),
          referencePoses(std::pmr::polymorphic_allocator<Pose3D>{&resourceRef}),
          ownerToken(nextOwnerToken())
    {
    }

    // Monotonic and never reused, so a handle outliving its graph cannot match a later one.
    // Relaxed ordering suffices: a graph is single-owner, and this only has to be unique
    // across graphs, not ordered against other work.
    [[nodiscard]] static Core::u32 nextOwnerToken() noexcept
    {
        static std::atomic<Core::u32> counter{0};
        return counter.fetch_add(1U, std::memory_order_relaxed) + 1U;
    }

    const Skeleton3D* skeleton = nullptr;
    std::pmr::memory_resource* resource = nullptr;
    AnimationGraph3DConfig config{};

    std::pmr::vector<const ClipSampler3D*> clips;
    std::pmr::vector<BlendTree3D*> blendTrees;
    std::pmr::vector<State> states;
    std::pmr::vector<Transition> transitions;
    std::pmr::vector<Layer> layers;
    std::pmr::vector<Pose3D> referencePoses;
    // Distinguishes this graph's handles from another's. Monotonic and never reused, so a
    // handle held past a graph's destruction cannot match a later graph.
    Core::u32 ownerToken = 0;

    // Scratch, all allocated at Create so no frame allocates.
    Pose3D basePose{};
    Pose3D layerPose{};
    Pose3D sourcePose{};
    Pose3D destinationPose{};
    Pose3D rootScratch{};
    Pose3D evaluatedPose{};

    RootMotionDelta3D rootMotion{};
    AnimationGraph3DStats stats{};
    bool evaluatedOnce = false;

    // The owner check is what stops a handle from one graph addressing another's slot.
    // States and layers are never erased, so an index is stable for the graph's life and
    // needs no per-slot generation.
    [[nodiscard]] bool layerValid(LayerId id) const noexcept
    {
        return id.hasValue() && id.owner() == ownerToken && id.index() < layers.size();
    }
    [[nodiscard]] bool stateValid(StateId id) const noexcept
    {
        return id.hasValue() && id.owner() == ownerToken && id.index() < states.size();
    }
    [[nodiscard]] bool stateBelongsToLayer(StateId state, LayerId layer) const noexcept
    {
        return stateValid(state) && layerValid(layer) &&
               states[state.index()].layerIndex == layer.index();
    }
};

AnimationGraph3D::AnimationGraph3D(Impl* impl) noexcept : m_impl(impl) {}

AnimationGraph3D::~AnimationGraph3D() noexcept
{
    delete m_impl;
    m_impl = nullptr;
}

AnimationGraph3D::AnimationGraph3D(AnimationGraph3D&& other) noexcept
    : m_impl(std::exchange(other.m_impl, nullptr))
{
}

AnimationGraph3D& AnimationGraph3D::operator=(AnimationGraph3D&& other) noexcept
{
    if (this != &other) {
        delete m_impl;
        m_impl = std::exchange(other.m_impl, nullptr);
    }
    return *this;
}

Core::Result<AnimationGraph3D> AnimationGraph3D::Create(
    const Skeleton3D& skeleton, std::span<const ClipSampler3D* const> clips,
    std::span<BlendTree3D* const> blendTrees, AnimationGraph3DConfig config)
{
    if (config.stateCapacity == 0U || config.stateCapacity > MaximumStateCount) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration,
                             "graph state capacity must be between 1 and the state bound");
    }
    if (config.layerCapacity == 0U || config.layerCapacity > MaximumLayerCount) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration,
                             "graph layer capacity must be between 1 and the layer bound");
    }
    if (config.transitionCapacity > MaximumTransitionCount) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration,
                             "graph transition capacity exceeds the bound");
    }
    if (config.rootMotion.enabled && config.rootMotion.rootJoint >= skeleton.jointCount()) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration,
                             "root motion joint is outside the skeleton");
    }
    for (const ClipSampler3D* const clip : clips) {
        if (clip == nullptr || clip->jointCount() != skeleton.jointCount()) {
            return Core::failure(AnimationErrorCode::SkeletonMismatch,
                                 "graph clip is null or does not match the skeleton");
        }
    }
    for (Core::usize index = 0; index < blendTrees.size(); ++index) {
        BlendTree3D* const tree = blendTrees[index];
        if (tree == nullptr) {
            return Core::failure(AnimationErrorCode::InvalidArgument,
                                 "graph blend tree list contains a null tree");
        }
        for (Core::usize previous = 0; previous < index; ++previous) {
            if (blendTrees[previous] == tree) {
                return Core::failure(
                    AnimationErrorCode::InvalidArgument,
                    "graph blend tree list contains the same mutable tree instance twice");
            }
        }
    }

    std::pmr::memory_resource& resource = config.memoryResource != nullptr
        ? *config.memoryResource
        : *std::pmr::get_default_resource();

    try {
        auto impl = std::make_unique<Impl>(skeleton, resource, config);
        impl->clips.assign(clips.begin(), clips.end());
        impl->blendTrees.assign(blendTrees.begin(), blendTrees.end());
        impl->states.reserve(config.stateCapacity);
        impl->transitions.reserve(config.transitionCapacity);
        impl->layers.reserve(config.layerCapacity);
        impl->referencePoses.reserve(config.layerCapacity);

        // Every scratch pose is allocated here, so no frame allocates.
        const auto makePose = [&]() -> Core::Result<Pose3D> {
            return Pose3D::Create(skeleton.jointCount(), resource);
        };
        auto base = makePose();
        auto layer = makePose();
        auto source = makePose();
        auto destination = makePose();
        auto rootScratch = makePose();
        auto evaluated = makePose();
        if (!base || !layer || !source || !destination || !rootScratch || !evaluated) {
            return Core::failure(AnimationErrorCode::AllocationFailed,
                                 "graph scratch pose allocation failed");
        }
        impl->basePose = std::move(*base);
        impl->layerPose = std::move(*layer);
        impl->sourcePose = std::move(*source);
        impl->destinationPose = std::move(*destination);
        impl->rootScratch = std::move(*rootScratch);
        impl->evaluatedPose = std::move(*evaluated);

        // Layer 0 exists from the start and always covers every joint: a masked base layer
        // would leave unmasked joints holding whatever the buffer last contained.
        Impl::Layer baseLayer{
            .mode = LayerBlendMode::Override,
            .weight = 1.0F,
            .mask = JointMask{},
            .stateIndices = std::pmr::vector<Core::u16>{
                std::pmr::polymorphic_allocator<Core::u16>{&resource}},
            .transitionIndices = std::pmr::vector<Core::u16>{
                std::pmr::polymorphic_allocator<Core::u16>{&resource}},
        };
        baseLayer.stateIndices.reserve(config.stateCapacity);
        baseLayer.transitionIndices.reserve(config.transitionCapacity);
        impl->layers.push_back(std::move(baseLayer));

        return AnimationGraph3D(impl.release());
    } catch (const std::bad_alloc&) {
        return Core::failure(AnimationErrorCode::AllocationFailed, "graph allocation failed");
    }
}

LayerId AnimationGraph3D::baseLayer() const noexcept
{
    if (m_impl == nullptr || m_impl->layers.empty()) {
        return LayerId{};
    }
    return LayerId{m_impl->ownerToken, 0U};
}

Core::Result<LayerId> AnimationGraph3D::addLayer(const LayerDesc& desc)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (m_impl->layers.size() >= m_impl->config.layerCapacity) {
        return Core::failure(AnimationErrorCode::CapacityExceeded,
                             "graph layer capacity is exhausted");
    }
    if (!std::isfinite(desc.weight) || desc.weight < 0.0F || desc.weight > 1.0F) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "layer weight must be finite and within [0,1]");
    }
    if (desc.mode != LayerBlendMode::Override && desc.mode != LayerBlendMode::Additive) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "layer blend mode is not a known value");
    }
    if (desc.mode == LayerBlendMode::Override &&
        desc.referenceClipIndex != BlendTreeNodeNone) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "override layer cannot declare an additive reference clip");
    }
    if (desc.mode == LayerBlendMode::Additive && desc.referenceClipIndex != BlendTreeNodeNone &&
        desc.referenceClipIndex >= m_impl->clips.size()) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "additive layer references an unknown reference clip");
    }

    try {
        Impl::Layer layer{
            .mode = desc.mode,
            .weight = desc.weight,
            .mask = desc.mask,
            .referenceClipIndex = desc.referenceClipIndex,
            .stateIndices = std::pmr::vector<Core::u16>{
                std::pmr::polymorphic_allocator<Core::u16>{m_impl->resource}},
            .transitionIndices = std::pmr::vector<Core::u16>{
                std::pmr::polymorphic_allocator<Core::u16>{m_impl->resource}},
        };
        // Reserve before publishing the layer. Later addState/addTransition operations can
        // then update their global table and this ownership index without a second
        // allocation that could fail between the two writes.
        layer.stateIndices.reserve(m_impl->config.stateCapacity);
        layer.transitionIndices.reserve(m_impl->config.transitionCapacity);

        if (desc.mode == LayerBlendMode::Additive) {
            // Resolved once, for the same reason the blend tree resolves its own: the
            // reference is a bind pose or a clip's first frame and neither changes.
            auto reference = Pose3D::Create(m_impl->skeleton->jointCount(), *m_impl->resource);
            if (!reference) {
                return Core::failure(reference.error());
            }
            if (desc.referenceClipIndex == BlendTreeNodeNone) {
                if (Core::Status status = m_impl->skeleton->writeBindPose(*reference); !status) {
                    return Core::failure(status.error());
                }
            } else if (Core::Status status = m_impl->clips[desc.referenceClipIndex]->sample(
                           0.0F, *m_impl->skeleton, *reference);
                       !status) {
                return Core::failure(status.error());
            }
            layer.referencePoseIndex = static_cast<Core::u16>(m_impl->referencePoses.size());
            m_impl->referencePoses.push_back(std::move(*reference));
        }

        const auto index = static_cast<Core::u32>(m_impl->layers.size());
        m_impl->layers.push_back(std::move(layer));
        return LayerId{m_impl->ownerToken, static_cast<Core::u16>(index)};
    } catch (const std::bad_alloc&) {
        return Core::failure(AnimationErrorCode::AllocationFailed, "layer allocation failed");
    }
}

Core::Result<StateId> AnimationGraph3D::addState(LayerId layer, const StateDesc& desc)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (!m_impl->layerValid(layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer handle is unknown");
    }
    if (m_impl->states.size() >= m_impl->config.stateCapacity) {
        return Core::failure(AnimationErrorCode::CapacityExceeded,
                             "graph state capacity is exhausted");
    }
    if (!std::isfinite(desc.speed)) {
        return Core::failure(AnimationErrorCode::InvalidArgument, "state speed must be finite");
    }
    if (desc.kind != StateSourceKind::Clip && desc.kind != StateSourceKind::BlendTree) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "state source kind is not a known value");
    }
    if (desc.kind == StateSourceKind::Clip && desc.clipIndex >= m_impl->clips.size()) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "state references an unknown clip");
    }
    if (desc.kind == StateSourceKind::BlendTree && desc.blendTreeIndex >= m_impl->blendTrees.size()) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "state references an unknown blend tree");
    }
    if (desc.kind == StateSourceKind::BlendTree) {
        for (const Impl::State& existing : m_impl->states) {
            if (existing.kind == StateSourceKind::BlendTree &&
                existing.blendTreeIndex == desc.blendTreeIndex) {
                return Core::failure(
                    AnimationErrorCode::InvalidArgument,
                    "a mutable blend tree instance can belong to only one graph state");
            }
        }
    }

    try {
        Impl::State state{
            .kind = desc.kind,
            .layerIndex = layer.index(),
            .clipIndex = desc.clipIndex,
            .blendTreeIndex = desc.blendTreeIndex,
            .speed = desc.speed,
            .restartOnEnter = desc.restartOnEnter,
        };
        if (desc.kind == StateSourceKind::Clip) {
            state.playhead = m_impl->clips[desc.clipIndex]->startPlayhead();
        }
        const auto index = static_cast<Core::u32>(m_impl->states.size());
        m_impl->states.push_back(state);
        m_impl->layers[layer.index()].stateIndices.push_back(static_cast<Core::u16>(index));
        return StateId{m_impl->ownerToken, static_cast<Core::u16>(index)};
    } catch (const std::bad_alloc&) {
        return Core::failure(AnimationErrorCode::AllocationFailed, "state allocation failed");
    }
}

Core::Status AnimationGraph3D::addTransition(LayerId layer, const TransitionDesc& desc)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (!m_impl->layerValid(layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer handle is unknown");
    }
    if (!m_impl->stateValid(desc.from) || !m_impl->stateValid(desc.to)) {
        return Core::failure(AnimationErrorCode::InvalidHandle,
                             "transition references an unknown state");
    }
    if (!m_impl->stateBelongsToLayer(desc.from, layer) ||
        !m_impl->stateBelongsToLayer(desc.to, layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle,
                             "transition state does not belong to the requested layer");
    }
    if (desc.from == desc.to) {
        return Core::failure(AnimationErrorCode::InvalidTransition,
                             "a transition cannot connect a state to itself");
    }
    if (m_impl->transitions.size() >= m_impl->config.transitionCapacity) {
        return Core::failure(AnimationErrorCode::CapacityExceeded,
                             "graph transition capacity is exhausted");
    }
    if (!std::isfinite(desc.duration.count()) || desc.duration.count() < 0.0) {
        return Core::failure(AnimationErrorCode::InvalidTransition,
                             "transition duration must be finite and non-negative");
    }
    if (!Gameplay::isValidEasing(desc.easing)) {
        return Core::failure(AnimationErrorCode::InvalidTransition,
                             "transition easing is not a known curve");
    }

    try {
        const auto index = static_cast<Core::u16>(m_impl->transitions.size());
        m_impl->transitions.push_back(Impl::Transition{
            .fromIndex = static_cast<Core::u16>(desc.from.index()),
            .toIndex = static_cast<Core::u16>(desc.to.index()),
            .duration = desc.duration,
            .easing = desc.easing,
            .canInterrupt = desc.canInterrupt,
        });
        m_impl->layers[layer.index()].transitionIndices.push_back(index);
        return Core::success();
    } catch (const std::bad_alloc&) {
        return Core::failure(AnimationErrorCode::AllocationFailed, "transition allocation failed");
    }
}

Core::Status AnimationGraph3D::setState(LayerId layer, StateId state)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (!m_impl->layerValid(layer) || !m_impl->stateValid(state)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer or state handle is unknown");
    }
    if (!m_impl->stateBelongsToLayer(state, layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle,
                             "state does not belong to the requested layer");
    }
    Impl::Layer& target = m_impl->layers[layer.index()];
    const auto stateIndex = static_cast<Core::u16>(state.index());
    target.currentState = stateIndex;
    target.previousState = BlendTreeNodeNone;
    target.transitioning = false;
    target.transitionElapsed = Core::Duration{0.0};

    Impl::State& entered = m_impl->states[stateIndex];
    if (entered.restartOnEnter) {
        if (entered.kind == StateSourceKind::Clip) {
            entered.playhead = m_impl->clips[entered.clipIndex]->startPlayhead();
        } else {
            m_impl->blendTrees[entered.blendTreeIndex]->restart();
        }
    }
    return Core::success();
}

Core::Status AnimationGraph3D::crossfadeTo(LayerId layer, StateId state, Core::Duration duration,
                                           Gameplay::Easing easing)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (!m_impl->layerValid(layer) || !m_impl->stateValid(state)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer or state handle is unknown");
    }
    if (!m_impl->stateBelongsToLayer(state, layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle,
                             "state does not belong to the requested layer");
    }
    if (!std::isfinite(duration.count()) || duration.count() < 0.0) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "crossfade duration must be finite and non-negative");
    }
    if (!Gameplay::isValidEasing(easing)) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "crossfade easing is not a known curve");
    }

    Impl::Layer& target = m_impl->layers[layer.index()];
    const auto stateIndex = static_cast<Core::u16>(state.index());
    // No current state, or a zero duration: a hard cut. Blending from nothing would blend
    // from whatever the pose buffer held.
    if (target.currentState == BlendTreeNodeNone || duration.count() <= 0.0) {
        return setState(layer, state);
    }
    if (target.currentState == stateIndex) {
        return Core::success();
    }

    target.previousState = target.currentState;
    target.currentState = stateIndex;
    target.transitioning = true;
    target.transitionElapsed = Core::Duration{0.0};
    target.transitionDuration = duration;
    target.transitionEasing = easing;
    ++m_impl->stats.transitionsStarted;

    Impl::State& entered = m_impl->states[stateIndex];
    if (entered.restartOnEnter) {
        if (entered.kind == StateSourceKind::Clip) {
            entered.playhead = m_impl->clips[entered.clipIndex]->startPlayhead();
        } else {
            m_impl->blendTrees[entered.blendTreeIndex]->restart();
        }
    }
    return Core::success();
}

Core::Status AnimationGraph3D::requestTransition(LayerId layer, StateId state)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (!m_impl->layerValid(layer) || !m_impl->stateValid(state)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer or state handle is unknown");
    }
    if (!m_impl->stateBelongsToLayer(state, layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle,
                             "state does not belong to the requested layer");
    }
    Impl::Layer& target = m_impl->layers[layer.index()];
    if (target.currentState == BlendTreeNodeNone) {
        return setState(layer, state);
    }

    const auto stateIndex = static_cast<Core::u16>(state.index());
    for (const Core::u16 transitionIndex : target.transitionIndices) {
        const Impl::Transition& transition = m_impl->transitions[transitionIndex];
        if (transition.fromIndex != target.currentState || transition.toIndex != stateIndex) {
            continue;
        }
        // A transition already in flight is only replaced when the new one says it may
        // interrupt. Dropping it silently would make input feel ignored; stacking would
        // accumulate poses.
        if (target.transitioning && !transition.canInterrupt) {
            ++m_impl->stats.transitionsRefused;
            return Core::failure(AnimationErrorCode::InvalidTransition,
                                 "a transition is already in flight and this one cannot interrupt");
        }
        return crossfadeTo(layer, state, transition.duration, transition.easing);
    }

    // No authored transition. Refused rather than defaulted: a missing transition is an
    // authoring gap, and a silent default duration hides it for as long as the result merely
    // looks a bit snappy.
    return Core::failure(AnimationErrorCode::InvalidTransition,
                         "no authored transition connects the current state to the requested one");
}

Core::Result<StateId> AnimationGraph3D::currentState(LayerId layer) const noexcept
{
    if (m_impl == nullptr || !m_impl->layerValid(layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer handle is unknown");
    }
    const Impl::Layer& target = m_impl->layers[layer.index()];
    if (target.currentState == BlendTreeNodeNone) {
        return Core::failure(AnimationErrorCode::NotBound, "layer has no current state");
    }
    return StateId{m_impl->ownerToken, target.currentState};
}

Core::Result<bool> AnimationGraph3D::isTransitioning(LayerId layer) const noexcept
{
    if (m_impl == nullptr || !m_impl->layerValid(layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer handle is unknown");
    }
    return m_impl->layers[layer.index()].transitioning;
}

Core::Status AnimationGraph3D::setLayerWeight(LayerId layer, float weight)
{
    if (m_impl == nullptr || !m_impl->layerValid(layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer handle is unknown");
    }
    if (!std::isfinite(weight) || weight < 0.0F || weight > 1.0F) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "layer weight must be finite and within [0,1]");
    }
    m_impl->layers[layer.index()].weight = weight;
    return Core::success();
}

Core::Status AnimationGraph3D::setLayerMask(LayerId layer, const JointMask& mask)
{
    if (m_impl == nullptr || !m_impl->layerValid(layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer handle is unknown");
    }
    // The base layer keeps covering every joint: masking it would leave the excluded joints
    // holding whatever the pose buffer last contained.
    if (layer.index() == 0U) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "the base layer always animates every joint and cannot be masked");
    }
    m_impl->layers[layer.index()].mask = mask;
    return Core::success();
}

Core::Result<float> AnimationGraph3D::layerWeight(LayerId layer) const noexcept
{
    if (m_impl == nullptr || !m_impl->layerValid(layer)) {
        return Core::failure(AnimationErrorCode::InvalidHandle, "layer handle is unknown");
    }
    return m_impl->layers[layer.index()].weight;
}

Core::Status AnimationGraph3D::advance(Core::Duration delta)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (!std::isfinite(delta.count()) || delta.count() < 0.0) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "advance delta must be finite and non-negative");
    }

    Impl& impl = *m_impl;
    ++impl.stats.advanceCount;
    impl.rootMotion = RootMotionDelta3D{};

    for (Impl::Layer& layer : impl.layers) {
        if (layer.currentState == BlendTreeNodeNone) {
            continue;
        }

        // Both sides of a transition advance. A fading-out state that stopped advancing
        // would freeze mid-stride and the blend would visibly stutter.
        const auto advanceState = [&](Core::u16 stateIndex) -> Core::Status {
            Impl::State& state = impl.states[stateIndex];
            switch (state.kind) {
            case StateSourceKind::Clip: {
                const ClipSampler3D* const clip = impl.clips[state.clipIndex];
                auto advanced = clip->advance(state.playhead, delta, state.speed);
                if (!advanced) {
                    return Core::failure(advanced.error());
                }
                state.playhead = *advanced;
                return Core::success();
            }
            case StateSourceKind::BlendTree:
                return impl.blendTrees[state.blendTreeIndex]->advance(delta, state.speed);
            }
            return Core::failure(AnimationErrorCode::InvalidConfiguration,
                                 "graph contains an unknown state source kind");
        };

        if (Core::Status status = advanceState(layer.currentState); !status) {
            return status;
        }
        if (layer.transitioning && layer.previousState != BlendTreeNodeNone) {
            if (Core::Status status = advanceState(layer.previousState); !status) {
                return status;
            }
            layer.transitionElapsed += delta;
            if (layer.transitionElapsed >= layer.transitionDuration) {
                layer.transitioning = false;
                layer.previousState = BlendTreeNodeNone;
                layer.transitionElapsed = layer.transitionDuration;
            }
        }
    }

    // Root motion comes from the base layer's current state only. Taking it from a masked
    // upper-body layer would move the character when an aim pose changed, and summing across
    // layers would move it once per layer.
    if (impl.config.rootMotion.enabled && !impl.layers.empty() &&
        impl.layers[0].currentState != BlendTreeNodeNone) {
        const Impl::State& state = impl.states[impl.layers[0].currentState];
        if (state.kind == StateSourceKind::Clip) {
            const ClipSampler3D* const clip = impl.clips[state.clipIndex];
            const Core::u16 rootJoint = impl.config.rootMotion.rootJoint;
            const ClipPlayhead3D& playhead = state.playhead;

            auto previous = sampleRoot(*clip, *impl.skeleton, impl.rootScratch,
                                       playhead.previousTimeSeconds, rootJoint);
            auto current = sampleRoot(*clip, *impl.skeleton, impl.rootScratch,
                                      playhead.timeSeconds, rootJoint);
            if (!previous || !current) {
                ++impl.stats.evaluationFailures;
                return Core::failure(AnimationErrorCode::EvaluationFailed,
                                     "root motion sampling failed");
            }

            RootSample accumulated = rootDeltaBetween(*previous, *current);

            if (playhead.wrappedThisAdvance) {
                auto atEnd = sampleRoot(*clip, *impl.skeleton, impl.rootScratch,
                                        clip->durationSeconds(), rootJoint);
                auto atStart =
                    sampleRoot(*clip, *impl.skeleton, impl.rootScratch, 0.0F, rootJoint);
                if (!atEnd || !atStart) {
                    ++impl.stats.evaluationFailures;
                    return Core::failure(AnimationErrorCode::EvaluationFailed,
                                         "root motion wrap sampling failed");
                }
                auto rootMotion = accumulateRootMotion(clip->playbackMode(), playhead, *previous,
                                                       *current, *atStart, *atEnd);
                if (!rootMotion) {
                    ++impl.stats.evaluationFailures;
                    return Core::failure(rootMotion.error());
                }
                accumulated = *rootMotion;
            }

            if (!Math::isFinite(accumulated.translation) ||
                !Math::isFinite(accumulated.rotation)) {
                ++impl.stats.evaluationFailures;
                return Core::failure(AnimationErrorCode::EvaluationFailed,
                                     "root motion accumulation is not finite");
            }

            if (!impl.config.rootMotion.applyTranslationXZ) {
                accumulated.translation.x = 0.0F;
                accumulated.translation.z = 0.0F;
            }
            if (!impl.config.rootMotion.applyTranslationY) {
                accumulated.translation.y = 0.0F;
            }
            if (!impl.config.rootMotion.applyRotation) {
                accumulated.rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F};
            }
            impl.rootMotion = RootMotionDelta3D{
                .translation = accumulated.translation,
                .rotation = Math::normalized(accumulated.rotation),
                .wrapped = playhead.wrappedThisAdvance,
            };
        }
    }

    return Core::success();
}

Core::Status AnimationGraph3D::evaluate(Pose3D& outPose)
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    Impl& impl = *m_impl;
    if (outPose.jointCount() != impl.skeleton->jointCount()) {
        return Core::failure(AnimationErrorCode::SkeletonMismatch,
                             "output pose does not match the skeleton");
    }
    if (impl.layers.empty() || impl.layers[0].currentState == BlendTreeNodeNone) {
        return Core::failure(AnimationErrorCode::NotBound,
                             "the base layer has no current state to evaluate");
    }

    // Evaluates one state into `target`, blending both sides when a transition is in flight.
    const auto evaluateLayerPose = [&](const Impl::Layer& layer, Pose3D& target) -> Core::Status {
        const auto evaluateState = [&](Core::u16 stateIndex, Pose3D& into) -> Core::Status {
            const Impl::State& state = impl.states[stateIndex];
            switch (state.kind) {
            case StateSourceKind::Clip:
                return impl.clips[state.clipIndex]->sample(state.playhead.timeSeconds,
                                                           *impl.skeleton, into);
            case StateSourceKind::BlendTree:
                return impl.blendTrees[state.blendTreeIndex]->evaluate(*impl.skeleton, into);
            }
            return Core::failure(AnimationErrorCode::InvalidConfiguration,
                                 "graph contains an unknown state source kind");
        };

        if (!layer.transitioning || layer.previousState == BlendTreeNodeNone) {
            return evaluateState(layer.currentState, target);
        }
        if (Core::Status status = evaluateState(layer.previousState, impl.sourcePose); !status) {
            return status;
        }
        if (Core::Status status = evaluateState(layer.currentState, impl.destinationPose); !status) {
            return status;
        }
        const float linear = layer.transitionDuration.count() > 0.0
            ? static_cast<float>(layer.transitionElapsed.count() / layer.transitionDuration.count())
            : 1.0F;
        blendPair(target, impl.sourcePose, impl.destinationPose,
                  Gameplay::evaluateEasing(layer.transitionEasing, linear));
        return Core::success();
    };

    if (Core::Status status = evaluateLayerPose(impl.layers[0], impl.basePose); !status) {
        ++impl.stats.evaluationFailures;
        return status;
    }

    for (Core::usize index = 1; index < impl.layers.size(); ++index) {
        const Impl::Layer& layer = impl.layers[index];
        if (layer.currentState == BlendTreeNodeNone || layer.weight <= 0.0F) {
            continue;
        }
        if (Core::Status status = evaluateLayerPose(layer, impl.layerPose); !status) {
            ++impl.stats.evaluationFailures;
            return status;
        }
        switch (layer.mode) {
        case LayerBlendMode::Override:
            blendOverwrite(impl.basePose, impl.layerPose, layer.weight, layer.mask);
            break;
        case LayerBlendMode::Additive:
            blendAdditive(impl.basePose, impl.layerPose,
                          impl.referencePoses[layer.referencePoseIndex], layer.weight, layer.mask);
            break;
        default:
            ++impl.stats.evaluationFailures;
            return Core::failure(AnimationErrorCode::InvalidConfiguration,
                                 "graph contains an unknown layer blend mode");
        }
    }

    // Repeated slerp leaves length drift behind, and next frame's blend compounds it. One
    // normalization at the root is cheaper than making every operator promise unit output.
    normalizeRotations(impl.basePose);

    // Root motion is removed from the pose because the caller applies it to the entity.
    // Leaving it in and also reporting it moves the character twice -- the classic
    // root-motion defect -- and the doubling looks like a tuning problem, not a bug.
    if (impl.config.rootMotion.enabled) {
        const Core::u16 rootJoint = impl.config.rootMotion.rootJoint;
        const Scene::LocalTransform& bind = impl.skeleton->bindPose(rootJoint);
        Scene::LocalTransform& root = impl.basePose.at(rootJoint);
        if (impl.config.rootMotion.applyTranslationXZ) {
            root.position.x = bind.position.x;
            root.position.z = bind.position.z;
        }
        if (impl.config.rootMotion.applyTranslationY) {
            root.position.y = bind.position.y;
        }
        if (impl.config.rootMotion.applyRotation) {
            root.rotation = bind.rotation;
        }
    }

    if (!isPoseFinite(impl.basePose)) {
        ++impl.stats.evaluationFailures;
        return Core::failure(AnimationErrorCode::EvaluationFailed,
                             "composed pose contains a non-finite transform");
    }

    impl.evaluatedPose.copyFrom(impl.basePose);
    outPose.copyFrom(impl.basePose);
    impl.evaluatedOnce = true;
    return Core::success();
}

Core::Status AnimationGraph3D::writeSkinningMatrices(std::span<float> outMatrices) const
{
    if (m_impl == nullptr) {
        return Core::failure(AnimationErrorCode::InvalidConfiguration, "graph was not created");
    }
    if (!m_impl->evaluatedOnce) {
        return Core::failure(AnimationErrorCode::NotBound,
                             "no pose has been evaluated yet");
    }
    return m_impl->skeleton->composeSkinningMatrices(m_impl->evaluatedPose, outMatrices);
}

const Pose3D& AnimationGraph3D::pose() const noexcept
{
    return m_impl->evaluatedPose;
}

RootMotionDelta3D AnimationGraph3D::rootMotion() const noexcept
{
    return m_impl != nullptr ? m_impl->rootMotion : RootMotionDelta3D{};
}

AnimationGraph3DStats AnimationGraph3D::stats() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    AnimationGraph3DStats snapshot = m_impl->stats;
    snapshot.stateCount = static_cast<Core::u16>(m_impl->states.size());
    snapshot.transitionCount = static_cast<Core::u16>(m_impl->transitions.size());
    snapshot.layerCount = static_cast<Core::u16>(m_impl->layers.size());
    snapshot.transitionInFlight = false;
    for (const Impl::Layer& layer : m_impl->layers) {
        if (layer.transitioning) {
            snapshot.transitionInFlight = true;
            break;
        }
    }
    return snapshot;
}

} // namespace Tina::Animation3D
