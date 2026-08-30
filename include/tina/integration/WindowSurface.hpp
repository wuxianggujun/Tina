#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/Window.hpp>

#include <functional>
#include <memory>

namespace Tina::Integration {

struct WindowSurfaceRegistryTag;
using WindowSurfaceId = Core::GenerationId<WindowSurfaceRegistryTag>;

struct WindowSurfaceSnapshot final {
    WindowSurfaceId surface{};
    Platform::WindowId sourceWindow{};
    Platform::FramebufferExtent framebufferExtent{};
    Platform::ContentScale contentScale{};
    u64 sourceMetricsRevision = 0;
    u64 surfaceRevision = 0;
    // Advances only when the platform handed over a *different* native window, which Android
    // does across background/foreground transitions. Orthogonal to surfaceRevision: that one
    // also moves for a plain resize, where the window itself is unchanged.
    //
    // Render's RenderSurfaceState has carried this since ADR 0034, but nothing ever assigned
    // it -- the whole rebind path was reachable only from tests. Publishing it here is what
    // connects a real platform to it. Desktop backends leave it at 1 forever.
    u64 nativeBindingRevision = 1;
    bool suspended = true;
};

namespace Detail {
struct NativeWindowSurfaceLeaseState;
class NativeWindowSurfaceLeaseAccess;
} // namespace Detail

// Pins one native primary-window binding for the complete RenderDevice
// lifetime. It exposes only a generation identity; the private platform/render
// bridge is the sole decoder of the native binding.
class NativeWindowSurfaceLease final {
  public:
    NativeWindowSurfaceLease() noexcept = default;
    ~NativeWindowSurfaceLease() noexcept;

    NativeWindowSurfaceLease(const NativeWindowSurfaceLease&) = delete;
    NativeWindowSurfaceLease& operator=(const NativeWindowSurfaceLease&) = delete;
    NativeWindowSurfaceLease(NativeWindowSurfaceLease&&) noexcept;
    NativeWindowSurfaceLease& operator=(NativeWindowSurfaceLease&&) noexcept;

    [[nodiscard]] WindowSurfaceId surface() const noexcept;
    [[nodiscard]] bool hasValue() const noexcept;
    explicit operator bool() const noexcept;

  private:
    friend class Detail::NativeWindowSurfaceLeaseAccess;

    explicit NativeWindowSurfaceLease(std::unique_ptr<Detail::NativeWindowSurfaceLeaseState> state) noexcept;

    std::unique_ptr<Detail::NativeWindowSurfaceLeaseState> m_state;
};

class IPrimaryWindowSurfaceProvider {
  public:
    virtual ~IPrimaryWindowSurfaceProvider() noexcept = default;

    [[nodiscard]] virtual Core::Result<NativeWindowSurfaceLease> acquirePrimaryWindowSurfaceLease() noexcept = 0;

    // Returns the value most recently derived from committed Platform metrics.
    // Implementations must not query the native window from this method.
    [[nodiscard]] virtual Core::Result<WindowSurfaceSnapshot> primaryWindowSurfaceSnapshot() const noexcept = 0;
};

class IWindowSurfacePlatformBackend : public Platform::IPlatformBackend, public IPrimaryWindowSurfaceProvider {
  public:
    ~IWindowSurfacePlatformBackend() noexcept override = default;

    // Publishes the window only after the surface-aware RenderDevice was fully
    // initialized. Hidden-window configurations commit without showing it.
    [[nodiscard]] virtual Core::Status publishPrimaryWindow() noexcept = 0;
};

using WindowSurfacePlatformBackendFactory =
    Core::MoveOnlyFunction<Core::Result<std::unique_ptr<IWindowSurfacePlatformBackend>>(
        const Platform::PlatformBackendCreateParams&)>;

} // namespace Tina::Integration
