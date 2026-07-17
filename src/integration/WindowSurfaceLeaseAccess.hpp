#pragma once

#include <tina/integration/WindowSurface.hpp>

#include <memory>
#include <thread>

namespace Tina::Integration::Detail {

enum class NativeWindowBindingKind : u8 {
    Win32,
    X11,
    Wayland,
};

struct NativeWindowBinding final {
    NativeWindowBindingKind kind = NativeWindowBindingKind::Win32;
    std::uintptr_t nativeDisplay = 0;
    std::uintptr_t nativeWindow = 0;
    u64 bindingRevision = 1;
};

struct NativeWindowSurfaceLeaseControl final {
    std::thread::id ownerThread{};
    usize activeLeaseCount = 0;
    bool surfaceAlive = true;
};

struct NativeWindowSurfaceLeaseState final {
    std::shared_ptr<NativeWindowSurfaceLeaseControl> control;
    WindowSurfaceId surface{};
    NativeWindowBinding binding{};

    ~NativeWindowSurfaceLeaseState() noexcept;
};

class NativeWindowSurfaceLeaseAccess final {
  public:
    [[nodiscard]] static Core::Result<NativeWindowSurfaceLease>
    Create(std::shared_ptr<NativeWindowSurfaceLeaseControl> control, WindowSurfaceId surface,
           NativeWindowBinding binding) noexcept;

    [[nodiscard]] static Core::Result<NativeWindowBinding> decode(const NativeWindowSurfaceLease& lease) noexcept;
};

} // namespace Tina::Integration::Detail
