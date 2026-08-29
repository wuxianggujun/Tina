#include "RenderSurfaceStateTracker.hpp"

#include <tina/render/RenderErrors.hpp>

#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace Tina::Render::Detail {
namespace {

[[nodiscard]] Core::Status invalidSurfaceState(std::string_view message)
{
    return Core::failure(RenderErrorCode::InvalidSurfaceState, message);
}

[[nodiscard]] Core::Status validateStructure(const RenderSurfaceState& state)
{
    if (!state.surface.hasValue() || state.sourceMetricsRevision == 0 || state.surfaceRevision == 0 ||
        state.nativeBindingRevision == 0)
    {
        return invalidSurfaceState("A render surface contains an invalid identity or revision");
    }
    if (!std::isfinite(state.contentScale.x) || !std::isfinite(state.contentScale.y) || state.contentScale.x <= 0.0F ||
        state.contentScale.y <= 0.0F)
    {
        return invalidSurfaceState("A render surface contains an invalid content scale");
    }

    switch (state.availability)
    {
    case RenderSurfaceAvailability::Active:
        if (state.framebufferExtent.width == 0 || state.framebufferExtent.height == 0)
        {
            return invalidSurfaceState("An active render surface must have a non-zero framebuffer extent");
        }
        break;
    case RenderSurfaceAvailability::Suspended:
        break;
    default:
        return invalidSurfaceState("A render surface contains an invalid availability value");
    }

    return Core::success();
}

[[nodiscard]] bool surfaceFactsChanged(const RenderSurfaceState& current, const RenderSurfaceState& previous) noexcept
{
    return current.framebufferExtent != previous.framebufferExtent || current.contentScale != previous.contentScale ||
           current.availability != previous.availability;
}

} // namespace

RenderSurfaceStateTracker::RenderSurfaceStateTracker(const std::optional<RenderSurfaceState>& initialState) noexcept
    : compositionPresent_(initialState.has_value()), committedState_(initialState)
{
}

Core::Result<RenderSurfaceStateTracker>
RenderSurfaceStateTracker::create(const std::optional<RenderSurfaceState>& initialState)
{
    if (initialState.has_value())
    {
        if (auto status = validateStructure(*initialState); !status)
        {
            return Core::failure(std::move(status.error()));
        }
    }
    return RenderSurfaceStateTracker{initialState};
}

Core::Status RenderSurfaceStateTracker::validateAndCommit(const std::optional<RenderSurfaceState>& state)
{
    if (state.has_value() != compositionPresent_)
    {
        return invalidSurfaceState("Render surface composition presence changed after device creation");
    }
    if (!state.has_value())
    {
        return Core::success();
    }
    if (auto status = validateStructure(*state); !status)
    {
        return status;
    }

    const RenderSurfaceState& previous = *committedState_;
    if (state->surface != previous.surface)
    {
        return invalidSurfaceState("Render surface identity changed after device creation");
    }
    if (state->sourceMetricsRevision < previous.sourceMetricsRevision ||
        state->surfaceRevision < previous.surfaceRevision ||
        state->nativeBindingRevision < previous.nativeBindingRevision)
    {
        return invalidSurfaceState("Render surface revisions must not move backward");
    }

    // A replaced native window is always a new backbuffer fact, so it has to arrive
    // with a new surfaceRevision. Without this a backend could observe the rebind and
    // the geometry it must reset to in two different frames.
    const bool nativeBindingChanged = state->nativeBindingRevision != previous.nativeBindingRevision;
    if (nativeBindingChanged && state->surfaceRevision == previous.surfaceRevision)
    {
        return invalidSurfaceState(
            "A render surface native binding change must publish a new surface revision");
    }

    const bool factsChanged = surfaceFactsChanged(*state, previous) || nativeBindingChanged;
    if (factsChanged && state->sourceMetricsRevision == previous.sourceMetricsRevision)
    {
        return invalidSurfaceState("Render surface facts changed without a new source metrics revision");
    }

    const bool canAdvanceSurfaceRevision = previous.surfaceRevision != (std::numeric_limits<u64>::max)();
    const bool surfaceRevisionAdvancedExactlyOnce =
        canAdvanceSurfaceRevision && state->surfaceRevision == previous.surfaceRevision + 1;
    if ((factsChanged && !surfaceRevisionAdvancedExactlyOnce) ||
        (!factsChanged && state->surfaceRevision != previous.surfaceRevision))
    {
        return invalidSurfaceState("Render surface revision must advance exactly once for each committed state change");
    }

    committedState_ = *state;
    // Latched rather than returned so a backend that does not rebind needs no code
    // change, and so a rejected commit above cannot leave a rebind pending.
    nativeBindingChanged_ = nativeBindingChanged;
    return Core::success();
}

} // namespace Tina::Render::Detail
