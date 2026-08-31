#include "WindowSurfaceLeaseAccess.hpp"

#include <tina/core/error/Error.hpp>

#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Integration {

NativeWindowSurfaceLease::NativeWindowSurfaceLease(
    std::unique_ptr<Detail::NativeWindowSurfaceLeaseState> state) noexcept
    : m_state(std::move(state))
{
}

NativeWindowSurfaceLease::~NativeWindowSurfaceLease() noexcept = default;
NativeWindowSurfaceLease::NativeWindowSurfaceLease(NativeWindowSurfaceLease&&) noexcept = default;
NativeWindowSurfaceLease& NativeWindowSurfaceLease::operator=(NativeWindowSurfaceLease&&) noexcept = default;

WindowSurfaceId NativeWindowSurfaceLease::surface() const noexcept
{
    return m_state != nullptr ? m_state->surface : WindowSurfaceId{};
}

bool NativeWindowSurfaceLease::hasValue() const noexcept
{
    return m_state != nullptr && m_state->surface.hasValue();
}

NativeWindowSurfaceLease::operator bool() const noexcept
{
    return hasValue();
}

namespace Detail {

NativeWindowSurfaceLeaseState::~NativeWindowSurfaceLeaseState() noexcept
{
    if (control == nullptr || std::this_thread::get_id() != control->ownerThread || control->activeLeaseCount == 0)
    {
        std::terminate();
    }
    --control->activeLeaseCount;
}

Core::Result<NativeWindowSurfaceLease>
NativeWindowSurfaceLeaseAccess::Create(std::shared_ptr<NativeWindowSurfaceLeaseControl> control,
                                       WindowSurfaceId surface, NativeWindowBinding binding) noexcept
{
    if (control == nullptr || !surface.hasValue() || binding.nativeWindow == 0 || binding.bindingRevision == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A native window surface lease requires a live identity and binding");
    }
    if (std::this_thread::get_id() != control->ownerThread)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A native window surface lease must be acquired on its owner thread");
    }
    if (!control->surfaceAlive)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The native window surface is no longer alive");
    }
    if (control->activeLeaseCount != 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The native window surface already has an active lifetime lease");
    }

    try
    {
        auto state = std::make_unique<NativeWindowSurfaceLeaseState>();
        state->control = std::move(control);
        state->surface = surface;
        state->control->surface = surface;
        state->control->binding = binding;
        ++state->control->activeLeaseCount;
        return NativeWindowSurfaceLease{std::move(state)};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "The native window surface lease allocation failed");
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "The native window surface lease allocation threw unexpectedly");
    }
}

Core::Status NativeWindowSurfaceLeaseAccess::rebind(
    const std::shared_ptr<NativeWindowSurfaceLeaseControl>& control, WindowSurfaceId surface,
    NativeWindowBinding binding) noexcept
{
    if (control == nullptr || !surface.hasValue() || binding.nativeWindow == 0 || binding.bindingRevision == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A native window surface rebind requires a live identity and binding");
    }
    if (std::this_thread::get_id() != control->ownerThread)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A native window surface rebind must run on its owner thread");
    }
    if (!control->surfaceAlive || control->surface != surface || control->binding.nativeWindow == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A native window surface rebind requires a live published surface");
    }
    if (control->binding.bindingRevision == (std::numeric_limits<u64>::max)() ||
        binding.bindingRevision != control->binding.bindingRevision + 1)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A native window surface binding revision must advance exactly once");
    }
    if (binding.kind != control->binding.kind || binding.nativeDisplay != control->binding.nativeDisplay)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A native window surface rebind may only replace the native window");
    }

    control->binding = binding;
    return Core::success();
}

Core::Result<NativeWindowBinding> NativeWindowSurfaceLeaseAccess::decode(const NativeWindowSurfaceLease& lease) noexcept
{
    if (lease.m_state == nullptr || lease.m_state->control == nullptr || !lease.m_state->control->surfaceAlive)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The native window surface lease is not live");
    }
    if (std::this_thread::get_id() != lease.m_state->control->ownerThread)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The native window surface lease must be decoded on its owner thread");
    }
    if (lease.m_state->control->surface != lease.m_state->surface ||
        lease.m_state->control->binding.nativeWindow == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "The native window surface lease binding is unavailable");
    }
    return lease.m_state->control->binding;
}

} // namespace Detail
} // namespace Tina::Integration
