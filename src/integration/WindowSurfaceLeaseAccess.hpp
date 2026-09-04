#pragma once

#include <tina/integration/WindowSurface.hpp>

#include <memory>
#include <thread>

namespace Tina::Integration::Detail {

enum class NativeWindowBindingKind : u8 {
    Win32,
    X11,
    Wayland,
    // ANativeWindow*. Unlike X11/Wayland there is no display to carry: the window handle is
    // self-contained, which is also why nativeDisplay must stay 0 for this kind.
    Android,
    // A NUL-terminated CSS selector naming the canvas, not a window handle. The pointed-to
    // string must outlive the lease; nativeDisplay stays 0 as there is no display to carry.
    Html5,
    // CAMetalLayer*, which bgfx's Metal swapchain casts nwh straight to on iOS. Like Android there
    // is no display to carry, so nativeDisplay must stay 0. It is the layer and not the UIView: a
    // view passed here is the wrong Objective-C class and fails at draw time rather than at bind
    // time.
    Ios,
};

struct NativeWindowBinding final {
    NativeWindowBindingKind kind = NativeWindowBindingKind::Win32;
    std::uintptr_t nativeDisplay = 0;
    std::uintptr_t nativeWindow = 0;
    u64 bindingRevision = 1;
};

struct NativeWindowSurfaceLeaseControl final {
    std::thread::id ownerThread{};
    WindowSurfaceId surface{};
    NativeWindowBinding binding{};
    usize activeLeaseCount = 0;
    bool surfaceAlive = true;
};

struct NativeWindowSurfaceLeaseState final {
    std::shared_ptr<NativeWindowSurfaceLeaseControl> control;
    WindowSurfaceId surface{};

    ~NativeWindowSurfaceLeaseState() noexcept;
};

class NativeWindowSurfaceLeaseAccess final {
  public:
    [[nodiscard]] static Core::Result<NativeWindowSurfaceLease>
    Create(std::shared_ptr<NativeWindowSurfaceLeaseControl> control, WindowSurfaceId surface,
           NativeWindowBinding binding) noexcept;

    [[nodiscard]] static Core::Status
    rebind(const std::shared_ptr<NativeWindowSurfaceLeaseControl>& control, WindowSurfaceId surface,
           NativeWindowBinding binding) noexcept;

    [[nodiscard]] static Core::Result<NativeWindowBinding> decode(const NativeWindowSurfaceLease& lease) noexcept;
};

} // namespace Tina::Integration::Detail
